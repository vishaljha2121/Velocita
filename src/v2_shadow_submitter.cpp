#include "velocita/v2/shadow_submitter.hpp"

#include "velocita/v2/digest.hpp"
#include "velocita/v2/wire.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace velocita::v2 {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

template <typename T>
bool number(std::string_view text, T& out) {
    const auto* end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, out);
    return parsed.ec == std::errc{} && parsed.ptr == end && !text.empty();
}

bool digest(std::string_view text, Digest256& out) {
    if (text.size() != 64) return false;
    for (std::size_t i = 0; i < 32; ++i) {
        unsigned value = 0;
        const auto parsed = std::from_chars(text.data() + 2 * i, text.data() + 2 * i + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + 2 * i + 2) return false;
        out.bytes[i] = static_cast<std::byte>(value);
    }
    return true;
}

const std::set<std::string_view> kKeys{
    "catalog.root", "catalog.account", "catalog.venue", "catalog.instrument", "catalog.instrument_spec_version",
    "catalog.venue_capabilities_version", "policy.id", "policy.version", "policy.feature_schema_id",
    "policy.risk_version", "policy.model_digest", "policy.valid_from_ns", "policy.valid_until_ns",
    "features.generic", "order.time_in_force", "order.post_only", "order.quantity_lots",
    "order.max_notional_micros", "order.max_market_data_age_ns", "source.id"};

std::optional<PredicateOperator> op(std::string_view s) {
    if (s == "lt") return PredicateOperator::less_than;
    if (s == "le") return PredicateOperator::less_than_or_equal;
    if (s == "eq") return PredicateOperator::equal;
    if (s == "ge") return PredicateOperator::greater_than_or_equal;
    if (s == "gt") return PredicateOperator::greater_than;
    return std::nullopt;
}

std::optional<ShadowAction> action(std::string_view s) {
    if (s == "enter_long") return ShadowAction::enter_long;
    if (s == "exit") return ShadowAction::exit;
    if (s == "hold") return ShadowAction::hold;
    return std::nullopt;
}

}  // namespace

