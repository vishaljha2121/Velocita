#include "velocita/v2/node.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace velocita::v2 {
namespace {

std::optional<AuthKey> derive(const AuthSecret& secret, const AuthRoute& route,
                              const std::array<std::byte, 16>& incarnation) {
    return AuthKey::derive(secret, route, incarnation);
}

std::optional<std::vector<AuthKey>> derive_accepting(const SecretSet& set, const AuthRoute& route,
                                                     const std::array<std::byte, 16>& incarnation) {
    std::vector<AuthKey> keys;
    for (const auto* secret : {&set.current, set.next ? &*set.next : nullptr,
                               set.previous ? &*set.previous : nullptr}) {
        if (!secret) continue;
        auto key = derive(*secret, route, incarnation);
        if (!key) return std::nullopt;
        keys.push_back(std::move(*key));
    }
    return keys;
}

AuthEnvelopeError verify_any(const std::vector<AuthKey>& keys, std::span<const std::byte> frame,
                             VerifiedAuthEnvelope& destination) {
    auto error = AuthEnvelopeError::tag_mismatch;
    for (const auto& key : keys) {
        error = verify_auth_envelope(key, frame, destination);
        if (error == AuthEnvelopeError::none) return error;
    }
    return error;
}

ResponseCorrelation correlation_of(const CommandIdentity& identity) {
    return {identity.request_sequence.value(), identity.payload_digest};
}

}

struct V2Node::KeySets {
    struct Peer {
        AuthKey send;
        std::vector<AuthKey> receive;
    };
    struct Caller {
        std::vector<AuthKey> ingress;
        AuthKey response;
    };
    std::map<std::int32_t, Peer> peers;
    std::map<std::int32_t, Caller> callers;
};

struct V2Node::PeerLink {
    KeySets::Peer keys;
    SessionSequencer sequencer;
    SessionFence fence{};
    std::vector<std::byte> stash{};
    std::vector<std::byte> pending{};
};

struct V2Node::CallerLink {
    std::int32_t key_id{};
    std::uint64_t caller_id{};
    KeySets::Caller keys;
    SessionSequencer sequencer;
    SessionFence fence{};
    std::uint64_t tokens_milli{};
};

std::optional<V2Node::KeySets> V2Node::derive_keys(const NodeSecrets& secrets) const {
    KeySets sets;
    for (const auto peer : peer_ids_) {
        const auto found = secrets.voters.find(peer);
        if (found == secrets.voters.end()) return std::nullopt;
        auto send = derive(found->second.current, {AuthRole::peer, node_id_, peer}, incarnation_);
        auto receive = derive_accepting(found->second, {AuthRole::peer, peer, node_id_}, incarnation_);
        if (!send || !receive) return std::nullopt;
        sets.peers.emplace(peer, KeySets::Peer{std::move(*send), std::move(*receive)});
    }
    for (const auto& [key_id, caller_id] : caller_ids_) {
        const auto found = secrets.callers.find(key_id);
        if (found == secrets.callers.end()) return std::nullopt;
        auto ingress = derive_accepting(found->second, {AuthRole::ingress, key_id, node_id_}, incarnation_);
        auto response = derive(found->second.current, {AuthRole::response, node_id_, key_id}, incarnation_);
        if (!ingress || !response) return std::nullopt;
        sets.callers.emplace(key_id, KeySets::Caller{std::move(*ingress), std::move(*response)});
    }
    return sets;
}

bool V2Node::replace_secrets(NodeSecrets secrets) {
    auto sets = derive_keys(secrets);
    if (!sets) return false;
    for (auto& [id, link] : peers_) link->keys = std::move(sets->peers.at(id));
    for (auto& [id, link] : callers_) link->keys = std::move(sets->callers.at(id));
    return true;
}

