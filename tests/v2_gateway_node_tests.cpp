#include "v2_catalog_test_support.hpp"
#include "v2_swap_test_support.hpp"
#include "velocita/v2/swap_paper_gateway.hpp"
#ifdef VELOCITA_SWAP_SUBMISSION_DRILL
#include "velocita/v2/swap_submission_session.hpp"
#include "velocita/v2/swap_submission_resolution.hpp"
#include "velocita/v2/swap_rest_codec.hpp"
#include "velocita/v2/swap_prepare_session.hpp"
#include "velocita/v2/swap_order_status.hpp"
#ifdef VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE
#include "v2_swap_signer_test_support.hpp"
#endif
#endif
#ifdef VELOCITA_SWAP_TLS_DRILL
#include "velocita/v2/swap_reconcile_session.hpp"
#include "velocita/v2/swap_reconcile_store.hpp"
#endif

#include "velocita/v2/gateway_dispatch.hpp"
#include "velocita/v2/gateway_loop.hpp"
#include "velocita/v2/node.hpp"
#include "velocita/v2/paper_venue.hpp"
#include "velocita/v2/paper_venue_store.hpp"
#include <fstream>
#include "velocita/v2/venue_simulator.hpp"
#include "velocita/v2/venue_transport.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using velocita::v2::Command;
using namespace velocita::v2;
using Bytes = std::vector<std::byte>;
using SteadyClock = std::chrono::steady_clock;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::int32_t kOperatorKey = 11, kStrategyKey = 12, kGatewayKey = 13, kSignerKey = 14;
constexpr std::uint64_t kOperator = 9, kStrategy = 21, kGateway = 22, kSigner = 23;
const std::array<std::byte, 16> kIncarnation{std::byte{0xAB}, std::byte{0x5A}, std::byte{0x02}};
constexpr std::uint8_t kLinkSeed = 0x10, kWrongSeed = 0x90;

std::uint8_t caller_seed(std::int32_t key) { return static_cast<std::uint8_t>(0x60 + 4 * (key - 10)); }

AuthSecret secret(std::uint8_t seed) {
    Bytes out(kAuthSecretBytes);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::byte>(seed + i);
    return std::move(*AuthSecret::from_bytes(out));
}
AuthKey key(std::uint8_t seed, AuthRole role, std::int32_t sender, std::int32_t recipient) {
    return std::move(*AuthKey::derive(secret(seed), {role, sender, recipient}, kIncarnation));
}
std::uint8_t link_seed(std::int32_t a, std::int32_t b) { return static_cast<std::uint8_t>(kLinkSeed + 4 * (a + b - 3)); }

Digest256 digest(std::uint64_t marker) {
    Digest256 value{};
    value.bytes[0] = static_cast<std::byte>(marker);
    value.bytes[1] = static_cast<std::byte>(marker >> 8);
    value.bytes[2] = std::byte{0x77};
    return value;
}

Tick steady_ms() {
    return static_cast<Tick>(
        std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now().time_since_epoch()).count());
}

constexpr std::int64_t kOrderLots = 2;

const VerifiedExecutionCatalog& paper_catalog() {
    static const auto value = [] {
        const std::array rows{catalog_test::row(AccountId{5},
            {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4}, InstrumentSpecVersion{7},
             MoneyMicros::from_atoms(100), PriceTicks::from_atoms(1), PriceTicks::from_atoms(100),
             QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
            {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
             (kVenueDirectivePlaceLimit | kVenueDirectiveCancel), kVenueTifFok, 0})};
        return catalog_test::catalog(rows);
    }();
    return value;
}
bool g_swap = false;
bool g_paper = false;
bool g_exit_template = false;
std::uint32_t g_ingress_rate = 0, g_ingress_burst = 0;
const VerifiedExecutionCatalog& fixture_catalog() { return g_swap ? shadow_catalog::truemarkets_two_row_catalog() : g_paper ? paper_catalog() : catalog_test::standard_catalog(); }

PolicyArtifact policy_artifact() {
    PolicyArtifact value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.schema_version = kPolicyArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.model_digest = digest(0x11);
    value.artifact_digest = digest(0x12);
    value.valid_from_event_time_ns = 100;
    value.valid_until_event_time_ns = 200;
    value.risk_policy_version = RiskPolicyVersion{6};
    value.rule_count = 1;
    value.rules[0] = {0, PredicateOperator::greater_than, FeatureMicros::from_atoms(10), Directive::place_limit};
    if (g_exit_template) {
        value.rule_count = 2;
        value.rules[1] = {0, PredicateOperator::less_than, FeatureMicros::from_atoms(3), Directive::place_limit};
    }
    return value;
}

StageExecutablePolicy executable_bundle() {
    StageExecutablePolicy value;
    value.feature_schema.schema_version = kFeatureSchemaArtifactSchemaVersion;
    value.feature_schema.feature_schema_id = FeatureSchemaId{3};
    value.feature_schema.feature_count = 1;
    value.feature_schema.features[0] = {FeatureEconomicKind::price, 1};
    value.feature_schema.artifact_digest = compute_artifact_digest(value.feature_schema).digest;
    auto& executable = value.executable_policy;
    executable.schema_version = kExecutablePolicyArtifactSchemaVersion;
    executable.policy_id = PolicyId{10};
    executable.policy_version = PolicyVersion{1};
    executable.policy_artifact_digest = policy_artifact().artifact_digest;
    executable.feature_schema_id = value.feature_schema.feature_schema_id;
    executable.feature_schema_digest = value.feature_schema.artifact_digest;
    executable.template_count = 1;
    auto& action = executable.templates[0];
    action.schema_version = kActionTemplateSchemaVersion;
    action.directive = Directive::place_limit;
    action.side = Side::buy;
    action.time_in_force = g_paper ? TimeInForce::fill_or_kill : TimeInForce::good_til_cancel;
    action.post_only = g_paper ? PostOnlyMode::prohibited : PostOnlyMode::required;
    action.order_slot = LogicalOrderSlot{1};
    action.instrument_spec_version = InstrumentSpecVersion{7};
    action.venue_capabilities_version = VenueCapabilitiesVersion{8};
    action.price_source = {EconomicValueSourceKind::feature, 0, RoundingMode::toward_zero, {}, 1, {}};
    action.quantity_source = {EconomicValueSourceKind::fixed, 0, RoundingMode::toward_zero,
                              QuantityLots::from_atoms(kOrderLots), 0};
    action.safety_caps = {QuantityLots::from_atoms(kOrderLots), MoneyMicros::from_atoms(10'000),
                          PriceTicks::from_atoms(2), PriceTicks::from_atoms(1), 100, action.price_source};
    if (g_exit_template) {
        executable.template_count = 2;
        executable.templates[1] = action;
        executable.templates[1].side = Side::sell;
    }
    executable.rule_binding_count = 1;
    executable.rule_template_slots[0] = ActionTemplateSlot{1};
    if (g_exit_template) {
        executable.rule_binding_count = 2;
        executable.rule_template_slots[1] = ActionTemplateSlot{2};
    }
    executable.artifact_digest = compute_artifact_digest(executable).digest;
    value.instrument = {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
                        InstrumentSpecVersion{7}, MoneyMicros::from_atoms(100), PriceTicks::from_atoms(1),
                        PriceTicks::from_atoms(100), QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)};
    value.venue = {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
                   (kVenueDirectivePlaceLimit | kVenueDirectiveCancel), g_paper ? kVenueTifFok : kVenueTifGtc,
                   static_cast<std::uint8_t>(g_paper ? 0 : 1)};
    return value;
}

DecisionFrame decision_frame() {
    DecisionFrame value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.feature_schema_id = FeatureSchemaId{3};
    value.venue_id = VenueId{9};
    value.venue_session_id = VenueSessionId{1};
    value.venue_sequence = VenueSequence{1};
    value.source_id = SourceId{12};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{1};
    value.event_time_ns = 150;
    value.receive_time_ns = 151;
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.feature_count = 1;
    value.features[0] = FeatureMicros::from_atoms(11);
    value.market_data_status = MarketDataStatus::ready;
    value.market_data_age_ns = 1;
    value.position_version = PositionVersion{1};
    value.risk_version = RiskVersion{1};
    value.raw_evidence_digest = digest(0x44);
    return value;
}

struct Network;

struct FakeTransport final : NodeTransport {
    Network* network{};
    std::int32_t self{};
    std::deque<Bytes> peer_inbox, ingress_inbox;
    std::optional<std::size_t> receive(Channel channel, std::span<std::byte> buffer) override {
        auto& inbox = channel == Channel::peer ? peer_inbox : ingress_inbox;
        if (inbox.empty()) return std::nullopt;
        auto frame = std::move(inbox.front());
        inbox.pop_front();
        require(frame.size() <= buffer.size(), "fixture frame exceeds node buffer");
        std::copy(frame.begin(), frame.end(), buffer.begin());
        return frame.size();
    }
    bool offer_peer(std::int32_t voter, std::span<const std::byte> envelope) override;
    bool offer_response(std::int32_t caller, std::span<const std::byte> envelope) override;
};

std::uint8_t body_kind(std::span<const std::byte> envelope) {
    return envelope.size() > kAuthHeaderBytes ? std::to_integer<std::uint8_t>(envelope[kAuthHeaderBytes]) : 0;
}

struct Network {
    std::map<std::int32_t, FakeTransport> transports;
    std::map<std::int32_t, std::deque<Bytes>> responses;
    std::set<std::int32_t> down;
    std::function<bool(std::int32_t node, std::span<const std::byte>)> gateway_ingress = {};
    std::function<bool(std::span<const std::byte>)> gateway_response = {};
    bool duplicate_gateway_responses{};
    std::vector<Bytes> gateway_tap;

    FakeTransport& at(std::int32_t id) {
        auto& transport = transports[id];
        transport.network = this;
        transport.self = id;
        return transport;
    }
};

bool FakeTransport::offer_peer(std::int32_t voter, std::span<const std::byte> envelope) {
    if (network->down.contains(voter)) return false;
    network->at(voter).peer_inbox.emplace_back(envelope.begin(), envelope.end());
    return true;
}
bool FakeTransport::offer_response(std::int32_t caller, std::span<const std::byte> envelope) {
    if (caller == kGatewayKey) {
        network->gateway_tap.emplace_back(envelope.begin(), envelope.end());
        if (network->gateway_response && !network->gateway_response(envelope)) return true;
        if (network->duplicate_gateway_responses) network->responses[caller].emplace_back(envelope.begin(), envelope.end());
    }
    network->responses[caller].emplace_back(envelope.begin(), envelope.end());
    return true;
}

struct GatewayClient final : ClientTransport {
    Network* network{};
    std::int32_t caller_key{kGatewayKey};
    bool offer_ingress(std::int32_t node, std::span<const std::byte> envelope) override {
        if (network->down.contains(node)) return false;
        if (network->gateway_ingress && !network->gateway_ingress(node, envelope)) return true;
        network->at(node).ingress_inbox.emplace_back(envelope.begin(), envelope.end());
        return true;
    }
    std::optional<std::size_t> receive_response(std::span<std::byte> buffer) override {
        auto& queue = network->responses[caller_key];
        if (queue.empty()) return std::nullopt;
        auto frame = std::move(queue.front());
        queue.pop_front();
        std::copy(frame.begin(), frame.end(), buffer.begin());
        return frame.size();
    }
};

NodeConfig config_for(const std::filesystem::path& base, std::int32_t id, NodeStartup startup,
                      const std::vector<std::int32_t>& voters, bool gateway_configured) {
    NodeConfig config;
    config.node_id = id;
    config.data_root = base / std::to_string(id);
    config.startup = startup;
    config.incarnation = kIncarnation;
    config.catalog_root = fixture_catalog().root();
    config.catalog_file = "/unused-by-node";
    config.deployment_mode = DeploymentMode::laboratory;
    for (const std::int32_t voter : voters)
        config.voters.push_back({voter, {"127.0.0.1", static_cast<std::uint16_t>(40200 + voter)},
                                 voter == id ? std::nullopt : std::optional<std::filesystem::path>("/k")});
    config.ingress_endpoint = {"127.0.0.1", static_cast<std::uint16_t>(40300 + id)};
    config.callers.push_back({kOperatorKey, kOperator, "/c11", {"127.0.0.1", 40411}});
    config.callers.push_back({kStrategyKey, kStrategy, "/c12", {"127.0.0.1", 40412}});
    config.callers.push_back({kGatewayKey, kGateway, "/c13", {"127.0.0.1", 40413}});
    if (gateway_configured) config.gateway_caller = kGateway;
    if (g_swap) {
        config.callers.push_back({kSignerKey,kSigner,"/c14",{"127.0.0.1",40414}});
        config.signer_caller=kSigner;
    }
    config.max_history_wal_bytes = 1ULL << 30;
    if (g_ingress_rate != 0) {
        config.ingress_rate_per_second = g_ingress_rate;
        config.ingress_burst = g_ingress_burst;
    }
    config.digest = digest(static_cast<std::uint64_t>(id));
    return config;
}

NodeSecrets secrets_for(std::int32_t id, const std::vector<std::int32_t>& voters) {
    NodeSecrets secrets;
    for (const auto peer : voters)
        if (peer != id) secrets.voters.emplace(peer, SecretSet{secret(link_seed(id, peer))});
    for (const auto caller : {kOperatorKey, kStrategyKey, kGatewayKey})
        secrets.callers.emplace(caller, SecretSet{secret(caller_seed(caller))});
    if (g_swap) secrets.callers.emplace(kSignerKey,SecretSet{secret(caller_seed(kSignerKey))});
    return secrets;
}

struct Caller {
    Caller(std::int32_t key, std::uint64_t caller) : key_id(key), caller_id(caller) {}
    std::int32_t key_id{};
    std::uint64_t caller_id{};
    SessionSequencer sequencer{1};
    std::map<std::int32_t, SessionFence> fences;
    std::uint64_t next_sequence{1};

    Bytes seal(std::int32_t node, std::span<const std::byte> body, std::uint8_t seed) {
        Bytes out(kMaxAuthEnvelopeBytes);
        const auto env = encode_auth_envelope(key(seed, AuthRole::ingress, key_id, node), 1, *sequencer.next(), body, out);
        require(env.error == AuthEnvelopeError::none, "fixture ingress envelope did not encode");
        out.resize(env.bytes_written);
        return out;
    }
    Bytes ingress(std::int32_t node, const Command& command, ProposeLane lane) {
        std::array<std::byte, kMaxKernelCommandBytes> bytes{};
        const auto encoded = encode_kernel_command(command, bytes);
        require(encoded.error == KernelCodecError::none, "fixture command did not encode");
        std::array<std::byte, kMaxIngressBodyBytes> body{};
        const auto framed = encode_ingress(lane, std::span(bytes).first(encoded.bytes_written), body);
        require(framed.error == NodeProtocolError::none, "fixture ingress did not encode");
        return seal(node, std::span(body).first(framed.bytes_written), caller_seed(key_id));
    }
    std::optional<std::pair<std::int32_t, Bytes>> open(const Bytes& frame) {
        AuthRoute route;
        if (peek_auth_route(frame, route) != AuthEnvelopeError::none || route.role != AuthRole::response) return std::nullopt;
        VerifiedAuthEnvelope verified;
        if (verify_auth_envelope(key(caller_seed(key_id), AuthRole::response, route.sender, key_id), frame, verified) !=
                AuthEnvelopeError::none ||
            fences[route.sender].admit(verified.header.session_epoch, verified.header.sequence) !=
                SessionFence::Decision::accept)
            return std::nullopt;
        return std::pair{route.sender, Bytes(verified.body.begin(), verified.body.end())};
    }
};

std::atomic<std::int32_t> g_slow_disk_voter{0};
std::atomic<int> g_slow_disk_ms{0};

void slow_disk(const StorageCheckpoint checkpoint, void* voter) {
    if (checkpoint != StorageCheckpoint::before_block_write ||
        g_slow_disk_voter.load() != *static_cast<const std::int32_t*>(voter)) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(g_slow_disk_ms.load()));
}

struct Cluster {
    std::filesystem::path base;
    Network network;
    std::vector<std::int32_t> voters;
    std::map<std::int32_t, std::int32_t> disk_owner;
    std::map<std::int32_t, std::unique_ptr<V2Node>> nodes;
    std::map<std::int32_t, std::uint64_t> epochs;
    bool gateway_configured;
    std::function<void()> on_round = {};

    Cluster(std::filesystem::path root, std::vector<std::int32_t> ids, bool gateway = true)
        : base(std::move(root)), voters(std::move(ids)), gateway_configured(gateway) {
        for (const auto id : voters) {
            epochs[id] = 1;
            disk_owner[id] = id;
            nodes[id] = std::make_unique<V2Node>(config_for(base, id, NodeStartup::bootstrap, voters, gateway_configured),
                                                 fixture_catalog(), secrets_for(id, voters), epochs[id],
                                                 network.at(id), velocita::MetadataCheckpoints{},
                                                 StorageCheckpoints{&slow_disk, &disk_owner[id]});
        }
    }
    ~Cluster() {
        g_slow_disk_voter = 0;
        nodes.clear();
    }
    V2Node& node(std::int32_t id) { return *nodes.at(id); }
    void stop(std::int32_t id) {
        nodes.erase(id);
        network.down.insert(id);
        network.at(id).peer_inbox.clear();
        network.at(id).ingress_inbox.clear();
    }
    SteadyClock::time_point last = SteadyClock::now();
    void run(int rounds) {
        for (int round = 0; round < rounds; ++round) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            const auto now = SteadyClock::now();
            const auto elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count());
            if (elapsed > 0) last = now;
            for (auto& [id, node] : nodes) node->turn(elapsed);
            if (on_round) on_round();
        }
    }
    template <typename Predicate>
    void run_until(Predicate predicate, const std::string& what, int limit = 40000) {
        for (int round = 0; round < limit; ++round) {
            if (predicate()) return;
            run(1);
        }
        throw std::runtime_error("timed out waiting for " + what);
    }
    std::int32_t leader() {
        for (auto& [id, node] : nodes)
            if (node->status().replica.role == "leader" && node->status().ready) return id;
        return 0;
    }
    std::int32_t elect() {
        run_until([&] { return leader() != 0; }, "a ready leader");
        return leader();
    }
    ApplyResult commit(Caller& caller, CommandBody body, ProposeLane lane = ProposeLane::normal) {
        Command command{{CallerId{caller.caller_id}, RequestSequence{caller.next_sequence++},
                               digest(caller.caller_id * 1000 + caller.next_sequence)}, std::move(body)};
        if (const auto* swap = std::get_if<ApplySpotSwap>(&command.body))
            command.identity.payload_digest = swap_record_digest(swap->record);
        for (int attempt = 0; attempt < 20; ++attempt) {
            const auto target = elect();
            network.at(target).ingress_inbox.push_back(caller.ingress(target, command, lane));
            for (int round = 0; round < 4000; ++round) {
                run(1);
                auto& queue = network.responses[caller.key_id];
                while (!queue.empty()) {
                    auto frame = std::move(queue.front());
                    queue.pop_front();
                    const auto opened = caller.open(frame);
                    if (!opened) continue;
                    NodeResponse response;
                    if (decode_response(opened->second, response) != NodeProtocolError::none ||
                        response.correlation.request_sequence != command.identity.request_sequence.value())
                        continue;
                    if (response.kind == ResponseKind::committed) {
                        ApplyResult result;
                        require(decode_kernel_result(response.result, result).error == KernelCodecError::none,
                                "committed result did not decode");
                        return result;
                    }
                    if (response.kind == ResponseKind::refused) {
                        const auto retry_at = steady_ms() + 500;
                        while (steady_ms() < retry_at) run(1);
                        round = 4000;
                    }
                }
            }
        }
        throw std::runtime_error("command never committed");
    }
    const TradingState& state_of(std::int32_t id) { return node(id).applied_state(); }
};