ShadowProgramFile parse_shadow_program(std::string_view text, const VerifiedExecutionCatalog& catalog) {
    ShadowProgramFile out;
    std::map<std::string, std::string> values;
    std::map<std::uint64_t, std::string> rules;
    std::size_t line_number = 0;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        const auto raw = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        ++line_number;
        const auto line = trim(raw);
        if (line.empty() || line.front() == '#') continue;
        const auto eq = line.find('=');
        const auto fail = [&](const std::string& why) {
            out.error = "line " + std::to_string(line_number) + ": " + why;
            return out;
        };
        if (eq == std::string_view::npos) return fail("expected key = value");
        const auto key = std::string(trim(line.substr(0, eq)));
        const auto value = std::string(trim(line.substr(eq + 1)));
        if (key.rfind("rule.", 0) == 0) {
            std::uint64_t index = 0;
            if (!number(std::string_view(key).substr(5), index) || index == 0) return fail("bad rule number");
            if (!rules.emplace(index, value).second) return fail("duplicate " + key);
            continue;
        }
        if (!kKeys.contains(key)) return fail("unknown key " + key);
        if (!values.emplace(key, value).second) return fail("duplicate " + key);
    }
    for (const auto key : kKeys)
        if (!values.contains(std::string(key))) {
            out.error = "key " + std::string(key) + ": missing";
            return out;
        }
    if (rules.empty() || rules.size() > kMaxPolicyRules) {
        out.error = "rules: need 1.." + std::to_string(kMaxPolicyRules);
        return out;
    }

    const auto bad = [&](const std::string& key) {
        out.error = "key " + key + ": invalid value '" + values[key] + "'";
        return out;
    };
    ShadowProgram p;
    Digest256 root;
    std::uint64_t account = 0, venue = 0, instrument = 0, spec = 0, caps = 0, id = 0, version = 0, schema = 0,
                  risk = 0, source = 0, generic = 0, quantity = 0;
    std::int64_t notional = 0;
    if (!digest(values["catalog.root"], root)) return bad("catalog.root");
    if (!number(values["catalog.account"], account)) return bad("catalog.account");
    if (!number(values["catalog.venue"], venue)) return bad("catalog.venue");
    if (!number(values["catalog.instrument"], instrument)) return bad("catalog.instrument");
    if (!number(values["catalog.instrument_spec_version"], spec)) return bad("catalog.instrument_spec_version");
    if (!number(values["catalog.venue_capabilities_version"], caps)) return bad("catalog.venue_capabilities_version");
    if (!number(values["policy.id"], id)) return bad("policy.id");
    if (!number(values["policy.version"], version)) return bad("policy.version");
    if (!number(values["policy.feature_schema_id"], schema)) return bad("policy.feature_schema_id");
    if (!number(values["policy.risk_version"], risk)) return bad("policy.risk_version");
    if (!digest(values["policy.model_digest"], p.model_digest)) return bad("policy.model_digest");
    if (!number(values["policy.valid_from_ns"], p.valid_from_ns)) return bad("policy.valid_from_ns");
    if (!number(values["policy.valid_until_ns"], p.valid_until_ns)) return bad("policy.valid_until_ns");
    if (!number(values["features.generic"], generic) || generic + 1 > kMaxDecisionFeatures)
        return bad("features.generic");
    if (!number(values["order.quantity_lots"], quantity)) return bad("order.quantity_lots");
    if (!number(values["order.max_notional_micros"], notional)) return bad("order.max_notional_micros");
    if (!number(values["order.max_market_data_age_ns"], p.max_market_data_age_ns))
        return bad("order.max_market_data_age_ns");
    if (!number(values["source.id"], source) || source == 0) return bad("source.id");
    const auto& tif = values["order.time_in_force"];
    if (tif == "gtc") p.time_in_force = TimeInForce::good_til_cancel;
    else if (tif == "ioc") p.time_in_force = TimeInForce::immediate_or_cancel;
    else if (tif == "fok") p.time_in_force = TimeInForce::fill_or_kill;
    else if (tif == "post_only") p.time_in_force = TimeInForce::post_only;
    else return bad("order.time_in_force");
    const auto& post = values["order.post_only"];
    if (post == "required") p.post_only = PostOnlyMode::required;
    else if (post == "prohibited") p.post_only = PostOnlyMode::prohibited;
    else return bad("order.post_only");

    if (catalog.root() != root) {
        out.error = "key catalog.root: does not match the verified catalog";
        return out;
    }
    const auto* row = catalog.lookup(AccountId{account}, VenueId{venue}, InstrumentId{instrument},
                                     InstrumentSpecVersion{spec}, VenueCapabilitiesVersion{caps});
    if (row == nullptr) {
        out.error = "catalog: no row for the account, venue, instrument and versions given";
        return out;
    }

    std::uint64_t expected = 1;
    for (const auto& [index, spec_text] : rules) {
        if (index != expected++) {
            out.error = "rules: numbering must be contiguous from rule.1";
            return out;
        }
        std::istringstream in(spec_text);
        std::string feature_text, op_text, threshold_text, action_text, extra;
        in >> feature_text >> op_text >> threshold_text >> action_text;
        ShadowRule rule;
        const auto parsed_op = op(op_text);
        const auto parsed_action = action(action_text);
        if (!number(feature_text, rule.feature) || rule.feature > generic || !parsed_op ||
            !number(threshold_text, rule.threshold) || !parsed_action || (in >> extra)) {
            out.error = "rule." + std::to_string(index) + ": expected FEATURE OP THRESHOLD ACTION";
            return out;
        }
        rule.op = *parsed_op;
        rule.action = *parsed_action;
        p.rules.push_back(rule);
    }

    p.policy_id = PolicyId{id};
    p.policy_version = PolicyVersion{version};
    p.feature_schema_id = FeatureSchemaId{schema};
    p.risk_policy_version = RiskPolicyVersion{risk};
    p.generic_features = static_cast<std::uint16_t>(generic);
    p.row = *row;
    p.order_quantity = QuantityLots::from_atoms(static_cast<std::int64_t>(quantity));
    p.max_notional = MoneyMicros::from_atoms(notional);
    out.program = std::move(p);
    out.source = SourceId{source};
    return out;
}