V2Node::V2Node(const NodeConfig& config, const VerifiedExecutionCatalog& catalog, NodeSecrets secrets,
               const std::uint64_t session_epoch, NodeTransport& transport,
               const MetadataCheckpoints metadata_checkpoints)
    : node_id_(config.node_id), incarnation_(config.incarnation), session_epoch_(session_epoch),
      budgets_(config.owner), compact_at_entries_(config.compact_at_entries),
      ingress_rate_per_second_(config.ingress_rate_per_second),
      ingress_burst_milli_(std::uint64_t{config.ingress_burst} * 1000), transport_(transport),
      codec_scratch_(std::make_unique<KernelCommandCodecScratch>()),
      inbound_(kMaxAuthEnvelopeBytes), outbound_(kMaxAuthEnvelopeBytes), gateway_caller_(config.gateway_caller),
      snapshot_(kMaxStateSnapshotBytes), gateway_body_(kMaxStateChunkResponseBytes) {
    if (session_epoch_ == 0) throw std::runtime_error("node session epoch must be positive");
    for (const auto& voter : config.voters)
        if (voter.id != node_id_) peer_ids_.push_back(voter.id);
    for (const auto& caller : config.callers) {
        caller_ids_.emplace(caller.key_id, caller.caller_id);
        caller_by_id_.emplace(caller.caller_id, caller.key_id);
    }
    auto sets = derive_keys(secrets);
    if (!sets) throw std::runtime_error("node secrets are missing a configured route or name an invalid one");
    for (const auto peer : peer_ids_)
        peers_.emplace(peer, std::make_unique<PeerLink>(
                                 PeerLink{std::move(sets->peers.at(peer)), SessionSequencer(session_epoch_)}));
    for (const auto& [key_id, caller_id] : caller_ids_)
        callers_.emplace(key_id, std::make_unique<CallerLink>(CallerLink{
                                     key_id, caller_id, std::move(sets->callers.at(key_id)),
                                     SessionSequencer(session_epoch_), {}, ingress_burst_milli_}));

    const auto kernel_root = config.data_root / "kernel";
    auto startup = [&] {
        if (config.startup == NodeStartup::bootstrap) {
            std::filesystem::create_directories(config.data_root);
            std::filesystem::permissions(config.data_root, std::filesystem::perms::owner_all,
                                         std::filesystem::perm_options::replace);
            return VerifiedIdentityStartup::bootstrap(kernel_root, config.static_identity(), catalog);
        }
        return VerifiedIdentityStartup::recover(kernel_root, config.static_identity(), catalog);
    }();
    kernel_ = std::make_unique<ReplicatedKernel>(
        std::move(startup), StorageCheckpoints{}, metadata_checkpoints, ArchiveStoreConfig{},
        HistoryRecoveryConfig{config.max_history_wal_bytes},
        config.archive_destination_identity.value_or(Digest256{}));
    kernel_->start_writer();
}

V2Node::~V2Node() = default;

void V2Node::check_failed() const {
    if (failed_) throw std::runtime_error("node failed earlier; restart required");
}

bool V2Node::ready() const { return !kernel_->history_recovery_pending(); }

void V2Node::turn(const int elapsed_ms) {
    check_failed();
    try {
        receive_peers();
        kernel_->flush_durability();
        kernel_->tick(elapsed_ms);
        maybe_compact(elapsed_ms);
        send_peers();
        for (auto& [key_id, caller] : callers_)
            caller->tokens_milli = std::min(ingress_burst_milli_, caller->tokens_milli +
                                                                       static_cast<std::uint64_t>(std::max(elapsed_ms, 0)) *
                                                                           ingress_rate_per_second_);
        if (ingress_open_) receive_ingress();
        send_results();
        service_gateway_authority(elapsed_ms);
    } catch (...) {
        failed_ = true;
        throw;
    }
}

void V2Node::maybe_compact(const int elapsed_ms) {
    if (kernel_->compaction_pending()) {
        compaction_ms_ += static_cast<std::uint64_t>(elapsed_ms);
        if (kernel_->compact_within_floor() != 0) {
            ++counters_.compactions;
            counters_.last_compaction_ms = compaction_ms_;
        }
        return;
    }
    compact_wait_ms_ += elapsed_ms;
    if (compact_wait_ms_ < 1000 || kernel_->retained_log_count() < compact_at_entries_) return;
    compact_wait_ms_ = 0;
    compaction_ms_ = 0;
    if (kernel_->compact_within_floor() != 0) {
        ++counters_.compactions;
        counters_.last_compaction_ms = 0;
    }
}

bool V2Node::deliver_peer(PeerLink& link, std::span<const std::byte> body) {
    const auto received = kernel_->receive_peer(body);
    if (received.status == PeerReceiveStatus::busy) {
        ++counters_.peer_busy_holds;
        if (link.pending.empty()) link.pending.assign(body.begin(), body.end());
        return false;
    }
    link.pending.clear();
    ++(received.frame_error == BoundFrameError::none ? counters_.peer_frames_accepted : counters_.peer_frame_errors);
    return true;
}