std::filesystem::path fresh_base(const char* leaf) {
    auto pattern = (std::filesystem::temp_directory_path() / (std::string(leaf) + "-XXXXXX")).string();
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    require(::mkdtemp(bytes.data()) != nullptr, "mkdtemp failed");
    return std::filesystem::canonical(bytes.data());
}

struct Base {
    std::filesystem::path path;
    explicit Base(const char* leaf) : path(fresh_base(leaf)) {}
    ~Base() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

void prepare_cluster(Cluster& cluster, Caller& operator_caller, Caller& strategy) {
    require(cluster.commit(operator_caller, AssignCallerRole{CallerId{kStrategy}, CallerRole::strategy}).code ==
                ApplyCode::applied, "strategy role");
    require(cluster.commit(operator_caller, AssignCallerRole{CallerId{kGateway}, CallerRole::gateway}).code ==
                ApplyCode::applied, "gateway role");
    require(cluster.commit(operator_caller, AdvanceKillEpoch{KillEpoch{1}}, ProposeLane::control).code == ApplyCode::applied,
            "kill epoch");
    require(cluster.commit(operator_caller, SetExecutorGeneration{ExecutorGeneration{1}}, ProposeLane::control).code ==
                ApplyCode::applied, "executor generation");
    require(cluster.commit(operator_caller, InstallExecutionCatalog{fixture_catalog().root(), KillEpoch{1},
                                                                    ExecutorGeneration{1}}).code == ApplyCode::applied,
            "catalog install");
    require(cluster.commit(operator_caller, SetStrategyMode{StrategyMode::normal, KillEpoch{1}, ExecutorGeneration{1}},
                           ProposeLane::control).code == ApplyCode::applied, "strategy mode");
    ConfigureRiskLimits limits;
    limits.account_id = AccountId{5};
    limits.instrument_id = InstrumentId{4};
    limits.risk_policy_version = RiskPolicyVersion{6};
    limits.expected_kill_epoch = KillEpoch{1};
    limits.expected_executor_generation = ExecutorGeneration{1};
    limits.max_order_quantity = QuantityLots::from_atoms(4);
    limits.max_reserved_quantity = QuantityLots::from_atoms(8);
    limits.max_order_gross_notional = MoneyMicros::from_atoms(4'000);
    limits.max_reserved_exposure = MoneyMicros::from_atoms(8'000);
    limits.initial_margin_ppm = 1'000'000;
    limits.maximum_fee_ppm = g_paper ? 15'000 : 2'500;
    limits.max_position_quantity = QuantityLots::from_atoms(100);
    limits.allow_short = false;
    limits.reason_code = 1;
    require(cluster.commit(operator_caller, limits).code == ApplyCode::applied, "risk limits");
    require(cluster.commit(operator_caller, PublishPolicy{policy_artifact()}).code == ApplyCode::applied, "publish");
    require(cluster.commit(operator_caller, ValidatePolicy{PolicyId{10}, PolicyVersion{1}}).code == ApplyCode::applied,
            "validate");
    const auto bundle = executable_bundle();
    require(cluster.commit(operator_caller, bundle).code == ApplyCode::applied, "stage");
    require(cluster.commit(operator_caller, ActivateExecutablePolicy{PolicyId{10}, PolicyVersion{1},
                                                                     bundle.executable_policy.artifact_digest}).code ==
                ApplyCode::applied, "activate");
    require(cluster.commit(strategy, RecordSourceEvent{SourceId{12}, SourceGeneration{1}, SourceSequence{1}, digest(0x44)})
                    .code == ApplyCode::applied, "source event");
}

EffectId authorize(Cluster& cluster, Caller& strategy) {
    const auto evaluated =
        cluster.commit(strategy, EvaluateDecisionFrame{DecisionId{20}, EvaluationMode::authoritative, decision_frame()});
    require(evaluated.decision_code == DecisionCode::action, "the decision did not produce an action");
    const auto authorized = cluster.commit(strategy, AuthorizeDecision{DecisionId{20}});
    require(authorized.code == ApplyCode::applied && authorized.effect_id().value() != 0, "authorization failed");
    return authorized.effect_id();
}

std::vector<ClientRoute> gateway_routes(const std::vector<std::int32_t>& voters, std::uint8_t seed, std::int32_t caller_key=kGatewayKey) {
    std::vector<ClientRoute> routes;
    for (const auto id : voters) {
        std::vector<AuthKey> responses;
        responses.push_back(key(seed, AuthRole::response, id, caller_key));
        routes.push_back({id, key(seed, AuthRole::ingress, caller_key, id), std::move(responses)});
    }
    return routes;
}

std::array<std::byte, 16> process_incarnation(std::uint8_t seed) {
    std::array<std::byte, 16> value{};
    for (std::size_t i = 0; i < value.size(); ++i) value[i] = static_cast<std::byte>(seed + 3 * i + 1);
    return value;
}

Tick loop_clock(void*) noexcept { return steady_ms(); }
double wall_clock(void*) noexcept { return 1'000'000.0 + static_cast<double>(steady_ms()) / 1000.0; }

struct CountingVenue {
    sim::VenueTransport inner{};
    std::vector<OrderEffect> sends;
    static sim::SendOutcome send(void* self, const OrderEffect& effect, Tick now) {
        auto& venue = *static_cast<CountingVenue*>(self);
        venue.sends.push_back(effect);
        return venue.inner.send(effect, now);
    }
    std::vector<VenueObservation> injected;
    static std::size_t poll(void* self, std::span<VenueObservation> out, Tick now) {
        auto& venue = *static_cast<CountingVenue*>(self);
        auto count = venue.inner.poll(out, now);
        if (venue.hold_fills) {
            std::size_t kept = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (out[i].kind == VenueObservationKind::fill) venue.held_fills.push_back(out[i]);
                else out[kept++] = out[i];
            }
            count = kept;
        }
        while (!venue.hold_fills && count < out.size() && !venue.held_fills.empty()) {
            out[count++] = venue.held_fills.front();
            venue.held_fills.erase(venue.held_fills.begin());
        }
        while (count < out.size() && !venue.injected.empty()) {
            out[count++] = venue.injected.front();
            venue.injected.erase(venue.injected.begin());
        }
        return count;
    }
    static void acknowledge(void* self, const VenueObservation& observation) {
        static_cast<CountingVenue*>(self)->inner.acknowledge(observation);
    }
    std::size_t queries{};
    bool hold_fills{};
    std::vector<VenueObservation> held_fills{};
    static sim::QueryResult query(void* self, ClusterOrderId order, Tick now) {
        auto& venue = *static_cast<CountingVenue*>(self);
        ++venue.queries;
        if (venue.hold_fills) return {};
        return venue.inner.query(order, now);
    }
    static VenueSessionId session(void* self) { return static_cast<CountingVenue*>(self)->inner.session(); }
    sim::VenueTransport transport() { return {&send, &poll, &acknowledge, &query, &session, this}; }
};

struct LaggingBook {
    sim::VenueBookSource inner{};
    static void read(void* self, sim::VenueBook& out) {
        auto& book = *static_cast<LaggingBook*>(self);
        book.inner.read_fn(book.inner.context, out);
        out.unacknowledged_reports = 0;
        out.pending_order_count = 0;
    }
    static void committed(void* self, const sim::CommittedEvidence& evidence) {
        auto& book = *static_cast<LaggingBook*>(self);
        if (book.inner.committed_fn) book.inner.committed_fn(book.inner.context, evidence);
    }
    sim::VenueBookSource source() { return {&read, &committed, this}; }
};

struct VenueChoice {
    std::optional<std::filesystem::path> durable_state;
    paper::PaperFaultPolicy paper_faults = paper::PaperFaultPolicy::clean();
    bool lagging_book{};
};

struct Gateway {
    GatewayClient client;
    GatewayLink link;
    sim::SimulatedVenue venue;
    std::unique_ptr<paper::PaperVenue> paper_venue;
    std::unique_ptr<paper::DurablePaperVenue> durable_venue;
    sim::VenueBookSource book{};
    LaggingBook lagging{};
    CountingVenue counting;
    GatewayLoop loop;

    static GatewayLoopConfig config() {
        GatewayLoopConfig value;
        value.caller = CallerId{kGateway};
        value.first_sequence = 1;
        value.sequence_limit = 1'000'000;
        value.venue = {VenueId{9}, VenueCapabilitiesVersion{8}, true, true};
        value.account = AccountId{5};
        value.instrument = InstrumentId{4};
        value.instrument_spec_version = InstrumentSpecVersion{7};
        value.timeouts = {2000, 3, 500, 4, 1000, 60'000};
        value.freshness_bound = 3000;
        value.lease_bound = 2000;
        value.state_interval = 100;
        value.request_timeout = 2000;
        value.ack_interval = 300;
        value.allow_new_risk = true;
        return value;
    }
    static paper::PaperVenueConfig paper_config() {
        const auto* row = paper_catalog().lookup(AccountId{5}, VenueId{9}, InstrumentId{4}, InstrumentSpecVersion{7},
                                                 VenueCapabilitiesVersion{8});
        paper::PaperVenueConfig value;
        value.spec = {row->instrument.venue_id, row->instrument.instrument_id, row->tick_quote_micros,
                      row->lot_base_atoms, row->base_decimals};
        value.profile = row->instrument;
        value.buy_fee_bps = 115;
        value.sell_fee_bps = 120;
        value.quote_balance = MoneyMicros::from_atoms(25'000'000);
        value.latency = 1;
        return value;
    }
    static GatewayLoopConfig with_view(GatewayLoopConfig value) {
        if (g_paper) value.venue.supports_post_only = value.venue.supports_good_til_cancel = false;
        return value;
    }
    sim::VenueTransport pick(const VenueChoice& choice) {
        sim::VenueTransport transport;
        if (choice.durable_state) {
            auto opened = paper::DurablePaperVenue::open(*choice.durable_state, paper_config(), choice.paper_faults,
                                                         kIncarnation);
            require(opened.error == paper::PaperStateError::none,
                    std::string("fixture paper state refused: ") + paper::describe(opened.error));
            durable_venue = std::move(opened.venue);
            book = durable_venue->book();
            transport = durable_venue->transport();
        } else if (g_paper) {
            paper_venue = std::make_unique<paper::PaperVenue>(paper_config(), choice.paper_faults, 1);
            book = paper::paper_venue_book(*paper_venue);
            transport = paper::paper_venue_transport(*paper_venue);
        } else {
            book = sim::simulated_venue_book(venue);
            transport = sim::simulated_venue_transport(venue);
        }
        if (choice.lagging_book) {
            lagging.inner = book;
            book = lagging.source();
        }
        return transport;
    }
    Gateway(Cluster& cluster, sim::FaultPolicy faults = sim::FaultPolicy::clean(),
            GatewayLoopConfig loop_config = config(), GatewayLinkTiming timing = {}, const VenueChoice& choice = {})
        : client{},
          link((client.network = &cluster.network, 1), process_incarnation(0x40),
               gateway_routes(cluster.voters, caller_seed(kGatewayKey)), client, timing),
          venue(7, faults, VenueId{9}),
          counting{pick(choice), {}},
          loop(with_view(loop_config), link, fixture_catalog(), counting.transport(),
               {&loop_clock, &wall_clock, nullptr}, book) {}
};

void create_fresh_paper_state(const std::filesystem::path& file) {
    const auto fresh = paper::fresh_paper_state(Gateway::paper_config(), kIncarnation,
                                                MoneyMicros::from_atoms(25'000'000));
    require(fresh.error.empty() && paper::create_paper_state(file, *fresh.state) == paper::PaperStateError::none,
            "fixture paper state could not be created");
}

std::uint64_t stops(const GatewayMetrics& metrics, GatewayStopCause cause) {
    return metrics.stops[static_cast<std::size_t>(cause)];
}

std::uint64_t stops_other_than(const GatewayMetrics& metrics, GatewayStopCause cause) {
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < metrics.stops.size(); ++i)
        if (i != static_cast<std::size_t>(cause)) total += metrics.stops[i];
    return total;
}

std::vector<GatewayEvent> drain(GatewayLoop& loop) {
    std::array<GatewayEvent, kMaxGatewayEvents> out{};
    const auto count = loop.drain_events(out);
    return {out.begin(), out.begin() + static_cast<std::ptrdiff_t>(count)};
}

std::int64_t highest_term(Cluster& cluster) {
    std::int64_t term = 0;
    for (auto& [id, node] : cluster.nodes) term = std::max(term, node->status().replica.term);
    return term;
}

std::uint64_t authority(const GatewayMetrics& metrics, GatewayAuthorityResult result) {
    return metrics.authority[static_cast<std::size_t>(result)];
}

const RiskAccount* account(const TradingState& state) {
    for (std::size_t i = 0; i < state.risk_account_count; ++i)
        if (state.risk_accounts[i].account_id == AccountId{5} && state.risk_accounts[i].instrument_id == InstrumentId{4})
            return &state.risk_accounts[i];
    return nullptr;
}

const CallerCursor* cursor(const TradingState& state, std::uint64_t caller) {
    for (std::size_t i = 0; i < state.caller_cursor_count; ++i)
        if (state.caller_cursors[i].caller_id == CallerId{caller}) return &state.caller_cursors[i];
    return nullptr;
}

const ClusterOrder* order_of(const TradingState& state, EffectId effect) {
    for (std::size_t i = 0; i < state.order_count; ++i)
        if (state.orders[i].effect_id == effect) return &state.orders[i];
    return nullptr;
}

void start(Cluster& cluster, Gateway& gateway) {
    cluster.on_round = [&] { gateway.loop.step(); };
    cluster.run_until([&] { return gateway.loop.phase() == GatewayPhase::running && !gateway.loop.new_risk_stopped(); },
                      "the gateway to activate on a clean cluster");
}

void authorized_order_is_delivered_once_and_its_fill_moves_the_position() {
    Base base("v2-gw-e2e");
    Cluster cluster(base.path, {1, 2, 3});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto leader = cluster.elect();
    require(account(cluster.state_of(leader))->position_quantity.atoms() == 0, "fixture starts flat");
    Gateway gateway(cluster);
    start(cluster, gateway);
    const auto effect = authorize(cluster, strategy);
    cluster.run_until([&] {
        const auto* risk = account(cluster.state_of(leader));
        return risk && risk->position_quantity.atoms() == kOrderLots && gateway.loop.evidence().obligation_count == 0;
    }, "the fill to commit and move the position");
    cluster.run(3000);

    require(gateway.counting.sends.size() == 1 && gateway.counting.sends[0].effect_id == effect,
            "the venue received " + std::to_string(gateway.counting.sends.size()) + " sends, not the one effect once");
    const auto& executions = gateway.venue.executions();
    require(executions.size() == 1, "the venue executed " + std::to_string(executions.size()) + " times, not once");
    require(executions[0].quantity.atoms() == kOrderLots && executions[0].side == Side::buy,
            "the venue executed different economics than the authorized decision");
    for (const auto id : cluster.voters)
        require(account(cluster.state_of(id))->position_quantity.atoms() == kOrderLots,
                "voter " + std::to_string(id) + " did not book the committed fill");
    require(order_of(cluster.state_of(leader), effect) == nullptr ||
                order_of(cluster.state_of(leader), effect)->state == ClusterOrderState::filled,
            "the order is neither filled nor swept");
    const auto& metrics = gateway.loop.metrics();
    require(metrics.orders_sent_buy == 1 && metrics.fills_buy == 1 && authority(metrics, GatewayAuthorityResult::granted) == 1,
            "metrics disagree with one send, one fill, one proof");
    require(!gateway.loop.new_risk_stopped() && metrics.unknown_outcomes == 0, "a clean run stopped new risk");
    require(metrics.position_count == 1 && metrics.positions[0].lots == kOrderLots, "position metric");
    const auto* producer = cursor(cluster.state_of(leader), kGateway);
    require(producer && producer->gateway_results_acknowledged_through.value() >= 2,
            "the gateway never acknowledged its committed evidence");
    require(metrics.stages[static_cast<std::size_t>(GatewayStage::fill_to_committed)].count == 1 &&
                metrics.stages[static_cast<std::size_t>(GatewayStage::proof_to_sent)].count == 1,
            "stage histograms did not record the one order");
}

Bytes gateway_request(Caller& caller, std::int32_t node, GatewayRequestKind kind, std::uint64_t id) {
    std::array<std::byte, kGatewayAuthorityRequestBytes> body{};
    const auto encoded = encode_gateway_request({kind, process_incarnation(1), id, {}}, body);
    require(encoded.error == NodeProtocolError::none, "fixture gateway request");
    return caller.seal(node, std::span(body).first(encoded.bytes_written), caller_seed(caller.key_id));
}

std::optional<GatewayRefusal> await_refusal(Cluster& cluster, Caller& caller) {
    for (int round = 0; round < 2000; ++round) {
        cluster.run(1);
        auto& queue = cluster.network.responses[caller.key_id];
        while (!queue.empty()) {
            auto frame = std::move(queue.front());
            queue.pop_front();
            const auto opened = caller.open(frame);
            GatewayRefusal refusal;
            if (opened && decode_gateway_refusal(opened->second, refusal) == NodeProtocolError::none) return refusal;
        }
    }
    return std::nullopt;
}

void an_idle_gateway_proposes_nothing_after_acknowledging_its_evidence() {
    Base base("v2-gw-idle");
    Cluster cluster(base.path, {1, 2, 3});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto leader = cluster.elect();
    Gateway gateway(cluster);
    start(cluster, gateway);
    (void)authorize(cluster, strategy);
    const auto evidence_through = [&] { return gateway.loop.next_sequence() - 1; };
    cluster.run_until([&] {
        const auto* risk = account(cluster.state_of(leader));
        return risk && risk->position_quantity.atoms() == kOrderLots && gateway.loop.evidence().obligation_count == 0;
    }, "the fill to commit");
    const auto last_evidence = evidence_through();
    cluster.run_until([&] {
        const auto* producer = cursor(cluster.state_of(leader), kGateway);
        return producer && producer->gateway_results_acknowledged_through.value() >= last_evidence;
    }, "the acknowledgement of the last evidence command");

    const auto sequences_used = gateway.loop.next_sequence();
    const auto state_version = cluster.state_of(leader).state_version;
    cluster.run(3000);
    require(gateway.loop.next_sequence() == sequences_used,
            "an idle gateway used " + std::to_string(gateway.loop.next_sequence() - sequences_used) +
                " request sequences: it is acknowledging its own acknowledgements");
    require(cluster.state_of(leader).state_version == state_version, "an idle gateway committed commands");
    for (const auto id : cluster.voters) {
        const auto replica = cluster.node(id).status().replica;
        const auto& state = cluster.state_of(id);
        require(replica.receipts == state.receipt_count && replica.pinned_receipts == pinned_receipt_count(state),
                "voter " + std::to_string(id) + " reports receipt counts that are not its state's");
        require(replica.pinned_receipts_peak >= 2 && replica.pinned_receipts_peak >= replica.pinned_receipts,
                "voter " + std::to_string(id) + " did not record the authorization and its fill as pinned at once");
    }
}

void node_refuses_wrong_caller_follower_and_unconfigured_gateway() {
    {
        Base base("v2-gw-refuse");
        Cluster cluster(base.path, {1, 2});
        const auto leader = cluster.elect();
        const auto follower = leader == 1 ? 2 : 1;
        Caller op{kOperatorKey, kOperator};
        cluster.network.at(leader).ingress_inbox.push_back(gateway_request(op, leader, GatewayRequestKind::state_read, 1));
        const auto wrong = await_refusal(cluster, op);
        require(wrong && wrong->reason == static_cast<std::uint8_t>(IngressRefusal::not_gateway_caller),
                "the operator caller was served a gateway request");
        require(cluster.network.responses[kOperatorKey].empty(), "a refused request also produced a state chunk");

        Caller gw{kGatewayKey, kGateway};
        cluster.network.at(follower).ingress_inbox.push_back(gateway_request(gw, follower, GatewayRequestKind::state_read, 2));
        const auto redirect = await_refusal(cluster, gw);
        require(redirect && redirect->reason == static_cast<std::uint8_t>(ProposeStatus::not_leader) &&
                    redirect->leader_id == leader, "a follower served state or did not name the leader");

        std::array<std::byte, 27> body{};
        require(encode_gateway_request({GatewayRequestKind::state_read, process_incarnation(1), 3, {}}, body).error ==
                    NodeProtocolError::none, "fixture");
        cluster.network.at(leader).ingress_inbox.push_back(gw.seal(leader, body, caller_seed(kGatewayKey)));
        const auto malformed = await_refusal(cluster, gw);
        require(malformed && malformed->reason == static_cast<std::uint8_t>(IngressRefusal::malformed_command),
                "a malformed gateway request was not refused as malformed");

        GatewayClient client;
        client.network = &cluster.network;
        GatewayLink link(1, process_incarnation(0x50), gateway_routes(cluster.voters, caller_seed(kGatewayKey)), client);
        cluster.run(50);
        require(link.request_state(steady_ms(), 3000), "state request refused locally");
        std::optional<LinkOutcome> outcome;
        cluster.run_until([&] {
            auto result = link.pump(steady_ms());
            if (result.status != LinkStatus::pending) outcome = std::move(result);
            return outcome.has_value();
        }, "a state read");
        const auto status = cluster.node(leader).status().replica;
        require(outcome->status == LinkStatus::done && outcome->reading.responder_id == leader &&
                    outcome->reading.term == status.term && outcome->reading.applied_index == status.applied_index,
                "state read coordinates differ from the leader's");
        require(outcome->reading.state_digest == status.state_digest && link.state().state_version == status.state_version,
                "reassembled state differs from the leader's applied state");
        require(outcome->reading.authority_domain == [&] {
                    const auto identity = config_for(base.path, leader, NodeStartup::recover, cluster.voters, true)
                                              .static_identity();
                    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
                    const auto encoded = encode_static_identity(identity, bytes);
                    VerifiedStaticIdentity verified;
                    require(VerifiedStaticIdentity::decode(std::span(bytes).first(encoded.bytes_written), verified) ==
                                StaticIdentityCodecError::none, "fixture identity");
                    return verified.authority_domain();
                }(), "state read reported a different authority domain than the cluster identity");
    }
    {
        Base base("v2-gw-unconfigured");
        Cluster cluster(base.path, {1}, false);
        const auto leader = cluster.elect();
        Caller gw{kGatewayKey, kGateway};
        cluster.network.at(leader).ingress_inbox.push_back(gateway_request(gw, leader, GatewayRequestKind::state_read, 1));
        const auto refused = await_refusal(cluster, gw);
        require(refused && refused->reason == static_cast<std::uint8_t>(IngressRefusal::not_gateway_caller),
                "a node without gateway.caller served the gateway");
    }
}

void leader_change_during_authority_request_sends_nothing() {
    Base base("v2-gw-leader");
    Cluster cluster(base.path, {1, 2, 3});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto old_leader = cluster.elect();
    Gateway gateway(cluster);
    start(cluster, gateway);
    bool stop_requested = false, stopped = false;
    cluster.network.gateway_ingress = [&](std::int32_t node, std::span<const std::byte> envelope) {
        if (body_kind(envelope) != static_cast<std::uint8_t>(GatewayRequestKind::authority) || node != old_leader)
            return true;
        stop_requested = true;
        return false;
    };
    cluster.on_round = [&] {
        gateway.loop.step();
        if (stop_requested && !stopped) {
            stopped = true;
            cluster.stop(old_leader);
        }
    };
    (void)authorize(cluster, strategy);
    cluster.run_until([&] { return stopped && cluster.leader() != 0 && cluster.leader() != old_leader; },
                      "a new leader after the old one died mid-request");
    cluster.run(8000);
    require(gateway.counting.sends.empty(), "the gateway sent without a proof current for the state it holds");
    require(gateway.loop.new_risk_stopped(), "a leader change during an authority request left new risk enabled");
}

void lost_and_duplicated_responses_still_deliver_exactly_once() {
    for (const int mode : {0, 1, 2}) {
        const bool duplicate = mode == 1;
        Base base(mode == 0 ? "v2-gw-lost" : mode == 1 ? "v2-gw-dup" : "v2-gw-resend");
        Cluster cluster(base.path, {1, 2});
        Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
        prepare_cluster(cluster, op, strategy);
        const auto leader = cluster.elect();
        GatewayLinkTiming timing;
        if (mode == 2) timing.resend_gateway = 1;
        Gateway gateway(cluster, sim::FaultPolicy::clean(), Gateway::config(), timing);
        start(cluster, gateway);
        int grants_dropped = 0;
        if (duplicate) {
            cluster.network.duplicate_gateway_responses = true;
        } else if (mode == 0) {
            cluster.network.gateway_response = [&](std::span<const std::byte> envelope) {
                if (body_kind(envelope) != static_cast<std::uint8_t>(ResponseKind::authority_granted) || grants_dropped)
                    return true;
                ++grants_dropped;
                return false;
            };
        }
        (void)authorize(cluster, strategy);
        cluster.run_until([&] {
            const auto* risk = account(cluster.state_of(leader));
            return risk && risk->position_quantity.atoms() == kOrderLots && gateway.loop.evidence().obligation_count == 0;
        }, duplicate ? "delivery with duplicated responses" : "delivery after a lost proof");
        cluster.run(3000);
        require(gateway.counting.sends.size() == 1, std::string(duplicate ? "duplicated" : "lost") +
                                                        " responses produced " +
                                                        std::to_string(gateway.counting.sends.size()) + " sends");
        require(!duplicate || gateway.link.counters().fenced > 0, "duplicated frames were not refused by the fence");
        require(mode != 0 || grants_dropped == 1, "the lost-response fault never fired");
        require(duplicate || authority(gateway.loop.metrics(), GatewayAuthorityResult::granted) == 1,
                "the proof was not obtained by exactly one granted request");
        require(mode != 2 || authority(gateway.loop.metrics(), GatewayAuthorityResult::refused) == 0,
                "resending a pending barrier made the node refuse or abandon it");
    }
}

LinkOutcome settle(Cluster& cluster, GatewayLink& link) {
    std::optional<LinkOutcome> outcome;
    cluster.run_until([&] {
        auto result = link.pump(steady_ms());
        if (result.status != LinkStatus::pending) outcome = std::move(result);
        return outcome.has_value();
    }, "a gateway request to settle");
    return std::move(*outcome);
}

Bytes seal_response(std::uint8_t seed, std::int32_t node, std::uint64_t sequence, std::span<const std::byte> body, std::uint64_t epoch = 1) {
    Bytes out(kMaxAuthEnvelopeBytes);
    const auto env = encode_auth_envelope(key(seed, AuthRole::response, node, kGatewayKey), epoch, sequence, body, out);
    require(env.error == AuthEnvelopeError::none, "fixture response envelope");
    out.resize(env.bytes_written);
    return out;
}

GatewayControlView control_of(const TradingState& state, Tick now) {
    return {state.executor_generation, state.kill_epoch, state.strategy_mode, now, 2000, now};
}

void forged_replayed_mismatched_and_stale_proofs_are_refused() {
    Base base("v2-gw-proof");
    Cluster cluster(base.path, {1});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto effect = authorize(cluster, strategy);
    const auto leader = cluster.elect();
    GatewayClient client;
    client.network = &cluster.network;
    const auto incarnation = process_incarnation(0x60);
    GatewayLink link(1, incarnation, gateway_routes(cluster.voters, caller_seed(kGatewayKey)), client);
    require(link.request_state(steady_ms(), 3000), "state");
    const auto reading = settle(cluster, link).reading;
    GatewayAuthority authority(incarnation, reading.authority_domain, 60'000);
    const auto start = steady_ms();
    authority.observe_fence(control_of(link.state(), start), reading.term);
    authority.activate_after_reconciliation();
    const auto binding = materialize_bound_order_effect(link.state(), effect, catalog_test::standard_catalog());
    require(binding.has_value(), "fixture effect did not materialize");
    const auto barrier = authority.begin_request(*binding, control_of(link.state(), start), start);
    require(barrier.has_value(), "begin_request refused a healthy fixture");

    cluster.network.gateway_tap.clear();
    require(link.request_authority(*barrier, steady_ms(), 5000), "authority request");
    auto granted = settle(cluster, link);
    Bytes genuine;
    for (const auto& frame : cluster.network.gateway_tap)
        if (body_kind(frame) == static_cast<std::uint8_t>(ResponseKind::authority_granted)) genuine = frame;
    require(!genuine.empty(), "no grant frame on the wire");
    const auto body = std::span<const std::byte>(genuine).subspan(kAuthHeaderBytes, kResponseAuthorityGrantedBytes);

    require(link.request_authority(*barrier, steady_ms(), 400), "second authority request");
    const auto pending_body = [&] {
        std::array<std::byte, kGatewayAuthorityRequestBytes> bytes{};
        (void)encode_gateway_request({GatewayRequestKind::authority, incarnation, 3, *barrier}, bytes);
        return bytes;
    }();
    const auto pending_correlation = gateway_correlation(pending_body);
    cluster.network.gateway_ingress = [](std::int32_t, std::span<const std::byte>) { return false; };
    const auto before = link.counters();
    auto& inbox = cluster.network.responses[kGatewayKey];
    inbox.push_back(genuine);
    ResponseCorrelation ignored;
    AuthorityProofView view;
    require(decode_authority_granted(body, ignored, view) == NodeProtocolError::none, "captured grant decodes");
    std::array<std::byte, kResponseAuthorityGrantedBytes> forged{};
    require(encode_authority_granted(pending_correlation, view, forged).error == NodeProtocolError::none, "forge");
    inbox.push_back(seal_response(kWrongSeed, leader, 1'000'000, forged));
    auto other = view;
    other.request.request_sequence += 1;
    std::array<std::byte, kResponseAuthorityGrantedBytes> mismatched{};
    require(encode_authority_granted(pending_correlation, other, mismatched).error == NodeProtocolError::none, "forge 2");
    inbox.push_back(seal_response(caller_seed(kGatewayKey), leader, 1'000'001, mismatched));
    std::array<std::byte, kResponseAuthorityGrantedBytes> uncorrelated{};
    require(encode_authority_granted({pending_correlation.request_sequence, digest(0x99)}, view, uncorrelated).error ==
                NodeProtocolError::none, "forge 3");
    inbox.push_back(seal_response(caller_seed(kGatewayKey), leader, 1'000'002, uncorrelated));
    const auto outcome = settle(cluster, link);
    const auto after = link.counters();
    require(outcome.status == LinkStatus::timed_out && !outcome.proof, "a forged, replayed or mismatched grant became a proof");
    require(after.fenced == before.fenced + 1, "the replayed genuine grant was not refused by the fence");
    require(after.unauthentic == before.unauthentic + 1, "the wrong-key grant was not refused as unauthentic");
    require(after.proofs_refused == before.proofs_refused + 1, "the mismatched-barrier grant was not refused");
    require(after.uncorrelated >= before.uncorrelated + 1, "the uncorrelated grant was not refused");
    cluster.network.gateway_ingress = {};

    require(cluster.commit(op, AdvanceKillEpoch{KillEpoch{2}}, ProposeLane::control).code == ApplyCode::applied,
            "kill epoch advance");
    GatewayLink fresh(2, process_incarnation(0x70), gateway_routes(cluster.voters, caller_seed(kGatewayKey)), client);
    require(fresh.request_state(steady_ms(), 3000), "state after kill");
    const auto after_kill = settle(cluster, fresh);
    require(after_kill.status == LinkStatus::done && fresh.state().kill_epoch == KillEpoch{2},
            "state read did not observe the advanced kill epoch");
    authority.observe_fence(control_of(fresh.state(), steady_ms()), after_kill.reading.term);
    require(!authority.accept(*granted.proof, steady_ms()), "a proof issued under kill epoch 1 was accepted after epoch 2");
    struct Sink {
        int calls{};
        static Tick clock(void*) noexcept { return steady_ms(); }
        static void handoff(BoundOrderEffect, void* self) { ++static_cast<Sink*>(self)->calls; }
    } sink;
    GatewayDeliveryState delivery{};
    delivery.started = true;
    SendQuotaView quota{5, 2};
    GatewayEvidenceState evidence{};
    require(!dispatch_gateway_send(authority, fresh.state(), *binding, control_of(fresh.state(), steady_ms()),
                                   after_kill.reading.term, {VenueId{9}, VenueCapabilitiesVersion{8}, true, true}, quota,
                                   delivery, evidence, {}, catalog_test::standard_catalog(),
                                   {&Sink::clock, &Sink::handoff, &sink}) &&
                sink.calls == 0, "a send reached the venue after the kill epoch advanced");
}

void unknown_outcome_stops_new_risk_and_is_committed() {
    Base base("v2-gw-unknown");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto leader = cluster.elect();
    sim::FaultPolicy lossy;
    lossy.loss_permille = 1000;
    Gateway gateway(cluster, lossy);
    start(cluster, gateway);
    const auto effect = authorize(cluster, strategy);
    cluster.run_until([&] {
        const auto* order = order_of(cluster.state_of(leader), effect);
        return order && order->state != ClusterOrderState::dispatch_authorized;
    }, "the unknown outcome to commit");
    cluster.run(3000);
    require(gateway.counting.sends.size() == 1, "an unanswered submit was resent without proof it was unsent");
    require(gateway.loop.new_risk_stopped(), "an unknown outcome left new risk enabled");
    require(gateway.loop.metrics().unknown_outcomes == 1, "the unknown outcome was not counted once");
    const auto* order = order_of(cluster.state_of(leader), effect);
    require(order && order->state != ClusterOrderState::dispatch_authorized &&
                order->state != ClusterOrderState::acknowledged,
            "the kernel never recorded the effect as unknown");
}

void gateway_started_with_an_unsettled_effect_never_sends_it() {
    Base base("v2-gw-restart");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto effect = authorize(cluster, strategy);
    Gateway gateway(cluster);
    cluster.on_round = [&] { gateway.loop.step(); };
    cluster.run_until([&] { return gateway.loop.phase() == GatewayPhase::running; }, "the gateway's first state read");
    require(gateway.loop.new_risk_stopped(), "a gateway that found an unsettled effect at startup enabled new risk");
    cluster.run(6000);
    require(gateway.counting.sends.empty(), "a possibly-sent effect found at startup was sent again");
    const auto* order = order_of(cluster.state_of(cluster.elect()), effect);
    require(!order || order->state != ClusterOrderState::dispatch_authorized,
            "the startup suspect was never declared unknown to the cluster");
}

void effect_for_another_catalog_row_is_not_sent() {
    Base base("v2-gw-row");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    auto config = Gateway::config();
    config.account = AccountId{6};
    Gateway gateway(cluster, sim::FaultPolicy::clean(), config);
    start(cluster, gateway);
    (void)authorize(cluster, strategy);
    cluster.run(4000);
    require(gateway.counting.sends.empty(), "a gateway sent an effect bound to a catalog row it does not serve");
    require(authority(gateway.loop.metrics(), GatewayAuthorityResult::granted) == 0,
            "a gateway requested authority for another row's effect");
}

void paper_venue_fill_books_position_and_measured_fee() {
    g_paper = true;
    struct Reset { ~Reset() { g_paper = false; } } reset;
    Base base("v2-gw-paper");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto leader = cluster.elect();
    Gateway gateway(cluster);
    start(cluster, gateway);
    const auto effect = authorize(cluster, strategy);
    cluster.run_until([&] {
        const auto* risk = account(cluster.state_of(leader));
        return risk && risk->position_quantity.atoms() == kOrderLots && gateway.loop.evidence().obligation_count == 0;
    }, "the paper fill to commit");
    cluster.run(1500);
    require(gateway.counting.sends.size() == 1 && gateway.counting.sends[0].effect_id == effect &&
                gateway.counting.sends[0].time_in_force == TimeInForce::fill_or_kill,
            "the paper venue did not receive the one fill-or-kill effect once");
    const auto& metrics = gateway.loop.metrics();
    require(metrics.fee_micros_total == 26, "fees " + std::to_string(metrics.fee_micros_total) + " micros, expected 26");
    require(gateway.paper_venue->balances().fees_paid.atoms() == 26, "the venue and the gateway disagree on fees");
    require(metrics.fills_buy == 1 && !gateway.loop.new_risk_stopped(), "paper fill metrics");
    require(metrics.volume_micros_buy == 100 * 11 * kOrderLots && metrics.volume_micros_sell == 0,
            "volume " + std::to_string(metrics.volume_micros_buy) + "/" + std::to_string(metrics.volume_micros_sell) +
                " micros, expected 2200 bought and 0 sold");
    require(metrics.last_fill_timestamp_seconds > 0, "last fill timestamp not set");
}

struct PaperFixture {
    PaperFixture() { g_paper = true; }
    ~PaperFixture() { g_paper = false; }
};

GatewayLoopConfig quiet_config(Tick quiet) {
    auto value = Gateway::config();
    value.rearm_quiet = quiet;
    return value;
}

void fence_rearms_after_the_quiet_period_and_then_delivers_once() {
    PaperFixture paper;
    Base base("v2-gw-rearm");
    Cluster cluster(base.path, {1, 2, 3});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    constexpr Tick kQuiet = 1500;
    Gateway gateway(cluster, sim::FaultPolicy::clean(), quiet_config(kQuiet));
    start(cluster, gateway);
    const auto opening = drain(gateway.loop);
    require(opening.size() == 1 && opening[0].kind == GatewayEventKind::armed,
            "the gateway did not log one startup arm");

    std::int64_t last_term = highest_term(cluster);
    Tick last_term_change = 0, rearmed_at = 0;
    cluster.on_round = [&] {
        gateway.loop.step();
        if (const auto term = highest_term(cluster); term != last_term) {
            last_term = term;
            last_term_change = steady_ms();
        }
        if (rearmed_at == 0 && stops(gateway.loop.metrics(), GatewayStopCause::fence) != 0 &&
            !gateway.loop.new_risk_stopped())
            rearmed_at = steady_ms();
    };
    cluster.stop(cluster.elect());
    cluster.run_until([&] { return stops(gateway.loop.metrics(), GatewayStopCause::fence) != 0; },
                      "the election to stop new risk");
    require(stops(gateway.loop.metrics(), GatewayStopCause::fence) == 1, "the election was not counted as a fence");
    const auto effect = authorize(cluster, strategy);
    cluster.run_until([&] { return rearmed_at != 0; }, "the automatic re-arm", 60'000);

    require(rearmed_at >= last_term_change + kQuiet,
            "re-armed " + std::to_string(rearmed_at - last_term_change) + " ms after the last term change, "
            "inside the " + std::to_string(kQuiet) + " ms quiet period");
    const auto leader = cluster.elect();
    cluster.run_until([&] {
        const auto* risk = account(cluster.state_of(leader));
        return risk && risk->position_quantity.atoms() == kOrderLots && gateway.loop.evidence().obligation_count == 0;
    }, "the order authorized during the fence to fill after the re-arm");
    cluster.run(1500);
    require(gateway.counting.sends.size() == 1 && gateway.counting.sends[0].effect_id == effect,
            "the effect was not sent exactly once after the re-arm");
    const auto& metrics = gateway.loop.metrics();
    require(metrics.rearms == 1 && stops_other_than(metrics, GatewayStopCause::fence) == 0,
            "expected one re-arm and no stop other than the fence");
    const auto events = drain(gateway.loop);
    bool stopped = false, rearmed = false;
    for (const auto& event : events) {
        stopped |= event.kind == GatewayEventKind::stopped && event.cause == GatewayStopCause::fence;
        rearmed |= event.kind == GatewayEventKind::rearmed && stopped;
    }
    require(stopped && rearmed, "the fence stop and the re-arm were not logged in order");
}

void a_timed_out_state_read_is_a_fence_that_rearms() {
    PaperFixture paper;
    Base base("v2-gw-gap");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto term = highest_term(cluster);
    Gateway gateway(cluster, sim::FaultPolicy::clean(), quiet_config(300));
    start(cluster, gateway);
    cluster.network.gateway_ingress = [](std::int32_t, std::span<const std::byte>) { return false; };
    cluster.run_until([&] { return gateway.loop.new_risk_stopped(); }, "a state-read gap to stop new risk");
    cluster.network.gateway_ingress = {};
    cluster.run_until([&] { return !gateway.loop.new_risk_stopped(); }, "the re-arm after the gap", 60'000);
    const auto& metrics = gateway.loop.metrics();
    require(highest_term(cluster) == term, "an election happened; this test is about a read gap alone");
    require(stops(metrics, GatewayStopCause::fence) == 1 && stops_other_than(metrics, GatewayStopCause::fence) == 0 &&
                metrics.rearms == 1,
            "the read gap was not one fence stop followed by one re-arm");
}

void a_control_change_latches_until_an_operator_restart() {
    PaperFixture paper;
    Base base("v2-gw-control");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    Gateway gateway(cluster, sim::FaultPolicy::clean(), quiet_config(200));
    start(cluster, gateway);
    require(cluster.commit(op, SetStrategyMode{StrategyMode::no_new_risk, KillEpoch{1}, ExecutorGeneration{1}},
                           ProposeLane::control).code == ApplyCode::applied, "mode no_new_risk");
    cluster.run_until([&] { return gateway.loop.new_risk_stopped(); }, "the mode change to stop new risk");
    require(cluster.commit(op, SetStrategyMode{StrategyMode::normal, KillEpoch{1}, ExecutorGeneration{1}},
                           ProposeLane::control).code == ApplyCode::applied, "mode normal");
    cluster.run(4000);
    const auto& metrics = gateway.loop.metrics();
    require(gateway.loop.new_risk_stopped(), "a strategy-mode change re-armed without an operator restart");
    require(stops(metrics, GatewayStopCause::control_change) == 1 && metrics.rearms == 0 &&
                metrics.stop_active[static_cast<std::size_t>(GatewayStopCause::control_change)],
            "the control change is not the recorded, active cause");

    Gateway restarted(cluster, sim::FaultPolicy::clean(), quiet_config(200));
    start(cluster, restarted);
}

void flatten_rearms_by_itself_and_sells_exactly_the_position() {
    PaperFixture paper;
    g_exit_template = true;
    struct Reset { ~Reset() { g_exit_template = false; } } reset;
    Base base("v2-gw-flatten");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    constexpr Tick kQuiet = 1000;
    Gateway gateway(cluster, sim::FaultPolicy::clean(), quiet_config(kQuiet));
    start(cluster, gateway);
    Tick stopped_at = 0, rearmed_at = 0;
    cluster.on_round = [&] {
        gateway.loop.step();
        const bool seen = stops(gateway.loop.metrics(), GatewayStopCause::reducing_mode) != 0;
        if (stopped_at == 0 && seen) stopped_at = steady_ms();
        if (rearmed_at == 0 && seen && !gateway.loop.new_risk_stopped()) rearmed_at = steady_ms();
    };
    const auto leader = cluster.elect();
    const auto held = [&] {
        const auto* risk = account(cluster.state_of(leader));
        return risk ? risk->position_quantity.atoms() : -1;
    };
    (void)authorize(cluster, strategy);
    cluster.run_until([&] { return held() == kOrderLots && gateway.loop.evidence().obligation_count == 0; },
                      "the entry to fill");

    require(cluster.commit(op, SetStrategyMode{StrategyMode::flatten, KillEpoch{1}, ExecutorGeneration{1}},
                           ProposeLane::control).code == ApplyCode::applied, "mode flatten");
    cluster.run_until([&] { return stops(gateway.loop.metrics(), GatewayStopCause::reducing_mode) == 1; },
                      "the gateway to see the flatten mode");
    require(gateway.loop.new_risk_stopped(), "the gateway kept its old permission across the mode change");
    require(cluster.commit(strategy, RecordSourceEvent{SourceId{12}, SourceGeneration{1}, SourceSequence{2},
                                                       digest(0x45)}).code == ApplyCode::applied, "second bar");
    auto frame = decision_frame();
    frame.source_sequence = SourceSequence{2};
    frame.raw_evidence_digest = digest(0x45);
    frame.event_time_ns = 160;
    frame.receive_time_ns = 161;
    frame.features[0] = FeatureMicros::from_atoms(5);
    frame.position_version = account(cluster.state_of(leader))->position_version;
    const auto decided =
        cluster.commit(strategy, EvaluateDecisionFrame{DecisionId{21}, EvaluationMode::authoritative, frame});
    require(decided.decision_code == DecisionCode::action && decided.effective_directive == Directive::place_limit,
            "flatten did not decide an order on a frame whose rule does not match");
    const auto exit = cluster.commit(strategy, AuthorizeDecision{DecisionId{21}});
    require(exit.code == ApplyCode::applied, "the flatten order was not authorized");
    cluster.run_until([&] { return held() == 0 && gateway.loop.evidence().obligation_count == 0; },
                      "the flatten order to fill", 60'000);
    cluster.run(1500);

    require(gateway.counting.sends.size() == 2 && gateway.counting.sends[1].effect_id == exit.effect_id() &&
                gateway.counting.sends[1].side == Side::sell &&
                gateway.counting.sends[1].quantity == QuantityLots::from_atoms(kOrderLots) &&
                gateway.counting.sends[1].price == PriceTicks::from_atoms(5),
            "the venue did not receive one sell of the whole position at the frame price");
    const auto& metrics = gateway.loop.metrics();
    require(metrics.rearms == 1 && stops(metrics, GatewayStopCause::reducing_mode) == 1 &&
                stops_other_than(metrics, GatewayStopCause::reducing_mode) == 0 && !gateway.loop.new_risk_stopped(),
            "the gateway did not re-arm once by itself, or latched another stop");
    require(metrics.fills_sell == 1, "the flatten fill was not counted");
    require(rearmed_at >= stopped_at + kQuiet - 100,
            "the gateway re-armed " + std::to_string(rearmed_at - stopped_at) + " ms after the mode change, inside the " +
                std::to_string(kQuiet) + " ms quiet period");

    require(cluster.commit(op, SetStrategyMode{StrategyMode::normal, KillEpoch{1}, ExecutorGeneration{1}},
                           ProposeLane::control).code == ApplyCode::applied, "mode normal");
    cluster.run_until([&] { return stops(gateway.loop.metrics(), GatewayStopCause::control_change) == 1; },
                      "the return to normal to latch");
    cluster.run(3 * kQuiet);
    require(gateway.loop.new_risk_stopped(), "leaving flatten re-armed new risk without an operator restart");
}

void a_reducing_mode_that_arrives_with_another_control_change_latches() {
    PaperFixture paper;
    for (const bool kill : {false, true}) {
        Base base(kill ? "v2-gw-flatten-kill" : "v2-gw-flatten-generation");
        Cluster cluster(base.path, {1, 2});
        Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
        prepare_cluster(cluster, op, strategy);
        constexpr Tick kQuiet = 200;
        Gateway gateway(cluster, sim::FaultPolicy::clean(), quiet_config(kQuiet));
        start(cluster, gateway);
        cluster.on_round = {};
        const KillEpoch epoch{kill ? 2U : 1U};
        const ExecutorGeneration generation{kill ? 1U : 2U};
        require((kill ? cluster.commit(op, AdvanceKillEpoch{epoch}, ProposeLane::control)
                      : cluster.commit(op, SetExecutorGeneration{generation}, ProposeLane::control)).code ==
                    ApplyCode::applied, "control change");
        require(cluster.commit(op, SetStrategyMode{StrategyMode::flatten, epoch, generation},
                               ProposeLane::control).code == ApplyCode::applied, "mode flatten");
        cluster.on_round = [&] { gateway.loop.step(); };
        cluster.run_until([&] { return stops(gateway.loop.metrics(), GatewayStopCause::control_change) == 1; },
                          "the combined change to stop new risk");
        cluster.run(4 * kQuiet);
        const auto& metrics = gateway.loop.metrics();
        require(gateway.loop.new_risk_stopped() && metrics.rearms == 0 &&
                    stops(metrics, GatewayStopCause::reducing_mode) == 0,
                std::string("a flatten that came with a new ") + (kill ? "kill epoch" : "executor generation") +
                    " re-armed without an operator restart");
    }
}

void a_caller_outside_the_operator_band_is_paced_at_ingress() {
    g_ingress_rate = 1;
    g_ingress_burst = 3;
    struct Reset { ~Reset() { g_ingress_rate = g_ingress_burst = 0; } } reset;
    Base base("v2-gw-ingress-rate");
    Cluster cluster(base.path, {1, 2}, false);
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto leader = cluster.elect();
    const auto limited = [&] { return cluster.node(leader).status().counters.ingress_rate_limited; };
    require(limited() == 0, "the operator's setup burst or one strategy command was rate limited");

    const auto wait_ms = [&](const Tick duration) {
        const auto until = steady_ms() + duration;
        while (steady_ms() < until) cluster.run(20);
    };
    const auto burst = [&](Caller& caller, const std::uint64_t first, const int count) {
        cluster.network.responses[caller.key_id].clear();
        for (int index = 0; index < count; ++index) {
            const Command command{{CallerId{caller.caller_id}, RequestSequence{first + static_cast<std::uint64_t>(index)},
                                   digest(first + static_cast<std::uint64_t>(index))},
                                  RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                                    SourceSequence{first + static_cast<std::uint64_t>(index)},
                                                    digest(0x45)}};
            cluster.network.at(leader).ingress_inbox.push_back(caller.ingress(leader, command, ProposeLane::normal));
        }
        int admitted = 0, busy = 0;
        const auto until = steady_ms() + 5'000;
        while (steady_ms() < until && admitted + busy < count) {
            cluster.run(5);
            auto& queue = cluster.network.responses[caller.key_id];
            while (!queue.empty()) {
                auto frame = std::move(queue.front());
                queue.pop_front();
                const auto opened = caller.open(frame);
                NodeResponse response;
                if (!opened || decode_response(opened->second, response) != NodeProtocolError::none) continue;
                admitted += response.kind == ResponseKind::admitted ? 1 : 0;
                busy += response.kind == ResponseKind::refused &&
                                response.reason == static_cast<std::uint8_t>(IngressRefusal::busy) ? 1 : 0;
            }
        }
        return std::pair{admitted, busy};
    };
    wait_ms(6'000);
    const auto first = burst(strategy, 100, 5);
    require(first == std::pair{3, 2} && limited() == 2,
            "five commands at once against a burst of three: admitted " + std::to_string(first.first) +
                ", refused busy " + std::to_string(first.second) + ", counted " + std::to_string(limited()));
    wait_ms(2'500);
    const auto second = burst(strategy, 200, 8);
    require(second.first >= 1 && second.first <= 3 && second.first + second.second == 8,
            "eight more after a pause, at one a second with a burst of three: admitted " + std::to_string(second.first) + ", refused busy " +
                std::to_string(second.second));
    const auto before = limited();
    const auto operator_burst = burst(op, 300, 6);
    require(operator_burst.second == 0 && limited() == before,
            "an operator caller was rate limited: refused busy " + std::to_string(operator_burst.second));
}

struct SoakResult {
    int completed{};
    std::size_t venue_queries{};
    std::uint64_t deferred_state_reads{};
};

SoakResult round_trips_until_a_stop(const int round_trips, const std::vector<std::int32_t>& voters = {1, 2},
                                    const int leader_disk_ms = 0) {
    PaperFixture paper;
    g_exit_template = true;
    struct Reset { ~Reset() { g_exit_template = false; } } reset;
    Base base("v2-gw-soak");
    Cluster cluster(base.path, voters);
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    Gateway gateway(cluster);
    start(cluster, gateway);
    const auto leader = cluster.elect();
    g_slow_disk_ms = leader_disk_ms;
    g_slow_disk_voter = leader_disk_ms == 0 ? 0 : leader;
    const auto outcome = [&](const int completed) {
        return SoakResult{completed, gateway.counting.queries,
                          cluster.node(leader).status().counters.gateway_state_reads_deferred};
    };
    std::uint64_t source = 1, decision = 100;
    const auto order = [&](const std::int64_t price, const std::int64_t expected_lots) {
        const auto* risk = account(cluster.state_of(leader));
        require(cluster.commit(strategy, RecordSourceEvent{SourceId{12}, SourceGeneration{1}, SourceSequence{++source},
                                                           digest(source)}).code == ApplyCode::applied, "soak bar");
        auto frame = decision_frame();
        frame.source_sequence = SourceSequence{source};
        frame.raw_evidence_digest = digest(source);
        frame.features[0] = FeatureMicros::from_atoms(price);
        frame.position_version = risk->position_version;
        frame.risk_version = risk->risk_version;
        const auto decided =
            cluster.commit(strategy, EvaluateDecisionFrame{DecisionId{++decision}, EvaluationMode::authoritative, frame});
        require(decided.decision_code == DecisionCode::action, "soak decision was not an action");
        require(cluster.commit(strategy, AuthorizeDecision{DecisionId{decision}}).code == ApplyCode::applied,
                "soak authorization refused");
        for (int round = 0; round < 40'000; ++round) {
            cluster.run(1);
            if (gateway.loop.new_risk_stopped()) return false;
            const auto* now = account(cluster.state_of(leader));
            if (now->position_quantity.atoms() == expected_lots && now->reserved_quantity.atoms() == 0) return true;
        }
        throw std::runtime_error("soak order did not fill");
    };
    for (int trip = 0; trip < round_trips; ++trip) {
        if (!order(11, kOrderLots) || !order(2, 0)) return outcome(trip);
        cluster.run(40);
        if (gateway.loop.new_risk_stopped()) return outcome(trip);
    }
    require(cluster.leader() == leader, "the soak changed leader, so it no longer tests one leader's disk");
    return outcome(round_trips);
}

void many_round_trips_never_stop_the_gateway() {
    const char* requested = std::getenv("VELOCITA_SOAK_ROUND_TRIPS");
    const int round_trips = requested ? std::atoi(requested) : 40;
    const auto soak = round_trips_until_a_stop(round_trips);
    require(soak.completed == round_trips, "the gateway stopped new risk after " + std::to_string(soak.completed) +
                                               " clean round trips out of " + std::to_string(round_trips));
}

void a_leader_with_a_slow_disk_never_makes_the_gateway_question_a_filled_order() {
    const int round_trips = 3;
    const auto soak = round_trips_until_a_stop(round_trips, {1, 2, 3}, 150);
    require(soak.completed == round_trips, "the gateway stopped new risk after " + std::to_string(soak.completed) +
                                               " round trips beside a leader with a slow disk");
    require(soak.deferred_state_reads != 0,
            "the leader never held a committed entry it had not applied, so the soak did not test the slow disk");
    require(soak.venue_queries == 0, "the gateway asked the venue about " + std::to_string(soak.venue_queries) +
                                         " orders it had already reported filled");
}

struct StateReplay {
    struct Chunk {
        StateChunkHeader header;
        Bytes payload;
    };
    std::vector<Chunk> latest, old;
    bool substitute{};

    bool pass(Network& network, std::span<const std::byte> envelope) {
        AuthRoute route;
        VerifiedAuthEnvelope verified;
        StateChunkView view;
        if (peek_auth_route(envelope, route) != AuthEnvelopeError::none ||
            verify_auth_envelope(key(caller_seed(kGatewayKey), AuthRole::response, route.sender, kGatewayKey), envelope,
                                 verified) != AuthEnvelopeError::none ||
            decode_state_chunk(verified.body, view) != NodeProtocolError::none)
            return true;
        if (!substitute) {
            if (view.header.chunk_index == 0) latest.clear();
            latest.push_back({view.header, Bytes(view.chunk.begin(), view.chunk.end())});
            return true;
        }
        if (view.header.chunk_index >= old.size()) return false;
        auto header = old[view.header.chunk_index].header;
        header.correlation = view.header.correlation;
        Bytes body(kMaxStateChunkResponseBytes);
        const auto framed = encode_state_chunk(header, old[view.header.chunk_index].payload, body);
        require(framed.error == NodeProtocolError::none, "fixture state chunk did not encode");
        body.resize(framed.bytes_written);
        network.responses[kGatewayKey].push_back(
            seal_response(caller_seed(kGatewayKey), route.sender, verified.header.sequence, body, verified.header.session_epoch));
        return false;
    }
};

void a_state_read_older_than_a_committed_result_is_discarded_as_a_fence() {
    Base base("v2-gw-stale-read");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    StateReplay replay;
    cluster.network.gateway_response = [&](std::span<const std::byte> envelope) {
        return replay.pass(cluster.network, envelope);
    };
    Gateway gateway(cluster, sim::FaultPolicy::clean(), quiet_config(300));
    start(cluster, gateway);
    const auto leader = cluster.elect();
    gateway.counting.hold_fills = true;
    const auto effect = authorize(cluster, strategy);
    cluster.run_until([&] {
        const auto* order = order_of(cluster.state_of(leader), effect);
        return order && order->state == ClusterOrderState::live && gateway.loop.evidence().obligation_count == 0 &&
               !replay.latest.empty() && replay.latest.size() == replay.latest.front().header.chunk_count &&
               replay.latest.front().header.applied_index == cluster.node(leader).status().replica.applied_index;
    }, "a state read that shows the order resting");
    replay.old = replay.latest;

    gateway.counting.hold_fills = false;
    const auto& metrics = gateway.loop.metrics();
    cluster.run_until([&] {
        const auto* risk = account(cluster.state_of(leader));
        const auto* producer = cursor(cluster.state_of(leader), kGateway);
        return risk && risk->position_quantity.atoms() == kOrderLots && gateway.loop.evidence().obligation_count == 0 &&
               metrics.position_count == 1 && metrics.positions[0].lots == kOrderLots && producer &&
               producer->gateway_results_acknowledged_through.value() + 2 >= gateway.loop.next_sequence();
    }, "the fill to commit, be read back and be acknowledged");
    require(metrics.stale_state_reads == 0 && !gateway.loop.new_risk_stopped(), "a current state read was called stale");
    const auto fences = stops(metrics, GatewayStopCause::fence);
    const auto sequences = gateway.loop.next_sequence();
    const auto queries = gateway.counting.queries;

    replay.substitute = true;
    const auto began = steady_ms();
    cluster.run_until([&] { return metrics.stale_state_reads >= 3; }, "three stale state reads");
    require(steady_ms() - began >= 2 * Gateway::config().state_interval,
            "stale state reads were retried without waiting for the state interval");
    require(metrics.positions[0].lots == kOrderLots, "the gateway adopted a state older than its own committed fill");
    require(gateway.loop.new_risk_stopped() && stops(metrics, GatewayStopCause::fence) == fences + 1 &&
                stops_other_than(metrics, GatewayStopCause::fence) == 0,
            "a stale state read was not held as one fence");
    require(gateway.counting.queries == queries && gateway.loop.next_sequence() == sequences,
            "the gateway acted on a stale state read that still shows the filled order resting");

    replay.substitute = false;
    const auto rearms = metrics.rearms;
    const auto current_again = steady_ms();
    cluster.run_until([&] { return !gateway.loop.new_risk_stopped(); }, "re-arming once state reads are current again");
    require(metrics.rearms == rearms + 1 && metrics.positions[0].lots == kOrderLots,
            "the gateway did not re-arm once on a current state read");
    require(steady_ms() - current_again >= 150, "the quiet period was not counted from the last stale state read");
}

void a_refused_report_stops_new_risk_and_says_which_report_and_why() {
    PaperFixture paper;
    Base base("v2-gw-refused-report");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    Gateway gateway(cluster);
    start(cluster, gateway);
    const auto leader = cluster.elect();
    const auto effect = authorize(cluster, strategy);
    cluster.run_until([&] {
        const auto* risk = account(cluster.state_of(leader));
        return risk && risk->position_quantity.atoms() == kOrderLots && gateway.loop.evidence().obligation_count == 0;
    }, "the order to fill");
    (void)drain(gateway.loop);
    const auto order = cluster.state_of(leader).orders[0].cluster_order_id;

    VenueObservation late;
    late.kind = VenueObservationKind::order_acknowledged;
    late.venue_id = VenueId{9};
    late.venue_session_id = VenueSessionId{1};
    late.venue_sequence = VenueSequence{900};
    late.cluster_order_id = order;
    late.effect_id = effect;
    late.venue_order_id = VenueOrderId{1};
    late.evidence_digest = digest(0x6e);
    gateway.counting.injected.push_back(late);
    cluster.run_until([&] { return stops(gateway.loop.metrics(), GatewayStopCause::refused_evidence) == 1; },
                      "the refused report to stop new risk");
    const auto events = drain(gateway.loop);
    bool described = false;
    for (const auto& event : events)
        described |= event.kind == GatewayEventKind::stopped && event.cause == GatewayStopCause::refused_evidence &&
                     event.refused_code == ApplyCode::venue_observation_not_applicable &&
                     event.refused_kind == VenueObservationKind::order_acknowledged && event.order == order;
    require(described, "the stop event does not name the refused report, its order and the kernel's code");
    require(gateway.loop.new_risk_stopped() && account(cluster.state_of(leader))->position_quantity.atoms() == kOrderLots,
            "a refused report did not hold new risk, or changed the position");
}

void without_allow_new_risk_the_gateway_never_arms() {
    PaperFixture paper;
    Base base("v2-gw-unarmed");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    auto config = quiet_config(100);
    config.allow_new_risk = false;
    Gateway gateway(cluster, sim::FaultPolicy::clean(), config);
    cluster.on_round = [&] { gateway.loop.step(); };
    cluster.run_until([&] { return gateway.loop.metrics().reconciliation == BookVerdict::match; },
                      "a clean reconciliation");
    cluster.run(2000);
    require(gateway.loop.new_risk_stopped() && gateway.loop.metrics().not_configured,
            "a gateway without --allow-new-risk armed");
}

void a_gateway_that_requires_a_loss_limit_arms_only_while_one_is_set() {
    PaperFixture paper;
    Base base("v2-gw-loss-limit");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    auto config = quiet_config(100);
    config.require_loss_limit = true;
    Gateway gateway(cluster, sim::FaultPolicy::clean(), config);
    cluster.on_round = [&] { gateway.loop.step(); };
    cluster.run_until([&] { return gateway.loop.metrics().reconciliation == BookVerdict::match; },
                      "a clean reconciliation");
    cluster.run(1500);
    require(gateway.loop.new_risk_stopped() && gateway.loop.metrics().no_loss_limit &&
                !gateway.loop.metrics().awaiting_arm,
            "a gateway that requires a loss limit armed for an account that has none");

    const auto leader = cluster.elect();
    const auto limit = [&](const std::int64_t max_window_loss) {
        const auto* risk = account(cluster.state_of(leader));
        ConfigureRiskLimits limits;
        limits.account_id = risk->account_id;
        limits.instrument_id = risk->instrument_id;
        limits.risk_policy_version = risk->risk_policy_version;
        limits.expected_risk_version = risk->risk_version;
        limits.expected_kill_epoch = KillEpoch{1};
        limits.expected_executor_generation = ExecutorGeneration{1};
        limits.max_order_quantity = risk->max_order_quantity;
        limits.max_reserved_quantity = risk->max_reserved_quantity;
        limits.max_order_gross_notional = risk->max_order_gross_notional;
        limits.max_reserved_exposure = risk->max_reserved_exposure;
        limits.initial_margin_ppm = risk->initial_margin_ppm;
        limits.maximum_fee_ppm = risk->maximum_fee_ppm;
        limits.max_position_quantity = risk->max_position_quantity;
        limits.allow_short = risk->allow_short;
        limits.reason_code = 2;
        limits.max_window_loss = MoneyMicros::from_atoms(max_window_loss);
        return cluster.commit(op, limits).code;
    };
    require(limit(5'000) == ApplyCode::applied, "the loss limit could not be set");
    cluster.run_until([&] { return !gateway.loop.new_risk_stopped(); }, "the gateway to arm once a loss limit is set");
    require(!gateway.loop.metrics().no_loss_limit, "the loss-limit gauge stayed on after a limit was set");

    require(limit(0) == ApplyCode::applied, "the loss limit could not be removed");
    cluster.run_until([&] { return gateway.loop.new_risk_stopped(); }, "the gateway to stop once the limit is removed");
    cluster.run(1500);
    const auto& metrics = gateway.loop.metrics();
    require(gateway.loop.new_risk_stopped() && metrics.no_loss_limit && stops_other_than(metrics, GatewayStopCause::count) == 0,
            "removing the loss limit did not hold new risk, or latched a stop");
    require(limit(1) == ApplyCode::applied, "the loss limit could not be set again");
    cluster.run_until([&] { return !gateway.loop.new_risk_stopped(); }, "the gateway to arm again by itself");
}

void a_restart_beside_a_held_position_with_a_reset_venue_is_a_mismatch() {
    PaperFixture paper;
    Base base("v2-gw-mismatch");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto leader = cluster.elect();
    {
        Gateway first(cluster);
        start(cluster, first);
        (void)authorize(cluster, strategy);
        cluster.run_until([&] {
            const auto* risk = account(cluster.state_of(leader));
            return risk && risk->position_quantity.atoms() == kOrderLots && first.loop.evidence().obligation_count == 0;
        }, "the first process's fill");
        cluster.run(500);
    }
    Gateway restarted(cluster, sim::FaultPolicy::clean(), quiet_config(100));
    cluster.on_round = [&] { restarted.loop.step(); };
    cluster.run_until([&] { return restarted.loop.metrics().reconciliation_mismatch; }, "the mismatch", 20'000);
    cluster.run(2000);
    const auto& metrics = restarted.loop.metrics();
    require(restarted.loop.new_risk_stopped(), "a gateway beside a mismatched venue armed");
    require(metrics.reconciliation_kernel_lots == kOrderLots && metrics.reconciliation_venue_lots == 0,
            "the exported values are not the kernel's 2 lots and the venue's 0");
    require(stops(metrics, GatewayStopCause::reconciliation_mismatch) == 1 && metrics.rearms == 0,
            "the mismatch is not the counted stop cause");
    require(restarted.paper_venue->balances().base.atoms() == 0 &&
                account(cluster.state_of(leader))->position_quantity.atoms() == kOrderLots,
            "the mismatch was resolved by editing one side");
    const auto text = render_gateway_metrics(metrics);
    require(text.find("velocita_gateway_reconciliation_mismatch 1\n") != std::string::npos,
            "the mismatch gauge is not exported");
    bool logged = false;
    for (const auto& event : drain(restarted.loop))
        logged |= event.kind == GatewayEventKind::reconciliation_mismatch && event.kernel_lots == kOrderLots &&
                  event.venue_lots == 0;
    require(logged, "the mismatch was not logged with both values");
}

void a_restart_with_the_persisted_venue_reconciles_and_keeps_its_fill_record() {
    PaperFixture paper;
    Base base("v2-gw-persist");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto leader = cluster.elect();
    const auto file = base.path / "paper.state";
    create_fresh_paper_state(file);
    double first_fill_at = 0;
    {
        Gateway first(cluster, sim::FaultPolicy::clean(), Gateway::config(), {}, {file});
        start(cluster, first);
        (void)authorize(cluster, strategy);
        cluster.run_until([&] {
            const auto* risk = account(cluster.state_of(leader));
            return risk && risk->position_quantity.atoms() == kOrderLots && first.loop.evidence().obligation_count == 0;
        }, "the first process's fill");
        cluster.run(500);
        first_fill_at = first.loop.metrics().last_fill_timestamp_seconds;
    }
    Gateway restarted(cluster, sim::FaultPolicy::clean(), Gateway::config(), {}, {file});
    const auto& opening = restarted.loop.metrics();
    require(opening.fee_micros_total == 26 && opening.volume_micros_buy == 2'200 &&
                opening.last_fill_timestamp_seconds == first_fill_at && first_fill_at > 0,
            "the restarted gateway did not publish the persisted fill totals and last-fill time");
    start(cluster, restarted);
    require(restarted.loop.metrics().reconciliation_kernel_lots == kOrderLots &&
                restarted.loop.metrics().reconciliation_venue_lots == kOrderLots,
            "the restarted gateway armed without comparing 2 lots on both sides");
}

void a_restart_between_commit_and_acknowledge_redelivers_then_arms() {
    PaperFixture paper;
    Base base("v2-gw-redeliver");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto leader = cluster.elect();
    const auto file = base.path / "paper.state";
    const auto before_ack = base.path / "paper.state.before-ack";
    create_fresh_paper_state(file);
    {
        Gateway first(cluster, sim::FaultPolicy::clean(), Gateway::config(), {}, {file});
        start(cluster, first);
        cluster.on_round = [&] {
            first.loop.step();
            if (first.counting.sends.size() == 1 && !std::filesystem::exists(before_ack))
                std::filesystem::copy_file(file, before_ack);
        };
        (void)authorize(cluster, strategy);
        cluster.run_until([&] {
            const auto* risk = account(cluster.state_of(leader));
            return risk && risk->position_quantity.atoms() == kOrderLots && first.loop.evidence().obligation_count == 0;
        }, "the first process's fill");
        cluster.run(300);
    }
    std::filesystem::copy_file(before_ack, file, std::filesystem::copy_options::overwrite_existing);
    Gateway restarted(cluster, sim::FaultPolicy::clean(), Gateway::config(), {}, {file});
    cluster.on_round = [&] { restarted.loop.step(); };
    cluster.run_until([&] { return restarted.loop.phase() == GatewayPhase::running; }, "the first read");
    require(restarted.loop.new_risk_stopped(), "armed while the venue still owned unacknowledged reports");
    cluster.run_until([&] { return !restarted.loop.new_risk_stopped(); }, "the re-delivered reports to reconcile");
    require(account(cluster.state_of(leader))->position_quantity.atoms() == kOrderLots,
            "the re-delivered fill moved the position twice");
    require(restarted.durable_venue->venue().pending_observation_count() == 0 &&
                restarted.durable_venue->venue().balances().base.atoms() == kOrderLots,
            "the venue did not release the committed reports");
    require(restarted.loop.metrics().fee_micros_total == 26,
            "the re-delivered fill was not counted exactly once in the fee total");
}

void an_adopted_position_reconciles_and_arms() {
    PaperFixture paper;
    Base base("v2-gw-adopt");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto leader = cluster.elect();
    {
        Gateway first(cluster);
        start(cluster, first);
        (void)authorize(cluster, strategy);
        cluster.run_until([&] {
            const auto* risk = account(cluster.state_of(leader));
            return risk && risk->position_quantity.atoms() == kOrderLots && first.loop.evidence().obligation_count == 0;
        }, "the pre-persistence fill");
        cluster.run(300);
    }
    const auto& state = cluster.state_of(leader);
    const auto file = base.path / "paper.state";
    const auto adopted = paper::adopted_paper_state(
        Gateway::paper_config(), kIncarnation, MoneyMicros::from_atoms(25'000'000),
        account(state)->position_quantity, MoneyMicros::from_atoms(2'226),
        cluster.node(leader).status().replica.applied_index, state.state_version);
    require(adopted.error.empty() && paper::create_paper_state(file, *adopted.state) == paper::PaperStateError::none,
            "adoption failed");
    Gateway restarted(cluster, sim::FaultPolicy::clean(), Gateway::config(), {}, {file});
    start(cluster, restarted);
    require(restarted.durable_venue->venue().balances().quote.atoms() == 25'000'000 - 2'226,
            "the adopted quote is not funding minus the cost basis");
}

void a_venue_balance_ahead_of_an_open_order_is_not_a_mismatch() {
    PaperFixture paper;
    Base base("v2-gw-ahead");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    const auto leader = cluster.elect();
    VenueChoice choice;
    choice.lagging_book = true;
    choice.paper_faults.delay_permille = 1000;
    choice.paper_faults.delay_ticks = 1200;
    Gateway gateway(cluster, sim::FaultPolicy::clean(), Gateway::config(), {}, choice);
    start(cluster, gateway);
    std::uint64_t ahead_reads = 0;
    cluster.on_round = [&] {
        gateway.loop.step();
        sim::VenueBook book;
        gateway.book.read_fn(gateway.book.context, book);
        const auto* risk = account(cluster.state_of(leader));
        if (book.base.atoms() == kOrderLots && risk && risk->position_quantity.atoms() == 0) ++ahead_reads;
    };
    (void)authorize(cluster, strategy);
    cluster.run_until([&] {
        const auto* risk = account(cluster.state_of(leader));
        return risk && risk->position_quantity.atoms() == kOrderLots && gateway.loop.evidence().obligation_count == 0;
    }, "the delayed fill to commit");
    cluster.run(500);
    require(ahead_reads > 100, "the fixture never held the venue's balance ahead of the kernel");
    require(!gateway.loop.metrics().reconciliation_mismatch && !gateway.loop.new_risk_stopped(),
            "a venue balance ahead of an open order was called a mismatch");
}

void an_election_during_an_in_flight_order_rearms_only_after_it_settles() {
    PaperFixture paper;
    Base base("v2-gw-inflight");
    Cluster cluster(base.path, {1, 2, 3});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    VenueChoice choice;
    choice.lagging_book = true;
    choice.paper_faults.delay_permille = 1000;
    choice.paper_faults.delay_ticks = 1200;
    Gateway gateway(cluster, sim::FaultPolicy::clean(), quiet_config(300), {}, choice);
    start(cluster, gateway);
    bool leader_stopped = false;
    Tick filled_at = 0, rearmed_at = 0;
    cluster.on_round = [&] {
        gateway.loop.step();
        if (gateway.counting.sends.size() == 1 && !leader_stopped) {
            leader_stopped = true;
            cluster.stop(cluster.leader());
        }
        if (!leader_stopped) return;
        if (const auto current = cluster.leader(); current != 0 && filled_at == 0) {
            const auto* risk = account(cluster.state_of(current));
            if (risk && risk->position_quantity.atoms() == kOrderLots) filled_at = steady_ms();
        }
        if (gateway.loop.new_risk_stopped() == false && rearmed_at == 0 &&
            stops(gateway.loop.metrics(), GatewayStopCause::fence) != 0)
            rearmed_at = steady_ms();
    };
    (void)authorize(cluster, strategy);
    cluster.run_until([&] { return rearmed_at != 0 || gateway.loop.metrics().reconciliation_mismatch; },
                      "the re-arm after the in-flight order", 60'000);
    require(!gateway.loop.metrics().reconciliation_mismatch,
            "the venue's early balance was called a mismatch while the order was still open");
    require(filled_at != 0 && rearmed_at >= filled_at, "re-armed before the in-flight order's fill committed");
    require(gateway.counting.sends.size() == 1, "the in-flight order was sent again");
}

#ifdef VELOCITA_SWAP_TLS_DRILL
void swap_tls_unknown_restart_reconcile() {
    using namespace velocita::v2::transport;
    const char* origin=std::getenv("VELOCITA_SWAP_DRILL_ORIGIN");
    const char* ca=std::getenv("VELOCITA_SWAP_DRILL_CA");
    require(origin && ca,"isolated TLS fixture configured");
    const std::string_view address(origin);
    require(address.starts_with("https://localhost:") && address.size()>18 &&
        std::all_of(address.begin()+18,address.end(),[](char c){return c>='0' && c<='9';}),"TLS drill loopback only");
    g_swap=true;struct Reset {~Reset(){g_swap=false;}} reset;
    Base base("v2-swap-tls");Cluster c(base.path,{1,2,3});Caller op(kOperatorKey,kOperator);
    c.elect();
    require(c.commit(op,AdvanceKillEpoch{KillEpoch{1}},ProposeLane::control).code==ApplyCode::applied,"TLS drill kill epoch");
    require(c.commit(op,SetExecutorGeneration{ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"TLS drill executor");
    require(c.commit(op,InstallExecutionCatalog{fixture_catalog().root(),KillEpoch{1},ExecutorGeneration{1}}).code==ApplyCode::applied,"TLS drill catalog");
    require(c.commit(op,AssignCallerRole{CallerId{kGateway},CallerRole::gateway}).code==ApplyCode::applied,"TLS drill gateway role");
    auto opening=swap_test::opening();opening.binding.incarnation=sha256_digest(kIncarnation);
    require(c.commit(op,ApplySpotSwap{{swap::Role::operator_role,100,opening}}).code==ApplyCode::applied,"TLS drill funding");
    require(c.commit(op,SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"TLS drill normal");
    require(c.commit(op,ApplySpotSwap{{swap::Role::strategy,100,swap_test::buy(opening)}}).code==ApplyCode::applied,"TLS drill intent");
    GatewayClient wire;wire.network=&c.network;
    GatewayLink link(1,process_incarnation(0x55),gateway_routes(c.voters,caller_seed(kGatewayKey)),wire);
    const swap::PaperQuote quote{swap::QuoteAtoms{108'000'000},2000};
    require(link.request_state(steady_ms(),3000),"paper journal baseline requested");
    const auto baseline=settle(c,link);
    require(baseline.status==LinkStatus::done,"paper journal baseline authenticated");
    const SwapReconcileStoreBinding paper_binding{opening.binding.incarnation,baseline.reading.authority_domain};
    SwapResultWatermark paper_watermark(opening.binding.incarnation);
    SwapReconcileStore paper_recovery(base.path/"paper-recovery",paper_binding,paper_watermark);
    require(paper_recovery.create({paper_binding,baseline.reading.applied_index,baseline.reading.state_digest})==SwapReconcileStoreCode::none,"paper fixture journal initialized");
    SwapPaperGateway paper(link,CallerId{kGateway},1,1000,quote,opening.binding.incarnation,3000,10,paper_recovery);
    c.on_round=[&]{if(c.leader() && c.state_of(c.leader()).spot_swap.pending.phase!=swap::Phase::attempted)paper.step(steady_ms(),101);};
    c.run_until([&]{return c.leader() && c.state_of(c.leader()).spot_swap.pending.phase==swap::Phase::attempted;},"attempt precedes fake HTTPS POST");
    c.on_round={};
    if(link.busy()) {const auto attempt_ack=settle(c,link);require(attempt_ack.status==LinkStatus::done &&
        attempt_ack.result.code==ApplyCode::applied,"attempt acknowledgement authenticated before POST");}
    const auto late=plan_swap_paper_step(c.state_of(c.leader()),quote,105);
    require(late.record.has_value() && std::holds_alternative<swap::Settlement>(late.record->command),
        "independent late settlement fixture available");
    SwapHttpsConfig config;config.origin=origin;config.ca_bundle=ca;config.timeout_ms=1000;
    config.routes[0]={SwapHttpsMethod::post,"/fixture/swap"};config.routes[1]={SwapHttpsMethod::get,"/v1/gateway/balances"};config.route_count=2;
    SwapHttpsClient https(std::move(config));
    const auto response=https.exchange({SwapHttpsMethod::post,"/fixture/swap","fixture-token",R"({"fixture":true})"});
    require(response.status==SwapHttpsStatus::transport_failure && response.exposure==SwapHttpsExposure::possibly_sent &&
        response.body_size==0,"lost HTTPS acknowledgement remains ambiguous");
    require(!link.busy(),"paper gateway finished committed attempt");
    swap::Record unknown{swap::Role::gateway,102,swap::MarkUnknown{EffectId{1}}};
    require(link.submit({{CallerId{kGateway},RequestSequence{1000},swap_record_digest(unknown)},ApplySpotSwap{unknown}},
        ProposeLane::normal,steady_ms(),3000),"unknown report submitted through gateway link");
    const auto unknown_ack=settle(c,link);
    require(unknown_ack.status==LinkStatus::done && unknown_ack.result.code==ApplyCode::applied && unknown_ack.index>0,
        "unknown report committed through authenticated gateway link");
    const auto identity=config_for(base.path,c.leader(),NodeStartup::recover,c.voters,true).static_identity();
    std::array<std::byte,kMaxStaticIdentityBytes> identity_bytes{};
    const auto encoded=encode_static_identity(identity,identity_bytes);VerifiedStaticIdentity verified;
    require(VerifiedStaticIdentity::decode(std::span(identity_bytes).first(encoded.bytes_written),verified)==StaticIdentityCodecError::none,
        "pinned authority domain from cluster configuration");
    const SwapReconcileStoreBinding durable_binding{opening.binding.incarnation,verified.authority_domain()};
    const auto durable_path=base.path/"swap-result-watermark";
    {
        SwapResultWatermark before_restart(opening.binding.incarnation);
        SwapReconcileStore durable(durable_path,durable_binding,before_restart);
        require(durable.create({durable_binding,unknown_ack.index,swap_record_digest(unknown)})==SwapReconcileStoreCode::none &&
            before_restart.index()==unknown_ack.index,"authenticated unknown result persisted before restart");
    }
    c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[](const auto& n){
        return n.second->applied_state().spot_swap.pending.phase==swap::Phase::unknown;});},"unknown replicated on all voters");
    for(const auto id:c.voters)c.stop(id);
    for(const auto id:c.voters){c.network.down.erase(id);++c.epochs[id];c.nodes[id]=std::make_unique<V2Node>(
        config_for(base.path,id,NodeStartup::recover,c.voters,true),fixture_catalog(),secrets_for(id,c.voters),c.epochs[id],
        c.network.at(id),velocita::MetadataCheckpoints{},StorageCheckpoints{&slow_disk,&c.disk_owner[id]});
        require(c.state_of(id).spot_swap.reserved_quote.value()==1'002'000 &&
            c.state_of(id).spot_swap.pending.phase==swap::Phase::unknown,"WAL recovery retains unknown debit hold");}
    c.elect();
    GatewayLink recovered(2,process_incarnation(0x56),gateway_routes(c.voters,caller_seed(kGatewayKey)),wire);
    struct Clock final:SwapReconcileClock {std::uint64_t now_ms() noexcept override{return steady_ms();}} clock;
    struct Http final:SwapBalanceGet {
        SwapHttpsClient& client;unsigned calls{};
        explicit Http(SwapHttpsClient& c):client(c){}
        SwapBalanceSessionIdentity identity() const noexcept override{return {digest(700),digest(701)};}
        SwapHttpsResult get() override{++calls;return client.exchange({SwapHttpsMethod::get,"/v1/gateway/balances","fixture-token",{}});}
    } http(https);
    SwapResultWatermark watermark(opening.binding.incarnation);
    SwapReconcileStore durable(durable_path,durable_binding,watermark);
    require(durable.recover()==SwapReconcileStoreCode::none && watermark.index()==unknown_ack.index &&
        durable.latest().result.result_digest==swap_record_digest(unknown),"fresh watermark recovered from durable authenticated result");
    SwapReconcileSessionConfig session_config;session_config.binding={opening.binding,digest(700),10'000'000};
    session_config.clock_epoch=digest(702);session_config.session_nonce=digest(703);
    session_config.outside={digest(700),digest(702),digest(705),steady_ms(),0,0};
    session_config.http_identity=http.identity();session_config.authority_domain=verified.authority_domain();
    session_config.max_age_ms=10'000;session_config.state_timeout_ms=3000;
    SwapReconcileSession session(recovered,http,clock,watermark,session_config);
    const auto compare=[&]{require(session.start(),"drill reconciliation state read starts");
        c.run_until([&]{return session.poll()!=SwapReconcileSessionStatus::reading_state;},"drill reconciliation completes");};
    compare();
    require(session.result().status==SwapReconcileSessionStatus::comparison_refused && http.calls==0 &&
        session.result().comparison.code==SwapReconcileCode::projection_refused &&
        session.result().comparison.projection_code==SwapBalanceCode::unresolved_activity &&
        session.result().captured.reserved_quote.value()==1'002'000,"unknown state blocks balance GET and retains hold");
    require(c.commit(op,SetStrategyMode{StrategyMode::halted,KillEpoch{1},ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,
        "held-position stop committed");
    require(recovered.submit({{CallerId{kGateway},RequestSequence{2000},swap_record_digest(*late.record)},ApplySpotSwap{*late.record}},
        ProposeLane::normal,steady_ms(),3000),"late settlement report submitted");
    const auto ack=settle(c,recovered);
    require(ack.status==LinkStatus::done && ack.result.code==ApplyCode::applied &&
        durable.persist({durable_binding,ack.index,swap_record_digest(*late.record)})==SwapReconcileStoreCode::none &&
        watermark.index()==ack.index,"late settlement durably covered before balance comparison");
    compare();
    require(session.result().status==SwapReconcileSessionStatus::complete && http.calls==1 &&
        session.result().comparison.comparison.verdict==SpotBalanceVerdict::matching_snapshot_only &&
        session.result().captured.base.value()==9'259'259 && session.result().captured.quote.value()==3'998'000 &&
        session.result().captured.reserved_quote.value()==0 && session.result().captured.settlement_count==1,
        "authenticated state and real HTTPS fixture reconcile exact settlement once");
    require(recovered.submit({{CallerId{kGateway},RequestSequence{2001},swap_record_digest(*late.record)},ApplySpotSwap{*late.record}},
        ProposeLane::normal,steady_ms(),3000),"duplicate terminal report submitted");
    const auto duplicate=settle(c,recovered);
    require(duplicate.status==LinkStatus::done && duplicate.result.code==ApplyCode::already_applied,
        "duplicate terminal evidence is idempotent");
    c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[](const auto& n){
        const auto& s=n.second->applied_state().spot_swap;return s.settlement_count==1 && s.base.value()==9'259'259 &&
            s.quote.value()==3'998'000 && s.reserved_quote.value()==0;});},"all voters retain one exact settlement");
}
#endif

#ifdef VELOCITA_SWAP_SUBMISSION_DRILL
void swap_authority_without_quorum_or_after_kill() {
    for(unsigned mode=0;mode<2;++mode){
    g_swap=true;struct Reset{~Reset(){g_swap=false;}}reset;
    Base base("v2-swap-authority-fences");Cluster c(base.path,{1,2,3});Caller op(kOperatorKey,kOperator);
    c.elect();
    require(c.commit(op,AdvanceKillEpoch{KillEpoch{1}},ProposeLane::control).code==ApplyCode::applied,"swap kill epoch initialized");
    require(c.commit(op,SetExecutorGeneration{ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"swap executor initialized");
    require(c.commit(op,InstallExecutionCatalog{fixture_catalog().root(),KillEpoch{1},ExecutorGeneration{1}}).code==ApplyCode::applied,"swap catalog installed");
    require(c.commit(op,AssignCallerRole{CallerId{kGateway},CallerRole::gateway}).code==ApplyCode::applied,"swap gateway role assigned");
    auto opening=swap_test::opening();opening.binding.incarnation=sha256_digest(kIncarnation);
    require(c.commit(op,ApplySpotSwap{{swap::Role::operator_role,100,opening}}).code==ApplyCode::applied,"swap exact funding replicated");
    require(c.commit(op,SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"swap normal mode enabled");
    GatewayClient client;client.network=&c.network;
    GatewayLink link(1,process_incarnation(0x91),gateway_routes(c.voters,caller_seed(kGatewayKey)),client);
    require(c.commit(op,ApplySpotSwap{{swap::Role::strategy,100,swap_test::buy(opening)}}).code==ApplyCode::applied,"authority fence buy committed");
    require(link.submit(swap_test::command(kGateway,1,swap::Prepare{EffectId{1},digest(201),digest(202),1},101),
        ProposeLane::normal,steady_ms(),3000),"authority fence prepared request");
    require(settle(c,link).status==LinkStatus::done,"authority fence prepared committed");
    require(link.request_state(steady_ms(),3000),"authority fence state read");
    const auto observed=settle(c,link);require(observed.status==LinkStatus::done,"authority fence authenticated state");
    const AuthorityBarrier barrier{link.incarnation(),1,EffectId{1},swap_signing_authority_digest(link.state().spot_swap.pending),
        KillEpoch{1},ExecutorGeneration{1},opening.binding.catalog,observed.reading.authority_domain};
    if(mode==0){
        require(c.commit(op,AdvanceKillEpoch{KillEpoch{2}},ProposeLane::control).code==ApplyCode::applied,"kill committed after signer state read");
    }else{
        const auto leader=c.elect();for(const auto id:c.voters)if(id!=leader)c.stop(id);
    }
    require(link.request_swap_authority(barrier,steady_ms(),250),"stale or minority authority request starts");
    const auto refused=settle(c,link);
    require((refused.status==LinkStatus::refused || refused.status==LinkStatus::timed_out) && !refused.swap_proof,
        "kill or missing durable quorum cannot produce fresh signing authority");
    for(const auto& [id,node]:c.nodes){const auto& state=node->applied_state().spot_swap;
        require(state.pending.phase==swap::Phase::prepared && state.reserved_quote.value()==1'002'000,
            "authority refusal never sends or releases pending exposure");}
    }
}

void swap_submission_lost_reply_leader_loss_restart() {
    g_swap=true;struct Reset{~Reset(){g_swap=false;}}reset;
    Base base("v2-swap-submit-drill");Cluster c(base.path,{1,2,3});Caller op(kOperatorKey,kOperator);
    c.elect();
    require(c.commit(op,AdvanceKillEpoch{KillEpoch{1}},ProposeLane::control).code==ApplyCode::applied,"swap kill epoch initialized");
    require(c.commit(op,SetExecutorGeneration{ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"swap executor initialized");
    require(c.commit(op,InstallExecutionCatalog{fixture_catalog().root(),KillEpoch{1},ExecutorGeneration{1}}).code==ApplyCode::applied,"swap catalog installed");
    require(c.commit(op,AssignCallerRole{CallerId{kGateway},CallerRole::gateway}).code==ApplyCode::applied,"swap gateway role assigned");
    auto opening=swap_test::opening();opening.binding.incarnation=sha256_digest(kIncarnation);
    require(c.commit(op,ApplySpotSwap{{swap::Role::operator_role,100,opening}}).code==ApplyCode::applied,"swap exact funding replicated");
    require(c.commit(op,SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"swap normal mode enabled");
    GatewayClient client;client.network=&c.network;
    SwapSubmissionBinding binding;
    SwapPrepareBinding preparation_binding;
    constexpr std::string_view handle="synthetic-tm-order";
    std::array<std::byte,33> public_encoding{};public_encoding[0]=std::byte{2};
    const std::array<std::byte,8> synthetic_der{std::byte{0x30},std::byte{6},std::byte{2},std::byte{1},
        std::byte{1},std::byte{2},std::byte{1},std::byte{2}};
    swap_signing::ApiKeyStamp stamp;
    require(swap_signing::encode_api_key_stamp(public_encoding,synthetic_der,stamp)==swap_signing::StampCode::encoded,
        "synthetic stamp formats without any signing operation");
    truemarkets::SwapRestRequest request;
    require(truemarkets::encode_swap_rest_execute(*truemarkets::tm_text(handle),stamp,request)==truemarkets::SwapRestCode::encoded,
        "fixture execute uses actual REST codec");
    auto bytes=std::as_bytes(std::span(request.body.data(),request.body_size));
    constexpr std::string_view prepare_payload="VELOCITA SYNTHETIC FEE INCLUSIVE SWAP\n";
    SwapSubmissionIdentity identity;identity.effect=EffectId{1};identity.payload=sha256_digest(std::as_bytes(std::span(prepare_payload.data(),prepare_payload.size())));
    identity.execution_request=sha256_digest(bytes);
    identity.handle_size=handle.size();
    std::copy(std::as_bytes(std::span(handle.data(),handle.size())).begin(),
        std::as_bytes(std::span(handle.data(),handle.size())).end(),identity.handle.begin());
    identity.handle_digest=sha256_digest(std::span(identity.handle).first(identity.handle_size));
    struct Port final:SwapExecutionPort {
        unsigned calls{};bool durable{};SwapSubmissionStore* store{};
        void execute(const SwapSubmissionIdentity& i,std::span<const std::byte> b) override {
            ++calls;durable=store && store->latest().phase==SwapSubmissionPhase::execute_intent &&
                store->latest().identity==i && sha256_digest(b)==i.execution_request;
            throw std::runtime_error("synthetic response lost after venue accepted request");
        }
    } port;
    struct PreparePort final:SwapPreparePort {
        unsigned calls{};bool durable{};SwapPrepareStore* store{};
        transport::SwapHttpsResult create(const truemarkets::SwapRestRequest& request,const SwapPrepareRead&)override{
            ++calls;durable=store && store->latest().phase==SwapPreparePhase::create_intent &&
                store->latest().operation.request_digest==sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size)));
            constexpr std::string_view body=R"({"order_id":"synthetic-tm-order","status":"initialized","payloads":[{"digest":"a71e361bce0c479f89741f87a0248cf21cae63ee745a5d6623d0e5760aabebcc","payload":"VELOCITA SYNTHETIC FEE INCLUSIVE SWAP\n"}],"quote":{"base_asset":"So11111111111111111111111111111111111111112","quote_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","fee_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","qty":"1","qty_out":"0.009191912","fee":"0.002"}})";
            transport::SwapHttpsResult result;result.status=transport::SwapHttpsStatus::complete;
            result.exposure=transport::SwapHttpsExposure::response_received;result.http_status=201;
            std::copy(body.begin(),body.end(),result.body.begin());result.body_size=body.size();return result;
        }
    } prepare_port;
    {
        GatewayLink link(1,process_incarnation(0x81),gateway_routes(c.voters,caller_seed(kGatewayKey)),client);
        require(link.request_state(steady_ms(),3000),"submission bootstrap read requested");const auto baseline=settle(c,link);
        require(baseline.status==LinkStatus::done,"submission bootstrap authenticated");
        binding={opening.binding.incarnation,baseline.reading.authority_domain};
        preparation_binding={opening.binding,binding.authority_domain,fee_inclusive_prepare_mapping_identity()};
        SwapPrepareStore preparation(base.path/"prepare",preparation_binding);prepare_port.store=&preparation;
        require(preparation.create({preparation_binding,baseline.reading.applied_index,baseline.reading.state_digest,EffectId{0}})==SwapPrepareStoreCode::none,
            "virgin prepare bootstrap persists before unsigned create");
        SwapSubmissionStore store(base.path/"submission",binding);port.store=&store;
        require(store.create({binding,baseline.reading.applied_index,baseline.reading.state_digest,EffectId{0}})==SwapSubmissionCode::none,
            "virgin submission bootstrap persists before intent");
        auto intent=swap_test::buy(opening);intent.quote_input=swap::QuoteAtoms{998'000};
        intent.max_quote_debit=swap::QuoteAtoms{1'000'000};intent.fee_cap=swap::QuoteAtoms{2'000};
        require(c.commit(op,ApplySpotSwap{{swap::Role::strategy,100,intent}}).code==ApplyCode::applied,"fee-inclusive buy reservation replicated");
        SwapPrepareSession prepare_session(link,preparation,prepare_port,{preparation_binding,SwapPrepareInputMapping::fee_inclusive_quote_input,3000});
        require(prepare_session.start(identity.effect,baseline.reading.applied_index,steady_ms(),100),"prepare coordinator requests reserved state");
        c.on_round=[&]{(void)prepare_session.poll(steady_ms(),101);};
        c.run_until([&]{return prepare_session.status()!=SwapPrepareSessionStatus::reading_state;},"unsigned fixture prepared durably");c.on_round={};
        const auto* saved=prepare_session.prepared();
        require(saved && saved->quoted_input==1'000'000 && saved->quoted_fee==2'000 && saved->payload_digest==identity.payload && saved->handle.view()==handle && prepare_port.calls==1 && prepare_port.durable,
            "one create follows durable intent and exact payload persists before kernel prepare");
        require(link.submit(swap_test::command(kGateway,1,swap::Prepare{identity.effect,identity.payload,identity.handle_digest,1},101),
            ProposeLane::normal,steady_ms(),3000),"gateway prepare request issued");
        const auto prepared=settle(c,link);
        require(prepared.status==LinkStatus::done && prepared.result.code==ApplyCode::applied,"exact prepared identity replicated");
        require(link.request_state(steady_ms(),3000),"signer independently reads prepared state");
        const auto sign_state=settle(c,link);require(sign_state.status==LinkStatus::done,"signer prepared read authenticated");
        AuthorityBarrier signing_barrier{link.incarnation(),1,identity.effect,swap_signing_authority_digest(link.state().spot_swap.pending),
            KillEpoch{1},ExecutorGeneration{1},opening.binding.catalog,binding.authority_domain};
        auto wrong_barrier=signing_barrier;wrong_barrier.expected_effect_payload_digest=digest(255);
        require(link.request_swap_authority(wrong_barrier,steady_ms(),3000),"mismatched signing authority requested");
        require(settle(c,link).status==LinkStatus::refused,"current quorum authority refuses different exact swap digest");
        require(swap_authority_matches(link.state(),signing_barrier,binding.authority_domain),"captured prepared state matches signing barrier");
        require(swap_authority_matches(c.nodes.at(c.elect())->applied_state(),signing_barrier,binding.authority_domain),"leader prepared state matches signing barrier");
        require(link.request_swap_authority(signing_barrier,steady_ms(),3000),"exact signing barrier requested");
        const auto authorization=settle(c,link);
        if(authorization.status!=LinkStatus::done || !authorization.swap_proof)
            std::cerr<<"swap authority status="<<static_cast<int>(authorization.status)<<" reason="<<static_cast<int>(authorization.refusal_reason)<<" proof="<<authorization.swap_proof.has_value()<<'\n';
        require(authorization.status==LinkStatus::done && authorization.swap_proof &&
            authorization.swap_proof->view().request==signing_barrier && authorization.swap_proof->view().barrier_index>prepared.index &&
            authorization.swap_proof->view().applied_index>=authorization.swap_proof->view().barrier_index,
            "current term durable quorum commits exact signing scope before grant");
#ifdef VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE
        swap_signer_test::TestKey generated_key(base.path/"signer-test-key.pem");
        require(bool(generated_key.protected_key),"generated fixture key passes protected loading");
        const SwapSignerConfig signer_config{opening.binding,binding.authority_domain,digest(221),CallerId{kSigner},1,3000};
        const auto signer_binding=swap_signer_journal_binding(signer_config,generated_key.public_key);
        SwapSubmissionStore signer_store(base.path/"signer-result",signer_binding);
        require(signer_store.create({signer_binding,baseline.reading.applied_index,baseline.reading.state_digest,EffectId{0}})==SwapSubmissionCode::none,
            "independent signer fence created from authenticated bootstrap");
        GatewayClient signer_client;signer_client.network=&c.network;signer_client.caller_key=kSignerKey;
        GatewayLink signer_link(2,process_incarnation(0x85),gateway_routes(c.voters,caller_seed(kSignerKey),kSignerKey),signer_client);
        auto forbidden_barrier=signing_barrier;forbidden_barrier.incarnation=signer_link.incarnation();
        require(signer_link.request_authority(forbidden_barrier,steady_ms(),3000),"signer fixed-base authority misuse issued");
        const auto fixed_refused=settle(c,signer_link);
        require(fixed_refused.status==LinkStatus::refused && fixed_refused.refusal_reason==static_cast<std::uint8_t>(IngressRefusal::signer_read_only),
            "signer identity cannot request fixed-base send authority");
        require(signer_link.submit(swap_test::command(kSigner,1,swap::Attempt{identity.effect,identity.payload},102),
            ProposeLane::normal,steady_ms(),3000),"signer kernel command misuse issued");
        const auto command_refused=settle(c,signer_link);
        require(command_refused.status==LinkStatus::refused && command_refused.refusal_reason==static_cast<std::uint8_t>(IngressRefusal::signer_read_only),
            "signer identity cannot mutate kernel trading state");
        const SwapSignerClock signer_clock{[](void*)noexcept{return steady_ms();},[](void*)noexcept{return std::uint64_t{101};},nullptr};
        SwapSignerSession signer_session(signer_link,signer_store,*generated_key.protected_key,signer_config,signer_clock);
        const auto signing_bytes=std::as_bytes(std::span(prepare_payload.data(),prepare_payload.size()));
        require(signer_session.start(identity.effect,1,handle,signing_bytes,prepared.index,20100),"independent signer starts against real three voters");
        c.on_round=[&]{(void)signer_session.poll();};
        c.run_until([&]{return signer_session.status()!=SwapSignerStatus::reading_state && signer_session.status()!=SwapSignerStatus::reading_authority;},
            "independent signer completes fresh durable quorum authorization");c.on_round={};
        require(signer_session.status()==SwapSignerStatus::signed_once && generated_key.protected_key->signature_attempts()==1 &&
            generated_key.verifies(signing_bytes,signer_session.signature_der()),"fresh three-voter authority produces one valid exact-byte signature");
        stamp=signer_session.stamp();
        require(truemarkets::encode_swap_rest_execute(*truemarkets::tm_text(handle),stamp,request)==truemarkets::SwapRestCode::encoded,
            "real generated signature feeds the actual execute codec");
        bytes=std::as_bytes(std::span(request.body.data(),request.body_size));
        identity.execution_request=sha256_digest(bytes);
#endif
        require(link.submit(swap_test::command(kGateway,2,swap::Attempt{identity.effect,identity.payload},102),
            ProposeLane::normal,steady_ms(),3000),"gateway attempt request issued");
        const auto attempted=settle(c,link);require(attempted.status==LinkStatus::done && attempted.result.code==ApplyCode::applied,"attempt committed before execute");
        const auto minimum=attempted.index;
        c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[](const auto& n){
            return n.second->applied_state().spot_swap.pending.phase==swap::Phase::attempted;});},"attempt reaches three voters");
        SwapSubmissionSession session(link,store,port,{opening.binding,binding.authority_domain,3000});
        require(session.start(identity,bytes,minimum,steady_ms()),"committed attempt requests authenticated submission read");
        c.on_round=[&]{(void)session.poll(steady_ms(),103);};
        c.run_until([&]{return session.status()!=SwapSubmissionSessionStatus::reading_state;},"one external fixture submission");c.on_round={};
        require(port.calls==1 && port.durable && session.status()==SwapSubmissionSessionStatus::needs_reconciliation,
            "lost response leaves exactly one send with prior durable identity");
        for(const auto& [id,node]:c.nodes) {
            const auto& state=node->applied_state().spot_swap;
            require(state.pending.phase==swap::Phase::attempted && state.reserved_quote.value()==1'000'000 &&
                state.quote.value()==5'000'000 && state.settlement_count==0,"uncertain send retains exact exposure on all voters");
        }
        const auto old=c.elect();c.stop(old);require(c.elect()!=old,"remaining quorum elects replacement leader");
    }
    port.store=nullptr;
    prepare_port.store=nullptr;
    {
        GatewayLink link(2,process_incarnation(0x82),gateway_routes(c.voters,caller_seed(kGatewayKey)),client);
        SwapPrepareStore preparation(base.path/"prepare",preparation_binding);
        require(preparation.recover()==SwapPrepareStoreCode::none,"restart recovers exact unsigned payload and order mapping");
        SwapPrepareSession replay_prepare(link,preparation,prepare_port,{preparation_binding,SwapPrepareInputMapping::fee_inclusive_quote_input,3000});
        require(replay_prepare.prepared() && replay_prepare.prepared()->payload_digest==identity.payload && prepare_port.calls==1,
            "prepared restart exposes retained bytes without new create");
        SwapSubmissionStore store(base.path/"submission",binding);require(store.recover()==SwapSubmissionCode::none,"restart recovers external execute identity");
        port.store=&store;SwapSubmissionSession session(link,store,port,{opening.binding,binding.authority_domain,3000});
        require(session.status()==SwapSubmissionSessionStatus::needs_reconciliation && !session.start(identity,bytes,10000,steady_ms()),
            "leader loss and gateway restart cannot resend ambiguous order");
        for(unsigned n=0;n<10;++n)(void)session.poll(steady_ms(),104);
        require(port.calls==1,"recovery polls perform zero additional sends");
        require(link.submit(swap_test::command(kGateway,3,swap::MarkUnknown{identity.effect},104),ProposeLane::normal,
            steady_ms(),3000),"gateway unknown request issued");
        const auto unknown=settle(c,link);
        require(unknown.status==LinkStatus::done && unknown.result.code==ApplyCode::applied,"unknown state replicated after restart");
        c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[](const auto& n){
            return n.second->applied_state().spot_swap.pending.phase==swap::Phase::unknown;});},"unknown reaches remaining quorum");
        for(const auto& [id,node]:c.nodes)require(node->applied_state().spot_swap.reserved_quote.value()==1'000'000 &&
            node->applied_state().spot_swap.settlement_count==0,"leader failover never releases unresolved hold");
        require(store.latest().identity==identity && store.latest().phase==SwapSubmissionPhase::execute_intent,
            "order handle and request binding survive cluster failover");
        SwapSubmissionResolutionSession unresolved(link,store,{opening.binding,binding.authority_domain,3000});
        require(unresolved.start(unknown.index,steady_ms()),"unknown journal resolution requests actual state");
        c.on_round=[&]{(void)unresolved.poll(steady_ms());};
        c.run_until([&]{return unresolved.status()!=SwapSubmissionResolutionStatus::reading_state;},"unknown resolution completed");c.on_round={};
        require(unresolved.status()==SwapSubmissionResolutionStatus::needs_reconciliation &&
            store.latest().phase==SwapSubmissionPhase::execute_intent,"unknown result cannot resolve local journal");
        SwapSubmissionTransaction signature{};
        for(std::size_t n=0;n<signature.size();++n)signature[n]=static_cast<std::byte>(n);
        struct ReadClock final:SwapOrderReadClock {std::uint64_t now_ms()noexcept override{return steady_ms();}} read_clock;
        struct OrderPort final:SwapOrderGet {
            unsigned calls{};
            SwapOrderSessionIdentity identity()const noexcept override{return {digest(214),digest(215)};}
            transport::SwapHttpsResult get(std::string_view path)override{
                ++calls;require(path=="/v1/gateway/orders/synthetic-tm-order","recovery reads only saved order handle");
                constexpr std::string_view body=R"({"order_id":"synthetic-tm-order","asset_id":"495e07ac-fa3e-4179-85b7-b1e8cece3dc4","base_asset":"SOL","quote_asset":"USDC","asset_type":"spot","venue":"defi","status":"complete","type":"market","side":"buy","qty_unit":"quote","qty":"1","executed_qty":"0.01","leaves_qty":"0","executed_vwap":"100","fee":"0.002","tx_hash":"1GMkH3brNXiNNs1tiFZHu4yZSRrzJwxi5wB9bHFtMinfCXNnR1adh8Vo8NTheK4evneedH4qmvjeqcBBNAefgS"})";
                transport::SwapHttpsResult r;r.status=transport::SwapHttpsStatus::complete;r.exposure=transport::SwapHttpsExposure::response_received;r.http_status=200;
                r.body_size=body.size();std::copy(body.begin(),body.end(),r.body.begin());return r;
            }
        } order_port;
        SwapOrderStatusReader order_reader(store,preparation,order_port,read_clock,{preparation_binding,order_port.identity(),3000});
        require(order_reader.read().status==SwapOrderReadStatus::terminal_needs_finality && order_port.calls==1 &&
            store.latest().transaction==signature && store.latest().phase==SwapSubmissionPhase::execute_intent,
            "status identity is retained without mistaking complete for settlement finality");
        for(const auto& [id,node]:c.nodes)require(node->applied_state().spot_swap.reserved_quote.value()==1'000'000 &&
            node->applied_state().spot_swap.settlement_count==0,"complete REST response alone never releases funds");
        const auto canonical_transaction=solana_signature_transaction_digest(signature);
        require(canonical_transaction.has_value(),"synthetic signature has canonical transaction identity");
        swap::Settlement terminal;terminal.effect=identity.effect;terminal.custody=opening.binding.custody;
        terminal.payload=identity.payload;terminal.transaction=*canonical_transaction;terminal.evidence=digest(212);
        terminal.outcome=swap::Outcome::succeeded;terminal.base_credit=swap::BaseAtoms{10'000'000};
        terminal.quote_debit=swap::QuoteAtoms{1'000'000};terminal.fee=swap::QuoteAtoms{2'000};terminal.finalized=true;
        require(link.submit(swap_test::command(kGateway,4,terminal,105),ProposeLane::normal,steady_ms(),3000),
            "synthetic external settlement submitted by authorized gateway");
        const auto settled=settle(c,link);
        require(settled.status==LinkStatus::done && settled.result.code==ApplyCode::applied,"synthetic terminal settlement committed");
        SwapSubmissionResolutionSession resolution(link,store,{opening.binding,binding.authority_domain,3000});
        require(resolution.start(settled.index,steady_ms()),"terminal journal resolution requests authenticated state");
        c.on_round=[&]{(void)resolution.poll(steady_ms());};
        c.run_until([&]{return resolution.status()!=SwapSubmissionResolutionStatus::reading_state;},"authenticated journal resolution completed");c.on_round={};
        require(resolution.status()==SwapSubmissionResolutionStatus::resolved &&
            store.latest().phase==SwapSubmissionPhase::resolved && resolution.result().settlement==terminal && port.calls==1,
            "committed exact terminal resolves journal without another execute");
        c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[](const auto& n){
            const auto& state=n.second->applied_state().spot_swap;
            return state.settlement_count==1 && state.reserved_quote.value()==0 && state.quote.value()==4'000'000 &&
                state.base.value()==10'000'000;});},"remaining quorum retains exact one settlement and balances");
    }
    port.store=nullptr;
    SwapSubmissionStore recovered(base.path/"submission",binding);
    require(recovered.recover()==SwapSubmissionCode::none && recovered.latest().phase==SwapSubmissionPhase::resolved &&
        recovered.latest().identity==identity && port.calls==1,"resolved journal survives another restart without duplicate send");
}
#endif

void swap_paper_journal_write_failure() {
    g_swap = true; struct Reset { ~Reset() { g_swap = false; } } reset;
    Base base("v2-swap-journal-failure"); Cluster c(base.path,{1,2,3}); Caller op(kOperatorKey,kOperator);
    c.elect();
    require(c.commit(op,AdvanceKillEpoch{KillEpoch{1}},ProposeLane::control).code==ApplyCode::applied,"swap kill epoch initialized");
    require(c.commit(op,SetExecutorGeneration{ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"swap executor initialized");
    require(c.commit(op,InstallExecutionCatalog{fixture_catalog().root(),KillEpoch{1},ExecutorGeneration{1}}).code==ApplyCode::applied,"swap catalog installed");
    require(c.commit(op,AssignCallerRole{CallerId{kGateway},CallerRole::gateway}).code==ApplyCode::applied,"swap gateway role assigned");
    auto opening=swap_test::opening();opening.binding.incarnation=sha256_digest(kIncarnation);
    require(c.commit(op,ApplySpotSwap{{swap::Role::operator_role,100,opening}}).code==ApplyCode::applied,"swap exact funding replicated");
    require(c.commit(op,SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"swap normal mode enabled");
    require(c.commit(op,ApplySpotSwap{{swap::Role::strategy,100,swap_test::buy(opening)}}).code==ApplyCode::applied,"explicit buy replicated");
    const swap::PaperQuote quote{swap::QuoteAtoms{108'000'000},2000};
    GatewayClient client;client.network=&c.network;
    GatewayLink link(1,process_incarnation(0x58),gateway_routes(c.voters,caller_seed(kGatewayKey)),client);
    require(link.request_state(steady_ms(),3000),"fault fixture baseline requested");
    const auto baseline=settle(c,link);
    require(baseline.status==LinkStatus::done,"fault fixture baseline authenticated");
    const SwapReconcileStoreBinding binding{opening.binding.incarnation,baseline.reading.authority_domain};
    SwapResultWatermark watermark(opening.binding.incarnation);
    bool fail_sync=false;
    SwapReconcileStore recovery(base.path/"recovery",binding,watermark,
        {+[](SwapReconcileStoreStage stage,void* context) noexcept {
            return !(stage==SwapReconcileStoreStage::temporary_sync && *static_cast<bool*>(context));
        },&fail_sync});
    bool refused=false;
    try { SwapPaperGateway unopened(link,CallerId{kGateway},1,1000,quote,opening.binding.incarnation,3000,10,recovery); }
    catch(const std::invalid_argument&) { refused=true; }
    require(refused,"gateway constructor refuses an unopened recovery journal");
    require(recovery.create({binding,baseline.reading.applied_index,baseline.reading.state_digest})==SwapReconcileStoreCode::none,"fault fixture journal initialized");
    SwapPaperGateway gateway(link,CallerId{kGateway},1,1000,quote,opening.binding.incarnation,3000,10,recovery);
    fail_sync=true;
    c.on_round=[&] {
        gateway.step(steady_ms(),101);
        if(recovery.status()!=SwapReconcileStoreCode::none)
            require(gateway.stopped(),"journal failure stops gateway in the acknowledgement step");
    };
    c.run_until([&]{return gateway.stopped();},"gateway halts on durable write failure");
    c.on_round={};
    c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[](const auto& node) {
        return node.second->applied_state().spot_swap.pending.phase==swap::Phase::prepared;
    });},"prepared record reaches all voters after gateway stops");
    require(watermark.index()==-1 && recovery.status()==SwapReconcileStoreCode::io_failure,
        "failed acknowledgement durability closes the visible watermark");
    for(const auto& [id,node]:c.nodes) {
        const auto& state=node->applied_state().spot_swap;
        require(state.pending.phase==swap::Phase::prepared && state.reserved_quote.value()==1'002'000 &&
            state.settlement_count==0 && state.quote.value()==5'000'000 && state.base.value()==0,
            "journal failure retains funded cash and hold without attempt or settlement");
    }
}

void swap_paper_raft_gateway_restart() {
    g_swap = true; struct Reset { ~Reset() { g_swap = false; } } reset;
    Base base("v2-swap-raft"); Cluster c(base.path,{1,2,3}); Caller op(kOperatorKey,kOperator);
    c.elect();
    require(c.commit(op,AdvanceKillEpoch{KillEpoch{1}},ProposeLane::control).code==ApplyCode::applied,"swap kill epoch initialized");
    require(c.commit(op,SetExecutorGeneration{ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"swap executor initialized");
    require(c.commit(op,InstallExecutionCatalog{fixture_catalog().root(),KillEpoch{1},ExecutorGeneration{1}}).code==ApplyCode::applied,"swap catalog installed");
    require(c.commit(op,AssignCallerRole{CallerId{kGateway},CallerRole::gateway}).code==ApplyCode::applied,"swap gateway role assigned");
    auto opening=swap_test::opening();opening.binding.incarnation=sha256_digest(kIncarnation);
    require(c.commit(op,ApplySpotSwap{{swap::Role::operator_role,100,opening}}).code==ApplyCode::applied,"swap exact funding replicated");
    require(c.commit(op,SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"swap normal mode enabled");
    require(c.commit(op,ApplySpotSwap{{swap::Role::strategy,100,swap_test::buy(opening)}}).code==ApplyCode::applied,"explicit buy replicated");
    const swap::PaperQuote quote{swap::QuoteAtoms{108'000'000},2000};
    GatewayClient client;client.network=&c.network;
    StateReplay initial_replay;
    c.network.gateway_response=[&](std::span<const std::byte> envelope){return initial_replay.pass(c.network,envelope);};
    {
        GatewayLink link(1,process_incarnation(0x50),gateway_routes(c.voters,caller_seed(kGatewayKey)),client);
        require(link.request_state(steady_ms(),3000),"journal baseline state requested");
        const auto baseline=settle(c,link);
        require(baseline.status==LinkStatus::done,"journal baseline state authenticated");
        initial_replay.old=initial_replay.latest;
        require(!initial_replay.old.empty(),"pre-ack snapshot retained for restart fence");
        c.network.gateway_response={};
        const SwapReconcileStoreBinding binding{opening.binding.incarnation,baseline.reading.authority_domain};
        SwapResultWatermark watermark(opening.binding.incarnation);
        SwapReconcileStore recovery(base.path/"paper-recovery",binding,watermark);
        require(recovery.create({binding,baseline.reading.applied_index,baseline.reading.state_digest})==SwapReconcileStoreCode::none,"paper journal fixture initialized");
        SwapPaperGateway gateway(link,CallerId{kGateway},1,1000,quote,opening.binding.incarnation,3000,10,recovery);
        c.on_round=[&] {
            if(c.leader()!=0 && c.state_of(c.leader()).spot_swap.pending.phase!=swap::Phase::attempted)
                gateway.step(steady_ms(),101);
        };
        c.run_until([&]{return c.leader()!=0 && c.state_of(c.leader()).spot_swap.pending.phase==swap::Phase::attempted;},"swap attempt committed before restart");
        require(!gateway.stopped(),"gateway reaches attempted without refusal");
        require(recovery.latest().result.index>baseline.reading.applied_index && recovery.latest().ordinal>=2,
            "gateway persists authenticated command acknowledgements before next action");c.on_round={};
    }
    c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[&](const auto& n){
        return n.second->applied_state().spot_swap.pending.phase==swap::Phase::attempted;});},"attempt on all voters");
    for(const auto id:c.voters) c.stop(id);
    for(const auto id:c.voters) {
        c.network.down.erase(id);++c.epochs[id];
        c.nodes[id]=std::make_unique<V2Node>(config_for(base.path,id,NodeStartup::recover,c.voters,true),
            fixture_catalog(),secrets_for(id,c.voters),c.epochs[id],c.network.at(id),velocita::MetadataCheckpoints{},
            StorageCheckpoints{&slow_disk,&c.disk_owner[id]});
        require(c.state_of(id).spot_swap.reserved_quote.value()==1'002'000,"WAL recovery preserves swap hold");
    }
    c.elect();
    StateReplay replay;
    c.network.gateway_response=[&](std::span<const std::byte> envelope) {
        const auto pass=replay.pass(c.network,envelope);
        if(replay.old.empty() && !replay.latest.empty() && replay.latest.size()==replay.latest.front().header.chunk_count)
            replay.old=replay.latest;
        return pass;
    };
    GatewayLink link(2,process_incarnation(0x70),gateway_routes(c.voters,caller_seed(kGatewayKey)),client);
    require(link.request_state(steady_ms(),3000),"restarted journal domain requested");
    const auto baseline=settle(c,link);
    require(baseline.status==LinkStatus::done,"restarted journal domain authenticated");
    const SwapReconcileStoreBinding binding{opening.binding.incarnation,baseline.reading.authority_domain};
    SwapResultWatermark watermark(opening.binding.incarnation);
    SwapReconcileStore recovery(base.path/"paper-recovery",binding,watermark);
    require(recovery.recover()==SwapReconcileStoreCode::none && watermark.index()>0,"gateway restart recovers journal without bootstrap");
    SwapPaperGateway gateway(link,CallerId{kGateway},1000,2000,quote,opening.binding.incarnation,3000,10,recovery);
    c.on_round=[&]{gateway.step(steady_ms(),102);};
    initial_replay.substitute=true;
    c.network.gateway_response=[&](std::span<const std::byte> envelope){return initial_replay.pass(c.network,envelope);};
    c.run(100);
    require(gateway.next_sequence()==1000 && !gateway.stopped(),"recovered durable index fences first stale state before any command");
    c.network.gateway_response=[&](std::span<const std::byte> envelope) {
        const auto pass=replay.pass(c.network,envelope);
        if(replay.old.empty() && !replay.latest.empty() && replay.latest.size()==replay.latest.front().header.chunk_count)
            replay.old=replay.latest;
        return pass;
    };
    c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[&](const auto& n){
        return n.second->applied_state().spot_swap.settlement_count==1;});},"paper settlement after voter and gateway restart");
    for(const auto& [id,node]:c.nodes) {
        const auto& s=node->applied_state().spot_swap;
        require(s.base.value()==9'259'259 && s.quote.value()==3'998'000 && s.reserved_quote.value()==0 &&
            s.fees.value()==2000,"all voters agree on exact atomic fill once");
    }
    c.run(100);require(!gateway.stopped(),"paper gateway remains idle after recovered fill");
    require(!replay.old.empty(),"attempted state captured for stale read check");
    replay.substitute=true;const auto sequence=gateway.next_sequence();c.run(200);
    require(gateway.next_sequence()==sequence && !gateway.stopped(),"stale snapshot cannot repeat a committed paper settlement");
    replay.substitute=false;c.run(100);require(!gateway.stopped(),"fresh snapshot resumes idle polling");
    c.on_round={};c.network.gateway_response={};
}

}

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
#ifdef VELOCITA_SWAP_SUBMISSION_DRILL
        {"swap_submission_lost_reply_leader_loss_restart",swap_submission_lost_reply_leader_loss_restart},
        {"swap_authority_without_quorum_or_after_kill",swap_authority_without_quorum_or_after_kill},
#endif
#ifdef VELOCITA_SWAP_TLS_DRILL
        {"swap_tls_unknown_restart_reconcile",swap_tls_unknown_restart_reconcile},
#endif
        {"swap_paper_raft_gateway_restart", swap_paper_raft_gateway_restart},
        {"swap_paper_journal_write_failure", swap_paper_journal_write_failure},
        {"node_refuses_wrong_caller_follower_and_unconfigured_gateway",
         node_refuses_wrong_caller_follower_and_unconfigured_gateway},
        {"authorized_order_is_delivered_once_and_its_fill_moves_the_position",
         authorized_order_is_delivered_once_and_its_fill_moves_the_position},
        {"an_idle_gateway_proposes_nothing_after_acknowledging_its_evidence",
         an_idle_gateway_proposes_nothing_after_acknowledging_its_evidence},
        {"forged_replayed_mismatched_and_stale_proofs_are_refused", forged_replayed_mismatched_and_stale_proofs_are_refused},
        {"lost_and_duplicated_responses_still_deliver_exactly_once", lost_and_duplicated_responses_still_deliver_exactly_once},
        {"leader_change_during_authority_request_sends_nothing", leader_change_during_authority_request_sends_nothing},
        {"unknown_outcome_stops_new_risk_and_is_committed", unknown_outcome_stops_new_risk_and_is_committed},
        {"gateway_started_with_an_unsettled_effect_never_sends_it", gateway_started_with_an_unsettled_effect_never_sends_it},
        {"effect_for_another_catalog_row_is_not_sent", effect_for_another_catalog_row_is_not_sent},
        {"paper_venue_fill_books_position_and_measured_fee", paper_venue_fill_books_position_and_measured_fee},
        {"fence_rearms_after_the_quiet_period_and_then_delivers_once",
         fence_rearms_after_the_quiet_period_and_then_delivers_once},
        {"a_timed_out_state_read_is_a_fence_that_rearms", a_timed_out_state_read_is_a_fence_that_rearms},
        {"a_control_change_latches_until_an_operator_restart", a_control_change_latches_until_an_operator_restart},
        {"a_gateway_that_requires_a_loss_limit_arms_only_while_one_is_set",
         a_gateway_that_requires_a_loss_limit_arms_only_while_one_is_set},
        {"a_caller_outside_the_operator_band_is_paced_at_ingress",
         a_caller_outside_the_operator_band_is_paced_at_ingress},
        {"many_round_trips_never_stop_the_gateway", many_round_trips_never_stop_the_gateway},
        {"a_leader_with_a_slow_disk_never_makes_the_gateway_question_a_filled_order",
         a_leader_with_a_slow_disk_never_makes_the_gateway_question_a_filled_order},
        {"a_state_read_older_than_a_committed_result_is_discarded_as_a_fence",
         a_state_read_older_than_a_committed_result_is_discarded_as_a_fence},
        {"a_refused_report_stops_new_risk_and_says_which_report_and_why",
         a_refused_report_stops_new_risk_and_says_which_report_and_why},
        {"flatten_rearms_by_itself_and_sells_exactly_the_position",
         flatten_rearms_by_itself_and_sells_exactly_the_position},
        {"a_reducing_mode_that_arrives_with_another_control_change_latches",
         a_reducing_mode_that_arrives_with_another_control_change_latches},
        {"without_allow_new_risk_the_gateway_never_arms", without_allow_new_risk_the_gateway_never_arms},
        {"a_restart_beside_a_held_position_with_a_reset_venue_is_a_mismatch",
         a_restart_beside_a_held_position_with_a_reset_venue_is_a_mismatch},
        {"a_restart_with_the_persisted_venue_reconciles_and_keeps_its_fill_record",
         a_restart_with_the_persisted_venue_reconciles_and_keeps_its_fill_record},
        {"a_restart_between_commit_and_acknowledge_redelivers_then_arms",
         a_restart_between_commit_and_acknowledge_redelivers_then_arms},
        {"an_adopted_position_reconciles_and_arms", an_adopted_position_reconciles_and_arms},
        {"a_venue_balance_ahead_of_an_open_order_is_not_a_mismatch",
         a_venue_balance_ahead_of_an_open_order_is_not_a_mismatch},
        {"an_election_during_an_in_flight_order_rearms_only_after_it_settles",
         an_election_during_an_in_flight_order_rearms_only_after_it_settles},
    };
    const char* only = std::getenv("GATEWAY_TEST");
    int failures = 0;
    std::size_t executed = 0;
    for (const auto& [name, test] : tests) {
        if (only && std::string(only) != name) continue;
        ++executed;
        try {
            test();
            std::cout << "PASS " << name << std::endl;
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "FAIL " << name << ": " << error.what() << std::endl;
        }
    }
    if (executed == 0) { std::cerr << "no matching gateway test\n"; return 1; }
    return failures == 0 ? 0 : 1;
}