// -- durable submitter state -------------------------------------------------

namespace {

constexpr std::array<std::byte, 4> kMagic{std::byte{'V'}, std::byte{'S'}, std::byte{'B'}, std::byte{'1'}};
constexpr std::size_t kStateBytes = 4 + 16 + 8 + 8 + 4;

struct State {
    std::array<std::byte, 16> incarnation{};
    std::uint64_t generation{};
    std::uint64_t reserved{};
};

void put(std::array<std::byte, kStateBytes>& b, std::size_t at, std::uint64_t v) {
    for (std::size_t i = 0; i < 8; ++i) b[at + i] = std::byte((v >> (8 * i)) & 0xff);
}
std::uint64_t get(const std::array<std::byte, kStateBytes>& b, std::size_t at) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8; ++i) v |= std::uint64_t(std::to_integer<unsigned>(b[at + i])) << (8 * i);
    return v;
}

std::array<std::byte, kStateBytes> encode(const State& s) {
    std::array<std::byte, kStateBytes> b{};
    std::copy(kMagic.begin(), kMagic.end(), b.begin());
    std::copy(s.incarnation.begin(), s.incarnation.end(), b.begin() + 4);
    put(b, 20, s.generation);
    put(b, 28, s.reserved);
    const auto crc = crc32_checksum(std::span(b).first(36));
    for (std::size_t i = 0; i < 4; ++i) b[36 + i] = std::byte((crc >> (8 * i)) & 0xff);
    return b;
}

bool write_all(int fd, std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        const auto n = ::write(fd, bytes.data(), bytes.size());
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        bytes = bytes.subspan(static_cast<std::size_t>(n));
    }
    return true;
}

bool sync(int fd) {
    int r;
    do { r = ::fsync(fd); } while (r < 0 && errno == EINTR);
    return r == 0;
}

// Temporary file, fsync, rename, fsync the directory: the new state exists
// only once its directory entry is durable.
std::string store(const std::filesystem::path& file, const State& s) {
    const auto parent = file.parent_path().empty() ? std::filesystem::path(".") : file.parent_path();
    const int dir = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) return "state directory unavailable";
    const auto name = file.filename().string(), temporary = name + ".tmp";
    const int out = ::openat(dir, temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    const auto bytes = encode(s);
    const bool ok = out >= 0 && ::fchmod(out, 0600) == 0 && write_all(out, bytes) && sync(out) && ::close(out) == 0 &&
                    ::renameat(dir, temporary.c_str(), dir, name.c_str()) == 0 && sync(dir);
    ::close(dir);
    return ok ? "" : "state write failed";
}

// nullopt + empty error: no file. nullopt + error: present but unusable.
std::optional<State> load(const std::filesystem::path& file, std::string& error) {
    const int in = ::open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0) {
        if (errno != ENOENT) error = "state unreadable";
        return std::nullopt;
    }
    std::array<std::byte, kStateBytes + 1> raw{};
    std::size_t length = 0;
    while (length < raw.size()) {
        const auto n = ::read(in, raw.data() + length, raw.size() - length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        length += static_cast<std::size_t>(n);
    }
    ::close(in);
    std::array<std::byte, kStateBytes> b{};
    std::copy(raw.begin(), raw.begin() + kStateBytes, b.begin());
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < 4; ++i) crc |= std::uint32_t(std::to_integer<unsigned>(b[36 + i])) << (8 * i);
    if (length != kStateBytes || !std::equal(kMagic.begin(), kMagic.end(), b.begin()) ||
        crc != crc32_checksum(std::span(b).first(36))) {
        error = "state file is torn or corrupt; refusing to reuse request sequences";
        return std::nullopt;
    }
    State s;
    std::copy(b.begin() + 4, b.begin() + 20, s.incarnation.begin());
    s.generation = get(b, 20);
    s.reserved = get(b, 28);
    if (s.generation == 0 || s.reserved == 0) {
        error = "state file holds an impossible generation or reservation";
        return std::nullopt;
    }
    return s;
}

}  // namespace

