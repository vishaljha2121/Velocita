// The gateway against real V2Node owner loops over an in-process fake network.
// Callers follow the Phase 2 plan: operator CallerId 9 (key 11), strategy 21
// (key 12), gateway 22 (key 13). Expectations come from fixture constants and
// from the voters' own kernels, never from the gateway code under test.
#include "v2_catalog_test_support.hpp"

#include "velocita/v2/gateway_dispatch.hpp"
#include "velocita/v2/gateway_loop.hpp"
#include "velocita/v2/node.hpp"
#include "velocita/v2/paper_venue.hpp"
#include "velocita/v2/venue_simulator.hpp"
#include "velocita/v2/venue_transport.hpp"

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

constexpr std::int32_t kOperatorKey = 11, kStrategyKey = 12, kGatewayKey = 13;
constexpr std::uint64_t kOperator = 9, kStrategy = 21, kGateway = 22;
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

// --- Fixture economics: buy 2 lots at 11 ticks, account 5 / venue 9 / instrument 4.
constexpr std::int64_t kOrderLots = 2;

// The paper variant binds the same row key to a fill-or-kill, no-post-only
// venue, as the True Markets row does.
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
bool g_paper = false;  // which fixture economics the next cluster uses
const VerifiedExecutionCatalog& fixture_catalog() { return g_paper ? paper_catalog() : catalog_test::standard_catalog(); }

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
    executable.rule_binding_count = 1;
    executable.rule_template_slots[0] = ActionTemplateSlot{1};
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

// --- Network -----------------------------------------------------------------

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
    std::map<std::int32_t, std::deque<Bytes>> responses;  // caller key id -> frames
    std::set<std::int32_t> down;
    // Gateway fault hooks: return false to drop the frame.
    std::function<bool(std::int32_t node, std::span<const std::byte>)> gateway_ingress = {};
    std::function<bool(std::span<const std::byte>)> gateway_response = {};
    bool duplicate_gateway_responses{};
    std::vector<Bytes> gateway_tap;  // every gateway response frame offered

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
    bool offer_ingress(std::int32_t node, std::span<const std::byte> envelope) override {
        if (network->down.contains(node)) return false;
        if (network->gateway_ingress && !network->gateway_ingress(node, envelope)) return true;
        network->at(node).ingress_inbox.emplace_back(envelope.begin(), envelope.end());
        return true;
    }
    std::optional<std::size_t> receive_response(std::span<std::byte> buffer) override {
        auto& queue = network->responses[kGatewayKey];
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
    config.max_history_wal_bytes = 1ULL << 30;
    config.digest = digest(static_cast<std::uint64_t>(id));
    return config;
}

NodeSecrets secrets_for(std::int32_t id, const std::vector<std::int32_t>& voters) {
    NodeSecrets secrets;
    for (const auto peer : voters)
        if (peer != id) secrets.voters.emplace(peer, SecretSet{secret(link_seed(id, peer))});
    for (const auto caller : {kOperatorKey, kStrategyKey, kGatewayKey})
        secrets.callers.emplace(caller, SecretSet{secret(caller_seed(caller))});
    return secrets;
}

// A synchronous test caller for operator and strategy commands.
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
    // Verified, fenced body of one response frame.
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

struct Cluster {
    std::filesystem::path base;
    Network network;
    std::vector<std::int32_t> voters;
    std::map<std::int32_t, std::unique_ptr<V2Node>> nodes;
    std::map<std::int32_t, std::uint64_t> epochs;
    bool gateway_configured;
    std::function<void()> on_round = {};