void V2Node::receive_peers() {
    for (auto& [id, link] : peers_)
        if (!link->pending.empty() && !deliver_peer(*link, std::vector<std::byte>(link->pending))) return;
    for (std::uint32_t i = 0; i < budgets_.peer_fragment_limit; ++i) {
        const auto size = transport_.receive(NodeTransport::Channel::peer, inbound_);
        if (!size) return;
        const auto frame = std::span<const std::byte>(inbound_).first(*size);
        AuthRoute route;
        if (peek_auth_route(frame, route) != AuthEnvelopeError::none || route.role != AuthRole::peer ||
            route.recipient != node_id_ || !peers_.contains(route.sender)) {
            ++counters_.auth_refusals;
            continue;
        }
        auto& link = *peers_.at(route.sender);
        VerifiedAuthEnvelope verified;
        if (verify_any(link.keys.receive, frame, verified) != AuthEnvelopeError::none) {
            ++counters_.auth_refusals;
            continue;
        }
        if (link.fence.admit(verified.header.session_epoch, verified.header.sequence) !=
            SessionFence::Decision::accept) {
            ++counters_.fence_refusals;
            continue;
        }
        ++counters_.peer_frames_fenced;
        if (!deliver_peer(link, verified.body)) return;
    }
}

void V2Node::send_peers() {
    std::array<std::int32_t, kMaxStaticVoters> held{};
    std::size_t held_count = 0;
    for (auto& [id, link] : peers_) {
        if (link->stash.empty()) continue;
        if (transport_.offer_peer(id, link->stash)) {
            link->stash.clear();
        } else {
            ++counters_.peer_backpressure;
            held[held_count++] = id;
        }
    }
    while (held_count < peers_.size()) {
        const auto taken = kernel_->take_outbound_frame(std::span(inbound_).first(kMaxBoundPeerBytes),
                                                        std::span(held).first(held_count));
        if (taken.error == OutboundTakeError::empty) return;
        if (taken.error != OutboundTakeError::none) throw std::runtime_error("peer frame exceeds node buffer");
        const auto found = peers_.find(taken.recipient_id);
        if (found == peers_.end()) throw std::runtime_error("kernel addressed a frame to a non-peer");
        auto& link = *found->second;
        const auto sequence = link.sequencer.next();
        if (!sequence) throw std::runtime_error("peer session sequence exhausted");
        const auto envelope = encode_auth_envelope(link.keys.send, session_epoch_, *sequence,
                                                   std::span(inbound_).first(taken.bytes_written), outbound_);
        if (envelope.error != AuthEnvelopeError::none) throw std::runtime_error("peer envelope encoding failed");
        const auto bytes = std::span<const std::byte>(outbound_).first(envelope.bytes_written);
        if (!transport_.offer_peer(taken.recipient_id, bytes)) {
            ++counters_.peer_backpressure;
            link.stash.assign(bytes.begin(), bytes.end());
            held[held_count++] = taken.recipient_id;
        }
    }
}

bool V2Node::offer_body(CallerLink& caller, std::span<const std::byte> body) {
    const auto sequence = caller.sequencer.next();
    if (!sequence) throw std::runtime_error("response session sequence exhausted");
    const auto envelope = encode_auth_envelope(caller.keys.response, session_epoch_, *sequence, body, outbound_);
    if (envelope.error != AuthEnvelopeError::none) throw std::runtime_error("response envelope encoding failed");
    return transport_.offer_response(caller.key_id, std::span<const std::byte>(outbound_).first(envelope.bytes_written));
}

void V2Node::answer(CallerLink& caller, const NodeResponse& response) {
    std::array<std::byte, kMaxResponseBodyBytes> body{};
    const auto encoded = encode_response(response, body);
    if (encoded.error != NodeProtocolError::none) throw std::runtime_error("response encoding failed");
    const bool offered = offer_body(caller, std::span(body).first(encoded.bytes_written));
    if (response.kind == ResponseKind::committed) {
        ++(offered ? counters_.results_sent : counters_.results_dropped);
    } else if (!offered) {
        ++counters_.responses_dropped;
    }
    if (response.kind == ResponseKind::refused) ++counters_.ingress_refusals;
}