SubmitterStart begin_submitter_state(const std::filesystem::path& file, const std::array<std::byte, 16>& incarnation,
                                     std::uint64_t reserve) {
    std::string error;
    const auto previous = load(file, error);
    if (!error.empty()) return {error, 0, 0};
    State next;
    next.incarnation = incarnation;
    std::uint64_t first = 1;
    if (previous && previous->incarnation == incarnation) {
        next.generation = previous->generation + 1;
        first = previous->reserved;
    } else {
        next.generation = 1;
    }
    next.reserved = first + reserve;
    if (auto stored = store(file, next); !stored.empty()) return {stored, 0, 0};
    return {"", next.generation, first};
}

std::string extend_reservation(const std::filesystem::path& file, const std::array<std::byte, 16>& incarnation,
                               std::uint64_t generation, std::uint64_t reserved_through) {
    return store(file, State{incarnation, generation, reserved_through});
}

std::optional<bool> parse_exits_only(const std::optional<std::string_view>& value) noexcept {
    if (!value || *value == "0") return false;
    if (*value == "1") return true;
    return std::nullopt;
}

// -- split authority and operator verbs --------------------------------------

std::string submitter_role_error(SubmitterRole role, CallerId caller) {
    if (caller.value() == 0) return "caller id must be non-zero";
    if (role == SubmitterRole::setup && !is_operator_caller(caller))
        return "setup changes strategy mode: it needs an operator caller (1.." + std::to_string(kMaxOperatorCallerId) + ")";
    if (role == SubmitterRole::ticks && is_operator_caller(caller))
        return "the tick loop must not hold operator authority: use a strategy caller above " +
               std::to_string(kMaxOperatorCallerId);
    return "";
}