    Cluster(std::filesystem::path root, std::vector<std::int32_t> ids, bool gateway = true)
        : base(std::move(root)), voters(std::move(ids)), gateway_configured(gateway) {
        for (const auto id : voters) {
            epochs[id] = 1;
            nodes[id] = std::make_unique<V2Node>(config_for(base, id, NodeStartup::bootstrap, voters, gateway_configured),
                                                 fixture_catalog(), secrets_for(id, voters), epochs[id],
                                                 network.at(id));
        }
    }
    ~Cluster() { nodes.clear(); }
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
    // Commit one command through the leader and return the reducer's result.
    ApplyResult commit(Caller& caller, CommandBody body, ProposeLane lane = ProposeLane::normal) {
        const Command command{{CallerId{caller.caller_id}, RequestSequence{caller.next_sequence++},
                               digest(caller.caller_id * 1000 + caller.next_sequence)}, std::move(body)};
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
                    if (response.kind == ResponseKind::refused) round = 4000;  // retry through the new leader
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

// The operator ceremony and a recorded source event; no decision yet.
void prepare_cluster(Cluster& cluster, Caller& operator_caller, Caller& strategy) {
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
    limits.maximum_fee_ppm = g_paper ? 15'000 : 2'500;  // the paper venue charges 115 bp
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

// The decision and its authorization, after the gateway is already running: a
// gateway that starts with an unsettled effect must treat it as possibly sent.
EffectId authorize(Cluster& cluster, Caller& strategy) {
    const auto evaluated =
        cluster.commit(strategy, EvaluateDecisionFrame{DecisionId{20}, EvaluationMode::authoritative, decision_frame()});
    require(evaluated.decision_code == DecisionCode::action, "the decision did not produce an action");
    const auto authorized = cluster.commit(strategy, AuthorizeDecision{DecisionId{20}});
    require(authorized.code == ApplyCode::applied && authorized.effect_id().value() != 0, "authorization failed");
    return authorized.effect_id();
}

std::vector<ClientRoute> gateway_routes(const std::vector<std::int32_t>& voters, std::uint8_t seed) {
    std::vector<ClientRoute> routes;
    for (const auto id : voters) {
        std::vector<AuthKey> responses;
        responses.push_back(key(seed, AuthRole::response, id, kGatewayKey));
        routes.push_back({id, key(seed, AuthRole::ingress, kGatewayKey, id), std::move(responses)});
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

// Counts every send that reaches the venue seam: the oracle for "sent once".
struct CountingVenue {
    sim::VenueTransport inner{};
    std::vector<OrderEffect> sends;
    static sim::SendOutcome send(void* self, const OrderEffect& effect, Tick now) {
        auto& venue = *static_cast<CountingVenue*>(self);
        venue.sends.push_back(effect);
        return venue.inner.send(effect, now);
    }
    static std::size_t poll(void* self, std::span<VenueObservation> out, Tick now) {
        return static_cast<CountingVenue*>(self)->inner.poll(out, now);
    }
    static void acknowledge(void* self, const VenueObservation& observation) {
        static_cast<CountingVenue*>(self)->inner.acknowledge(observation);
    }
    static sim::QueryResult query(void* self, ClusterOrderId order, Tick now) {
        return static_cast<CountingVenue*>(self)->inner.query(order, now);
    }
    static VenueSessionId session(void* self) { return static_cast<CountingVenue*>(self)->inner.session(); }
    sim::VenueTransport transport() { return {&send, &poll, &acknowledge, &query, &session, this}; }
};

// The whole gateway process in-process: link, loop and a simulated venue.
struct Gateway {
    GatewayClient client;
    GatewayLink link;
    sim::SimulatedVenue venue;
    std::unique_ptr<paper::PaperVenue> paper_venue;
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
    Gateway(Cluster& cluster, sim::FaultPolicy faults = sim::FaultPolicy::clean(),
            GatewayLoopConfig loop_config = config(), GatewayLinkTiming timing = {})
        : client{},
          link((client.network = &cluster.network, 1), process_incarnation(0x40),
               gateway_routes(cluster.voters, caller_seed(kGatewayKey)), client, timing),
          venue(7, faults, VenueId{9}),
          counting{g_paper ? paper::paper_venue_transport(*(paper_venue = std::make_unique<paper::PaperVenue>(
                                 paper_config(), paper::PaperFaultPolicy::clean(), 1)))
                           : sim::simulated_venue_transport(venue),
                   {}},
          loop(with_view(loop_config), link, fixture_catalog(), counting.transport(),
               {&loop_clock, &wall_clock, nullptr}) {}
};

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

// Starts stepping GATEWAY every round and waits until it has activated.
void start(Cluster& cluster, Gateway& gateway) {
    cluster.on_round = [&] { gateway.loop.step(); };
    cluster.run_until([&] { return gateway.loop.phase() == GatewayPhase::running && !gateway.loop.new_risk_stopped(); },
                      "the gateway to activate on a clean cluster");
}

// ---------------------------------------------------------------------------

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
    cluster.run(3000);  // long enough for any duplicate send, ack and heartbeat

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
    // Two evidence commands (ack, fill) preceded the acknowledgement that retires them.
    const auto* producer = cursor(cluster.state_of(leader), kGateway);
    require(producer && producer->gateway_results_acknowledged_through.value() >= 2,
            "the gateway never acknowledged its committed evidence");
    require(metrics.stages[static_cast<std::size_t>(GatewayStage::fill_to_committed)].count == 1 &&
                metrics.stages[static_cast<std::size_t>(GatewayStage::proof_to_sent)].count == 1,
            "stage histograms did not record the one order");
}

// Raw gateway request envelope from any caller, for refusal tests.
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

        // Malformed: a state-read kind with a trailing byte.
        std::array<std::byte, 27> body{};
        require(encode_gateway_request({GatewayRequestKind::state_read, process_incarnation(1), 3, {}}, body).error ==
                    NodeProtocolError::none, "fixture");
        cluster.network.at(leader).ingress_inbox.push_back(gw.seal(leader, body, caller_seed(kGatewayKey)));
        const auto malformed = await_refusal(cluster, gw);
        require(malformed && malformed->reason == static_cast<std::uint8_t>(IngressRefusal::malformed_command),
                "a malformed gateway request was not refused as malformed");

        // The leader serves the configured gateway: the reassembled snapshot is
        // exactly the leader's applied state (its own canonical digest).
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
        return false;  // the barrier never reaches the old leader
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
    // Mode 2: the same request resent every millisecond while its barrier is
    // pending, so resends reach the node before and after the proof exists.
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

// Pumps LINK until its request settles.
LinkOutcome settle(Cluster& cluster, GatewayLink& link) {
    std::optional<LinkOutcome> outcome;
    cluster.run_until([&] {
        auto result = link.pump(steady_ms());
        if (result.status != LinkStatus::pending) outcome = std::move(result);
        return outcome.has_value();
    }, "a gateway request to settle");
    return std::move(*outcome);
}

Bytes seal_response(std::uint8_t seed, std::int32_t node, std::uint64_t sequence, std::span<const std::byte> body) {
    Bytes out(kMaxAuthEnvelopeBytes);
    const auto env = encode_auth_envelope(key(seed, AuthRole::response, node, kGatewayKey), 1, sequence, body, out);
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

    // Genuine proof, captured on the wire for the forgeries below.
    cluster.network.gateway_tap.clear();
    require(link.request_authority(*barrier, steady_ms(), 5000), "authority request");
    auto granted = settle(cluster, link);
    Bytes genuine;
    for (const auto& frame : cluster.network.gateway_tap)
        if (body_kind(frame) == static_cast<std::uint8_t>(ResponseKind::authority_granted)) genuine = frame;
    require(!genuine.empty(), "no grant frame on the wire");
    const auto body = std::span<const std::byte>(genuine).subspan(kAuthHeaderBytes, kResponseAuthorityGrantedBytes);

    // A second request; every forged answer to it must be refused.
    require(link.request_authority(*barrier, steady_ms(), 400), "second authority request");
    const auto pending_body = [&] {
        std::array<std::byte, kGatewayAuthorityRequestBytes> bytes{};
        (void)encode_gateway_request({GatewayRequestKind::authority, incarnation, 3, *barrier}, bytes);
        return bytes;
    }();
    const auto pending_correlation = gateway_correlation(pending_body);  // link ids: 1 state, 2, 3
    cluster.network.gateway_ingress = [](std::int32_t, std::span<const std::byte>) { return false; };
    const auto before = link.counters();
    auto& inbox = cluster.network.responses[kGatewayKey];
    inbox.push_back(genuine);  // replay: stale sequence
    ResponseCorrelation ignored;
    AuthorityProofView view;
    require(decode_authority_granted(body, ignored, view) == NodeProtocolError::none, "captured grant decodes");
    std::array<std::byte, kResponseAuthorityGrantedBytes> forged{};
    require(encode_authority_granted(pending_correlation, view, forged).error == NodeProtocolError::none, "forge");
    inbox.push_back(seal_response(kWrongSeed, leader, 1'000'000, forged));  // right bytes, wrong key
    auto other = view;
    other.request.request_sequence += 1;  // authentic key, correlated, but a different barrier
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

    // Stale after a kill-epoch advance: the genuine proof must not survive it.
    // (A new session: the forgeries above advanced this link's fence past the node.)
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
    lossy.loss_permille = 1000;  // accepted, but no report ever arrives
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

// A new process cannot know whether a prior one sent an unsettled effect.
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

// A gateway serves one catalog row; an effect bound to another is never sent.
void effect_for_another_catalog_row_is_not_sent() {
    Base base("v2-gw-row");
    Cluster cluster(base.path, {1, 2});
    Caller op{kOperatorKey, kOperator}, strategy{kStrategyKey, kStrategy};
    prepare_cluster(cluster, op, strategy);
    auto config = Gateway::config();
    config.account = AccountId{6};  // the fixture effect binds account 5
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
    // Notional 100 micros/tick-lot x 11 ticks x 2 lots = 2,200 micros; 115 bp
    // rounds up to 26 micros.
    const auto& metrics = gateway.loop.metrics();
    require(metrics.fee_micros_total == 26, "fees " + std::to_string(metrics.fee_micros_total) + " micros, expected 26");
    require(gateway.paper_venue->balances().fees_paid.atoms() == 26, "the venue and the gateway disagree on fees");
    require(metrics.fills_buy == 1 && !gateway.loop.new_risk_stopped(), "paper fill metrics");
    // Volume is the fill's notional at its own price and quantity: 100 x 11 x 2.
    require(metrics.volume_micros_buy == 100 * 11 * kOrderLots && metrics.volume_micros_sell == 0,
            "volume " + std::to_string(metrics.volume_micros_buy) + "/" + std::to_string(metrics.volume_micros_sell) +
                " micros, expected 2200 bought and 0 sold");
    require(metrics.last_fill_timestamp_seconds > 0, "last fill timestamp not set");
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"node_refuses_wrong_caller_follower_and_unconfigured_gateway",
         node_refuses_wrong_caller_follower_and_unconfigured_gateway},
        {"authorized_order_is_delivered_once_and_its_fill_moves_the_position",
         authorized_order_is_delivered_once_and_its_fill_moves_the_position},
        {"forged_replayed_mismatched_and_stale_proofs_are_refused", forged_replayed_mismatched_and_stale_proofs_are_refused},
        {"lost_and_duplicated_responses_still_deliver_exactly_once", lost_and_duplicated_responses_still_deliver_exactly_once},
        {"leader_change_during_authority_request_sends_nothing", leader_change_during_authority_request_sends_nothing},
        {"unknown_outcome_stops_new_risk_and_is_committed", unknown_outcome_stops_new_risk_and_is_committed},
        {"gateway_started_with_an_unsettled_effect_never_sends_it", gateway_started_with_an_unsettled_effect_never_sends_it},
        {"effect_for_another_catalog_row_is_not_sent", effect_for_another_catalog_row_is_not_sent},
        {"paper_venue_fill_books_position_and_measured_fee", paper_venue_fill_books_position_and_measured_fee},
    };
    const char* only = std::getenv("GATEWAY_TEST");
    int failures = 0;
    for (const auto& [name, test] : tests) {
        if (only && std::string(only) != name) continue;
        try {
            test();
            std::cout << "PASS " << name << std::endl;
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "FAIL " << name << ": " << error.what() << std::endl;
        }
    }
    return failures == 0 ? 0 : 1;
}