void V2Node::receive_ingress() {
    for (std::uint32_t i = 0; i < budgets_.client_fragment_limit; ++i) {
        const auto size = transport_.receive(NodeTransport::Channel::ingress, inbound_);
        if (!size) return;
        const auto frame = std::span<const std::byte>(inbound_).first(*size);
        AuthRoute route;
        if (peek_auth_route(frame, route) != AuthEnvelopeError::none || route.role != AuthRole::ingress ||
            route.recipient != node_id_ || !callers_.contains(route.sender)) {
            ++counters_.auth_refusals;
            continue;
        }
        auto& caller = *callers_.at(route.sender);
        VerifiedAuthEnvelope verified;
        if (verify_any(caller.keys.ingress, frame, verified) != AuthEnvelopeError::none) {
            ++counters_.auth_refusals;
            continue;
        }
        if (caller.fence.admit(verified.header.session_epoch, verified.header.sequence) !=
            SessionFence::Decision::accept) {
            ++counters_.fence_refusals;
            continue;
        }
        ++counters_.ingress_frames_accepted;
        if (is_gateway_request(verified.body)) {
            handle_gateway(caller, verified.body);
            continue;
        }

        NodeResponse response;
        response.kind = ResponseKind::refused;
        IngressView ingress;
        Command command;
        if (decode_ingress(verified.body, ingress) != NodeProtocolError::none ||
            decode_kernel_command(ingress.command, command, *codec_scratch_).error != KernelCodecError::none) {
            response.reason = static_cast<std::uint8_t>(IngressRefusal::malformed_command);
            answer(caller, response);
            continue;
        }
        response.correlation = correlation_of(command.identity);
        if (command.identity.caller_id.value() != caller.caller_id) {
            response.reason = static_cast<std::uint8_t>(IngressRefusal::caller_mismatch);
        } else if (!ready()) {
            response.reason = static_cast<std::uint8_t>(IngressRefusal::not_ready);
        } else if (!is_operator_caller(command.identity.caller_id) && caller.tokens_milli < 1000) {
            response.reason = static_cast<std::uint8_t>(IngressRefusal::busy);
            ++counters_.ingress_rate_limited;
        } else {
            if (!is_operator_caller(command.identity.caller_id)) caller.tokens_milli -= 1000;
            const auto outcome = kernel_->propose(ingress.command, ingress.lane);
            if (outcome.status == ProposeStatus::admitted) {
                response.kind = ResponseKind::admitted;
                response.term = outcome.term;
                response.index = outcome.index;
            } else {
                response.reason = static_cast<std::uint8_t>(outcome.status);
                if (outcome.status == ProposeStatus::not_leader && outcome.leader_id > 0)
                    response.leader_id = outcome.leader_id;
            }
        }
        answer(caller, response);
    }
}

void V2Node::send_results() {
    while (const auto committed = kernel_->take_committed_result()) {
        const auto found = caller_by_id_.find(committed->identity.caller_id.value());
        if (found == caller_by_id_.end()) {
            ++counters_.results_dropped;
            continue;
        }
        NodeResponse response;
        response.kind = ResponseKind::committed;
        response.correlation = correlation_of(committed->identity);
        response.term = committed->term;
        response.index = committed->index;
        if (encode_kernel_result(committed->result, response.result).error != KernelCodecError::none)
            throw std::runtime_error("committed result did not encode");
        answer(*callers_.at(found->second), response);
    }
}

void V2Node::refuse_gateway(CallerLink& caller, const ResponseCorrelation& correlation, const std::uint8_t reason) {
    const auto replica = kernel_->committed_outbox();
    GatewayRefusal refusal{correlation, reason, reason == static_cast<std::uint8_t>(ProposeStatus::not_leader) &&
                                                        replica.leader_id > 0 ? replica.leader_id : 0,
                           replica.term};
    std::array<std::byte, kResponseGatewayRefusedBytes> body{};
    if (encode_gateway_refusal(refusal, body).error != NodeProtocolError::none)
        throw std::runtime_error("gateway refusal encoding failed");
    ++counters_.gateway_refusals;
    ++counters_.ingress_refusals;
    if (!offer_body(caller, body)) ++counters_.responses_dropped;
}