// A code appended to ApplyCode must be named here: -Wswitch is an error.
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
std::string_view apply_code_name(ApplyCode code) noexcept {
    switch (code) {
        case ApplyCode::applied: return "applied";
        case ApplyCode::already_applied: return "already_applied";
        case ApplyCode::invalid_command: return "invalid_command";
        case ApplyCode::invalid_policy_contract: return "invalid_policy_contract";
        case ApplyCode::duplicate_conflict: return "duplicate_conflict";
        case ApplyCode::policy_not_found: return "policy_not_found";
        case ApplyCode::immutable_policy_conflict: return "immutable_policy_conflict";
        case ApplyCode::non_monotonic_policy_version: return "non_monotonic_policy_version";
        case ApplyCode::invalid_policy_transition: return "invalid_policy_transition";
        case ApplyCode::active_policy_scope_conflict: return "active_policy_scope_conflict";
        case ApplyCode::stale_control_generation: return "stale_control_generation";
        case ApplyCode::non_monotonic_kill_epoch: return "non_monotonic_kill_epoch";
        case ApplyCode::non_monotonic_executor_generation: return "non_monotonic_executor_generation";
        case ApplyCode::source_event_conflict: return "source_event_conflict";
        case ApplyCode::source_generation_gap: return "source_generation_gap";
        case ApplyCode::source_generation_regression: return "source_generation_regression";
        case ApplyCode::source_sequence_gap: return "source_sequence_gap";
        case ApplyCode::source_sequence_regression: return "source_sequence_regression";
        case ApplyCode::source_capacity_exceeded: return "source_capacity_exceeded";
        case ApplyCode::source_event_capacity_exceeded: return "source_event_capacity_exceeded";
        case ApplyCode::decision_capacity_exceeded: return "decision_capacity_exceeded";
        case ApplyCode::policy_capacity_exceeded: return "policy_capacity_exceeded";
        case ApplyCode::receipt_capacity_exceeded: return "receipt_capacity_exceeded";
        case ApplyCode::state_version_exhausted: return "state_version_exhausted";
        case ApplyCode::decision_id_conflict: return "decision_id_conflict";
        case ApplyCode::invalid_feature_schema_contract: return "invalid_feature_schema_contract";
        case ApplyCode::invalid_executable_policy_contract: return "invalid_executable_policy_contract";
        case ApplyCode::artifact_digest_mismatch: return "artifact_digest_mismatch";
        case ApplyCode::executable_policy_binding_failed: return "executable_policy_binding_failed";
        case ApplyCode::invalid_instrument_profile_contract: return "invalid_instrument_profile_contract";
        case ApplyCode::invalid_venue_capabilities_contract: return "invalid_venue_capabilities_contract";
        case ApplyCode::executable_policy_not_staged: return "executable_policy_not_staged";
        case ApplyCode::executable_policy_identity_conflict: return "executable_policy_identity_conflict";
        case ApplyCode::decision_not_authorizable: return "decision_not_authorizable";
        case ApplyCode::risk_account_not_found: return "risk_account_not_found";
        case ApplyCode::stale_risk_state: return "stale_risk_state";
        case ApplyCode::risk_arithmetic_overflow: return "risk_arithmetic_overflow";
        case ApplyCode::insufficient_risk_capacity: return "insufficient_risk_capacity";
        case ApplyCode::reservation_capacity_exceeded: return "reservation_capacity_exceeded";
        case ApplyCode::order_capacity_exceeded: return "order_capacity_exceeded";
        case ApplyCode::outbox_capacity_exceeded: return "outbox_capacity_exceeded";
        case ApplyCode::derived_identity_collision: return "derived_identity_collision";
        case ApplyCode::venue_observation_not_applicable: return "venue_observation_not_applicable";
        case ApplyCode::fill_identity_conflict: return "fill_identity_conflict";
        case ApplyCode::fill_capacity_exceeded: return "fill_capacity_exceeded";
        case ApplyCode::fill_quantity_conflict: return "fill_quantity_conflict";
        case ApplyCode::position_arithmetic_overflow: return "position_arithmetic_overflow";
        case ApplyCode::order_not_found: return "order_not_found";
        case ApplyCode::order_not_cancellable: return "order_not_cancellable";
        case ApplyCode::venue_event_identity_conflict: return "venue_event_identity_conflict";
        case ApplyCode::venue_event_capacity_exceeded: return "venue_event_capacity_exceeded";
        case ApplyCode::replacement_not_authorizable: return "replacement_not_authorizable";
        case ApplyCode::order_not_replaceable: return "order_not_replaceable";
        case ApplyCode::replacement_not_supported: return "replacement_not_supported";
        case ApplyCode::order_slot_occupied: return "order_slot_occupied";
        case ApplyCode::risk_account_capacity_exceeded: return "risk_account_capacity_exceeded";
        case ApplyCode::risk_version_exhausted: return "risk_version_exhausted";
        case ApplyCode::risk_reconfiguration_blocked: return "risk_reconfiguration_blocked";
        case ApplyCode::request_expired: return "request_expired";
        case ApplyCode::venue_event_expired: return "venue_event_expired";
        case ApplyCode::unauthorized_control: return "unauthorized_control";
        case ApplyCode::reconciliation_required: return "reconciliation_required";
        case ApplyCode::catalog_not_installed: return "catalog_not_installed";
        case ApplyCode::catalog_installation_blocked: return "catalog_installation_blocked";
        case ApplyCode::immutable_catalog_conflict: return "immutable_catalog_conflict";
        case ApplyCode::catalog_row_not_found: return "catalog_row_not_found";
        case ApplyCode::catalog_binding_mismatch: return "catalog_binding_mismatch";
        case ApplyCode::position_limit_exceeded: return "position_limit_exceeded";
    }
    return "unknown";
}
#pragma GCC diagnostic pop

std::optional<StrategyMode> parse_strategy_mode(std::string_view name) noexcept {
    if (name == "normal") return StrategyMode::normal;
    if (name == "no_new_risk") return StrategyMode::no_new_risk;
    if (name == "cancel_only") return StrategyMode::cancel_only;
    if (name == "reduce_only") return StrategyMode::reduce_only;
    if (name == "halted") return StrategyMode::halted;
    return std::nullopt;
}

namespace {

constexpr std::array<std::string_view, 15> kRiskKeys{
    "account", "instrument", "risk_policy_version", "expected_risk_version", "kill_epoch", "executor_generation",
    "max_order_lots", "max_reserved_lots", "max_order_notional_micros", "max_reserved_exposure_micros",
    "initial_margin_ppm", "maximum_fee_ppm", "max_position_lots", "allow_short", "reason_code"};

std::string dollars(MoneyMicros micros) {
    const auto atoms = micros.atoms();
    const auto magnitude = atoms < 0 ? -static_cast<std::uint64_t>(atoms) : static_cast<std::uint64_t>(atoms);
    auto fraction = std::to_string(magnitude % 1'000'000);
    fraction.insert(0, 6 - fraction.size(), '0');
    return std::string(atoms < 0 ? "-$" : "$") + std::to_string(magnitude / 1'000'000) + "." + fraction;
}

std::string percent(std::uint32_t ppm) {
    auto fraction = std::to_string(ppm % 10'000);
    fraction.insert(0, 4 - fraction.size(), '0');
    return std::to_string(ppm / 10'000) + "." + fraction + " %";
}

}  // namespace

RiskLimitsVerb parse_risk_limits_verb(std::span<const std::string> words) {
    RiskLimitsVerb out;
    std::map<std::string, std::string> values;
    for (const auto& word : words) {
        const auto eq = word.find('=');
        const auto key = word.substr(0, eq);
        if (eq == std::string::npos || std::find(kRiskKeys.begin(), kRiskKeys.end(), key) == kRiskKeys.end()) {
            out.error = "unknown field '" + key + "' (expected KEY=VALUE)";
            return out;
        }
        if (!values.emplace(key, word.substr(eq + 1)).second) {
            out.error = "duplicate field " + key;
            return out;
        }
    }
    std::map<std::string, std::uint64_t> n;
    for (const auto key : kRiskKeys) {
        const auto found = values.find(std::string(key));
        if (found == values.end()) {
            out.error = "missing field " + std::string(key);
            return out;
        }
        std::uint64_t value = 0;
        const bool bounded = key == "initial_margin_ppm" || key == "maximum_fee_ppm" || key == "reason_code";
        if (!number(found->second, value) || (bounded && value > std::numeric_limits<std::uint32_t>::max()) ||
            (!bounded && value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))) {
            out.error = "field " + std::string(key) + ": invalid value '" + found->second + "'";
            return out;
        }
        n[std::string(key)] = value;
    }
    if (n["allow_short"] > 1) {
        out.error = "field allow_short: must be 0 (long-only) or 1";
        return out;
    }
    if (n["reason_code"] == 0) {
        out.error = "field reason_code: must be non-zero (the audit reference for this change)";
        return out;
    }
    const auto lots = [&](const char* key) { return QuantityLots::from_atoms(static_cast<std::int64_t>(n[key])); };
    const auto micros = [&](const char* key) { return MoneyMicros::from_atoms(static_cast<std::int64_t>(n[key])); };
    ConfigureRiskLimits l;
    l.account_id = AccountId{n["account"]};
    l.instrument_id = InstrumentId{n["instrument"]};
    l.risk_policy_version = RiskPolicyVersion{n["risk_policy_version"]};
    l.expected_risk_version = RiskVersion{n["expected_risk_version"]};
    l.expected_kill_epoch = KillEpoch{n["kill_epoch"]};
    l.expected_executor_generation = ExecutorGeneration{n["executor_generation"]};
    l.max_order_quantity = lots("max_order_lots");
    l.max_reserved_quantity = lots("max_reserved_lots");
    l.max_order_gross_notional = micros("max_order_notional_micros");
    l.max_reserved_exposure = micros("max_reserved_exposure_micros");
    l.initial_margin_ppm = static_cast<std::uint32_t>(n["initial_margin_ppm"]);
    l.maximum_fee_ppm = static_cast<std::uint32_t>(n["maximum_fee_ppm"]);
    l.max_position_quantity = lots("max_position_lots");
    l.allow_short = n["allow_short"] == 1;
    l.reason_code = static_cast<std::uint32_t>(n["reason_code"]);
    if (!valid_risk_limits(l)) {
        out.error = "limits invalid: non-zero account, instrument, risk policy version, order lots, notional and "
                    "exposure; reserved lots >= order lots; margin 1..1000000 ppm; fee <= 1000000 ppm";
        return out;
    }
    out.limits = l;
    return out;
}

std::string risk_limits_canonical(const ConfigureRiskLimits& l) {
    std::ostringstream out;
    out << "account=" << l.account_id.value() << " instrument=" << l.instrument_id.value()
        << " risk_policy_version=" << l.risk_policy_version.value()
        << " expected_risk_version=" << l.expected_risk_version.value() << " kill_epoch=" << l.expected_kill_epoch.value()
        << " executor_generation=" << l.expected_executor_generation.value()
        << " max_order_lots=" << l.max_order_quantity.atoms() << " max_reserved_lots=" << l.max_reserved_quantity.atoms()
        << " max_order_notional_micros=" << l.max_order_gross_notional.atoms()
        << " max_reserved_exposure_micros=" << l.max_reserved_exposure.atoms()
        << " initial_margin_ppm=" << l.initial_margin_ppm << " maximum_fee_ppm=" << l.maximum_fee_ppm
        << " max_position_lots=" << l.max_position_quantity.atoms() << " allow_short=" << (l.allow_short ? 1 : 0)
        << " reason_code=" << l.reason_code;
    return out.str();
}

std::string describe_risk_limits(const ConfigureRiskLimits& l) {
    std::ostringstream out;
    out << "RISK-LIMITS account " << l.account_id.value() << ", instrument " << l.instrument_id.value()
        << ", risk policy version " << l.risk_policy_version.value() << ", replacing risk version "
        << l.expected_risk_version.value() << (l.expected_risk_version.value() == 0 ? " (new account)" : "")
        << ", kill epoch " << l.expected_kill_epoch.value() << ", executor generation "
        << l.expected_executor_generation.value() << "\n"
        << "  one order:    at most " << l.max_order_quantity.atoms() << " lots and "
        << dollars(l.max_order_gross_notional) << " gross notional\n"
        << "  open orders:  at most " << l.max_reserved_quantity.atoms() << " lots reserved and "
        << dollars(l.max_reserved_exposure) << " held (margin + fee holds)\n"
        << "  position:     |position| at most " << l.max_position_quantity.atoms()
        << " lots after every open order fills"
        << (l.max_position_quantity.atoms() == 0 ? " (zero: no new exposure at all)" : "") << "; "
        << (l.allow_short ? "shorting allowed" : "long-only: a sell may only reduce a long position") << "\n"
        << "  holds:        initial margin " << percent(l.initial_margin_ppm) << ", fee "
        << percent(l.maximum_fee_ppm) << " of notional\n"
        << "  reason code:  " << l.reason_code << " (audit reference; the kernel does not interpret it)\n";
    return out.str();
}

}  // namespace velocita::v2