void V2Node::serve_state(CallerLink& caller, const ResponseCorrelation& correlation) {
    const auto encoded = encode_state_snapshot(kernel_->state(), snapshot_);
    if (encoded.error != StateSnapshotError::none) throw std::runtime_error("applied state did not encode");
    const auto replica = kernel_->committed_outbox();
    StateChunkHeader header;
    header.correlation = correlation;
    header.responder_id = node_id_;
    header.leader_id = replica.leader_id;
    header.term = replica.term;
    header.applied_index = replica.applied_index;
    header.authority_domain = kernel_->static_identity().authority_domain();
    header.snapshot_bytes = static_cast<std::uint32_t>(encoded.bytes_written);
    header.snapshot_digest = sha256_digest(std::span(snapshot_).first(encoded.bytes_written));
    header.chunk_count = static_cast<std::uint16_t>(
        (encoded.bytes_written + kStateChunkPayloadBytes - 1) / kStateChunkPayloadBytes);
    for (std::uint16_t index = 0; index < header.chunk_count; ++index) {
        header.chunk_index = index;
        const auto offset = static_cast<std::size_t>(index) * kStateChunkPayloadBytes;
        const auto length = std::min(kStateChunkPayloadBytes, encoded.bytes_written - offset);
        const auto framed = encode_state_chunk(header, std::span(snapshot_).subspan(offset, length), gateway_body_);
        if (framed.error != NodeProtocolError::none) throw std::runtime_error("state chunk encoding failed");
        if (!offer_body(caller, std::span(gateway_body_).first(framed.bytes_written))) ++counters_.responses_dropped;
    }
    ++counters_.gateway_state_reads;
}

void V2Node::handle_gateway(CallerLink& caller, std::span<const std::byte> body) {
    const auto correlation = gateway_correlation(body);
    GatewayRequest request;
    if (decode_gateway_request(body, request) != NodeProtocolError::none)
        return refuse_gateway(caller, correlation, static_cast<std::uint8_t>(IngressRefusal::malformed_command));
    if (!gateway_caller_ || caller.caller_id != *gateway_caller_)
        return refuse_gateway(caller, correlation, static_cast<std::uint8_t>(IngressRefusal::not_gateway_caller));
    if (!ready()) return refuse_gateway(caller, correlation, static_cast<std::uint8_t>(IngressRefusal::not_ready));
    if (!quiescent())
        return refuse_gateway(caller, correlation, static_cast<std::uint8_t>(ProposeStatus::persistence_busy));
    if (!kernel_->is_leader())
        return refuse_gateway(caller, correlation, static_cast<std::uint8_t>(ProposeStatus::not_leader));
    if (request.kind == GatewayRequestKind::state_read) return serve_state(caller, correlation);

    if (gateway_authority_ && gateway_authority_->barrier == request.barrier) {
        gateway_authority_->correlation = correlation;
        return;
    }
    if (gateway_authority_) {
        kernel_->abandon_authority_request(gateway_authority_->barrier);
        gateway_authority_.reset();
    }
    const auto outcome = kernel_->propose_authority_barrier(request.barrier);
    if (outcome.status != ProposeStatus::admitted)
        return refuse_gateway(caller, correlation, static_cast<std::uint8_t>(outcome.status));
    gateway_authority_ = PendingGatewayAuthority{caller.key_id, request.barrier, correlation, 0};
}

void V2Node::service_gateway_authority(const int elapsed_ms) {
    if (!gateway_authority_) return;
    auto& pending = *gateway_authority_;
    const auto found = callers_.find(pending.key_id);
    if (found == callers_.end()) {
        gateway_authority_.reset();
        return;
    }
    auto& caller = *found->second;
    if (quiescent() && !kernel_->is_leader()) {
        kernel_->abandon_authority_request(pending.barrier);
        const auto correlation = pending.correlation;
        gateway_authority_.reset();
        return refuse_gateway(caller, correlation, static_cast<std::uint8_t>(ProposeStatus::not_leader));
    }
    if (auto proof = kernel_->take_authority_proof()) {
        if (proof->view().request != pending.barrier) return;
        std::array<std::byte, kResponseAuthorityGrantedBytes> body{};
        if (encode_authority_granted(pending.correlation, proof->view(), body).error != NodeProtocolError::none)
            throw std::runtime_error("authority proof encoding failed");
        gateway_authority_.reset();
        ++counters_.gateway_proofs_sent;
        if (!offer_body(caller, body)) ++counters_.responses_dropped;
        return;
    }
    pending.waited_ms += std::max(elapsed_ms, 0);
    if (pending.waited_ms > kGatewayAuthorityNodeBoundMs) {
        kernel_->abandon_authority_request(pending.barrier);
        const auto correlation = pending.correlation;
        gateway_authority_.reset();
        refuse_gateway(caller, correlation, static_cast<std::uint8_t>(IngressRefusal::authority_lost));
    }
}

NodeStatus V2Node::status() const {
    std::uint64_t held = 0;
    for (const auto& [id, link] : peers_) held += link->pending.empty() ? 0 : 1;
    return {kernel_->status(), ready(), kernel_->durable_index(), held, session_epoch_, counters_};
}

bool V2Node::quiescent() const noexcept {
    return !kernel_->persistence_pending() && !kernel_->compaction_pending();
}

}
