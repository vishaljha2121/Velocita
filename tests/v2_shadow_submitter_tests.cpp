#include "velocita/v2/shadow_submitter.hpp"
#include "v2_catalog_test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <vector>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

// Shadow-submitter library: the program file and the durable submitter state.
// Expected programs are built by hand here, not by the parser under test.

namespace {

using namespace velocita::v2;
namespace fs = std::filesystem;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string hex(const Digest256& d) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (const auto b : d.bytes) {
        out += digits[std::to_integer<unsigned>(b) >> 4];
        out += digits[std::to_integer<unsigned>(b) & 0xf];
    }
    return out;
}

Digest256 model() {
    Digest256 d;
    d.bytes[0] = std::byte{0x5a};
    d.bytes[31] = std::byte{0x01};
    return d;
}

std::string valid_text() {
    return "# BTC trend, SMA 125 (research/rules.py)\n"
           "catalog.root = " + hex(catalog_test::standard_catalog().root()) + "\n"
           "catalog.account = 5\n"
           "catalog.venue = 9\n"
           "catalog.instrument = 4\n"
           "catalog.instrument_spec_version = 7\n"
           "catalog.venue_capabilities_version = 8\n"
           "policy.id = 21\n"
           "policy.version = 1\n"
           "policy.feature_schema_id = 31\n"
           "policy.risk_version = 1\n"
           "policy.model_digest = " + hex(model()) + "\n"
           "policy.valid_from_ns = 1000\n"
           "policy.valid_until_ns = 1000000000000\n"
           "features.generic = 4\n"
           "rule.1 = 0 gt 0 enter_long\n"
           "rule.2 = 0 le -5 exit\n"
           "rule.3 = 3 ge 250000 hold\n"
           "order.time_in_force = gtc\n"
           "order.post_only = required\n"
           "order.quantity_lots = 1\n"
           "order.max_notional_micros = 10000\n"
           "order.max_market_data_age_ns = 5000000000\n"
           "source.id = 41\n";
}

ShadowProgramFile parse(const std::string& text) {
    return parse_shadow_program(text, catalog_test::standard_catalog());
}

void a_valid_file_parses_to_the_hand_built_program() {
    const auto result = parse(valid_text());
    require(result.error.empty() && result.program, "valid program refused: " + result.error);
    const auto& p = *result.program;
    require(p.policy_id == PolicyId{21} && p.policy_version == PolicyVersion{1} &&
                p.feature_schema_id == FeatureSchemaId{31} && p.risk_policy_version == RiskPolicyVersion{1} &&
                p.model_digest == model() && p.valid_from_ns == 1000 && p.valid_until_ns == 1'000'000'000'000 &&
                p.generic_features == 4,
            "policy identity differs");
    require(p.rules.size() == 3 && p.rules[0].feature == 0 && p.rules[0].op == PredicateOperator::greater_than &&
                p.rules[0].threshold == 0 && p.rules[0].action == ShadowAction::enter_long &&
                p.rules[1].op == PredicateOperator::less_than_or_equal && p.rules[1].threshold == -5 &&
                p.rules[1].action == ShadowAction::exit && p.rules[2].feature == 3 &&
                p.rules[2].op == PredicateOperator::greater_than_or_equal && p.rules[2].threshold == 250'000 &&
                p.rules[2].action == ShadowAction::hold,
            "rules differ");
    require(p.row.account_id == AccountId{5} && p.row.instrument.instrument_id == InstrumentId{4} &&
                p.row.instrument.venue_id == VenueId{9} &&
                p.row.instrument.maximum_price == PriceTicks::from_atoms(100),
            "catalog row was not the catalog's own row");
    require(p.time_in_force == TimeInForce::good_til_cancel && p.post_only == PostOnlyMode::required &&
                p.order_quantity == QuantityLots::from_atoms(1) && p.max_notional == MoneyMicros::from_atoms(10'000) &&
                p.max_market_data_age_ns == 5'000'000'000,
            "order parameters differ");
    require(result.source == SourceId{41}, "source id differs");
}

void bad_files_are_refused_with_the_line() {
    const auto refused = [](const std::string& text, const std::string& what) {
        const auto result = parse(text);
        require(!result.program && !result.error.empty(), what + ": was accepted");
    };
    refused(valid_text() + "surprise = 1\n", "unknown key");
    refused(valid_text() + "policy.id = 22\n", "duplicate key");
    std::string without = valid_text();
    without.erase(without.find("source.id"), std::string("source.id = 41\n").size());
    refused(without, "missing key");
    const auto missing = parse(without);
    require(missing.error.find("source.id") != std::string::npos && missing.error.find("missing") != std::string::npos,
            "a missing key was reported as something else: " + missing.error);
    std::string gap = valid_text();
    gap.replace(gap.find("rule.3"), 6, "rule.4");
    refused(gap, "rule numbering gap");
    refused(std::string(valid_text()).replace(valid_text().find("0 gt 0"), 6, "0 >> 0"), "unknown operator");
    refused(std::string(valid_text()).replace(valid_text().find("enter_long"), 10, "enter_shrt"), "unknown action");
    // Features are 0..3 generic plus 4 = price; 5 does not exist.
    refused(std::string(valid_text()).replace(valid_text().find("rule.3 = 3"), 10, "rule.3 = 5"), "feature out of range");
    refused(std::string(valid_text()).replace(valid_text().find("catalog.instrument = 4"), 22,
                                              "catalog.instrument = 5"), "row not in the catalog");
    std::string root = valid_text();
    root[root.find("catalog.root = ") + 15] = root[root.find("catalog.root = ") + 15] == '0' ? '1' : '0';
    refused(root, "catalog root mismatch");
    refused(std::string(valid_text()).replace(valid_text().find("order.quantity_lots = 1"), 23,
                                              "order.quantity_lots = x"), "non-numeric value");
}

struct TempDir {
    fs::path path;
    TempDir() {
        auto pattern = (fs::temp_directory_path() / "v2-shadow-state-XXXXXX").string();
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        require(::mkdtemp(bytes.data()) != nullptr, "mkdtemp failed");
        path = bytes.data();
    }
    ~TempDir() { std::error_code ignored; fs::remove_all(path, ignored); }
};

const std::array<std::byte, 16> kIncarnationA{std::byte{0xa1}}, kIncarnationB{std::byte{0xb2}};

void state_starts_fresh_then_advances_generation_and_skips_the_reservation() {
    TempDir dir;
    const auto file = dir.path / "submitter.state";
    const auto first = begin_submitter_state(file, kIncarnationA, 1000);
    require(first.error.empty() && first.generation == 1 && first.first_sequence == 1,
            "a fresh state did not start at generation 1, sequence 1: " + first.error);
    const auto second = begin_submitter_state(file, kIncarnationA, 1000);
    // Everything up to the reserved high-water mark may have been used: start past it.
    require(second.error.empty() && second.generation == 2 && second.first_sequence == 1001,
            "a restart did not move to the next generation past the reservation");
    require(extend_reservation(file, kIncarnationA, second.generation, 5000).empty(), "extension failed");
    const auto third = begin_submitter_state(file, kIncarnationA, 1000);
    require(third.generation == 3 && third.first_sequence == 5000, "an extended reservation was not honoured");
}

void a_new_cluster_starts_the_source_again() {
    TempDir dir;
    const auto file = dir.path / "submitter.state";
    (void)begin_submitter_state(file, kIncarnationA, 1000);
    (void)begin_submitter_state(file, kIncarnationA, 1000);
    const auto fresh = begin_submitter_state(file, kIncarnationB, 1000);
    require(fresh.error.empty() && fresh.generation == 1 && fresh.first_sequence == 1,
            "a different cluster incarnation did not restart the source at generation 1");
}

void a_corrupt_state_is_refused_not_reset() {
    TempDir dir;
    const auto file = dir.path / "submitter.state";
    (void)begin_submitter_state(file, kIncarnationA, 1000);
    {
        std::fstream f(file, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(24);
        f.put('\x7f');
    }
    const auto result = begin_submitter_state(file, kIncarnationA, 1000);
    require(!result.error.empty() && result.generation == 0,
            "a corrupt state file was silently replaced (sequences could be reused)");
}

// CallerId 9 is the shadow operator route, 21 the strategy route; 15 is the
// top of the kernel's operator band (kernel.hpp: kMaxOperatorCallerId).
void authority_is_split_between_setup_and_ticks() {
    require(submitter_role_error(SubmitterRole::setup, CallerId{9}).empty(), "setup refused an operator caller");
    require(submitter_role_error(SubmitterRole::setup, CallerId{15}).empty(), "setup refused the top operator id");
    require(!submitter_role_error(SubmitterRole::setup, CallerId{21}).empty(), "setup accepted a strategy caller");
    require(!submitter_role_error(SubmitterRole::setup, CallerId{16}).empty(), "setup accepted caller 16");
    require(submitter_role_error(SubmitterRole::ticks, CallerId{21}).empty(), "ticks refused a strategy caller");
    require(submitter_role_error(SubmitterRole::ticks, CallerId{16}).empty(), "ticks refused caller 16");
    require(!submitter_role_error(SubmitterRole::ticks, CallerId{9}).empty(), "ticks accepted an operator caller");
    require(!submitter_role_error(SubmitterRole::ticks, CallerId{15}).empty(), "ticks accepted caller 15");
    require(!submitter_role_error(SubmitterRole::ticks, CallerId{0}).empty(), "ticks accepted caller 0");
}

void apply_codes_have_their_enumerator_names() {
    require(apply_code_name(ApplyCode::applied) == "applied", "applied");
    require(apply_code_name(ApplyCode::stale_risk_state) == "stale_risk_state", "stale_risk_state");
    require(apply_code_name(ApplyCode::order_slot_occupied) == "order_slot_occupied", "order_slot_occupied");
    require(apply_code_name(ApplyCode::position_limit_exceeded) == "position_limit_exceeded",
            "position_limit_exceeded");
    require(apply_code_name(ApplyCode::insufficient_risk_capacity) == "insufficient_risk_capacity",
            "insufficient_risk_capacity");
    require(apply_code_name(ApplyCode::catalog_binding_mismatch) == "catalog_binding_mismatch",
            "the last enumerator");
    std::set<std::string_view> names;
    for (int code = 0; code <= static_cast<int>(ApplyCode::catalog_binding_mismatch); ++code)
        require(names.insert(apply_code_name(static_cast<ApplyCode>(code))).second,
                "two codes share the name " + std::string(apply_code_name(static_cast<ApplyCode>(code))));
}

void strategy_modes_parse_by_name() {
    require(parse_strategy_mode("normal") == StrategyMode::normal && parse_strategy_mode("halted") == StrategyMode::halted &&
                parse_strategy_mode("no_new_risk") == StrategyMode::no_new_risk &&
                parse_strategy_mode("cancel_only") == StrategyMode::cancel_only &&
                parse_strategy_mode("reduce_only") == StrategyMode::reduce_only,
            "a strategy mode name was not parsed");
    require(!parse_strategy_mode("halt") && !parse_strategy_mode(""), "an unknown mode name was accepted");
}

// The owner's paper settings (docs/PAPER_TRADING_RUNBOOK.md): $25 per order and
// in total, one 25-lot clip of position, long-only.
std::vector<std::string> owner_words() {
    return {"account=1", "instrument=1", "risk_policy_version=1", "expected_risk_version=0", "kill_epoch=1",
            "executor_generation=1", "max_order_lots=25", "max_reserved_lots=25",
            "max_order_notional_micros=25000000", "max_reserved_exposure_micros=25000000",
            "initial_margin_ppm=1000000", "maximum_fee_ppm=15000", "max_position_lots=25", "allow_short=0",
            "reason_code=20260929"};
}

void the_risk_limits_verb_builds_every_field() {
    const auto parsed = parse_risk_limits_verb(owner_words());
    require(parsed.error.empty() && parsed.limits, "the owner's limits were refused: " + parsed.error);
    ConfigureRiskLimits expected;
    expected.account_id = AccountId{1};
    expected.instrument_id = InstrumentId{1};
    expected.risk_policy_version = RiskPolicyVersion{1};
    expected.expected_risk_version = RiskVersion{0};
    expected.expected_kill_epoch = KillEpoch{1};
    expected.expected_executor_generation = ExecutorGeneration{1};
    expected.max_order_quantity = QuantityLots::from_atoms(25);
    expected.max_reserved_quantity = QuantityLots::from_atoms(25);
    expected.max_order_gross_notional = MoneyMicros::from_atoms(25'000'000);
    expected.max_reserved_exposure = MoneyMicros::from_atoms(25'000'000);
    expected.initial_margin_ppm = 1'000'000;
    expected.maximum_fee_ppm = 15'000;
    expected.max_position_quantity = QuantityLots::from_atoms(25);
    expected.allow_short = false;
    expected.reason_code = 20'260'929;
    const auto& l = *parsed.limits;
    require(l.account_id == expected.account_id && l.instrument_id == expected.instrument_id &&
                l.risk_policy_version == expected.risk_policy_version &&
                l.expected_risk_version == expected.expected_risk_version &&
                l.expected_kill_epoch == expected.expected_kill_epoch &&
                l.expected_executor_generation == expected.expected_executor_generation &&
                l.max_order_quantity == expected.max_order_quantity &&
                l.max_reserved_quantity == expected.max_reserved_quantity &&
                l.max_order_gross_notional == expected.max_order_gross_notional &&
                l.max_reserved_exposure == expected.max_reserved_exposure &&
                l.initial_margin_ppm == expected.initial_margin_ppm && l.maximum_fee_ppm == expected.maximum_fee_ppm &&
                l.max_position_quantity == expected.max_position_quantity && l.allow_short == expected.allow_short &&
                l.reason_code == expected.reason_code,
            "a risk-limit field differs from the words typed");
    auto shorting = owner_words();
    shorting[13] = "allow_short=1";
    require(parse_risk_limits_verb(shorting).limits->allow_short, "allow_short=1 was not carried");
    // Every value distinct, so no field can be read from another's word.
    const auto distinct = parse_risk_limits_verb(std::vector<std::string>{
        "account=2", "instrument=3", "risk_policy_version=4", "expected_risk_version=5", "kill_epoch=6",
        "executor_generation=7", "max_order_lots=8", "max_reserved_lots=9", "max_order_notional_micros=10",
        "max_reserved_exposure_micros=11", "initial_margin_ppm=12", "maximum_fee_ppm=13", "max_position_lots=14",
        "allow_short=1", "reason_code=15"});
    require(distinct.limits.has_value(), "distinct limits refused: " + distinct.error);
    const auto& d = *distinct.limits;
    require(d.account_id == AccountId{2} && d.instrument_id == InstrumentId{3} &&
                d.risk_policy_version == RiskPolicyVersion{4} && d.expected_risk_version == RiskVersion{5} &&
                d.expected_kill_epoch == KillEpoch{6} && d.expected_executor_generation == ExecutorGeneration{7} &&
                d.max_order_quantity == QuantityLots::from_atoms(8) &&
                d.max_reserved_quantity == QuantityLots::from_atoms(9) &&
                d.max_order_gross_notional == MoneyMicros::from_atoms(10) &&
                d.max_reserved_exposure == MoneyMicros::from_atoms(11) && d.initial_margin_ppm == 12 &&
                d.maximum_fee_ppm == 13 && d.max_position_quantity == QuantityLots::from_atoms(14) &&
                d.allow_short && d.reason_code == 15,
            "a field was read from another field's word");
}

void bad_risk_limits_are_refused_before_sending() {
    const auto refused = [](std::vector<std::string> words, const std::string& what, const std::string& names) {
        const auto parsed = parse_risk_limits_verb(words);
        require(!parsed.limits, what + ": was accepted");
        require(parsed.error.find(names) != std::string::npos, what + ": reported as '" + parsed.error + "'");
    };
    auto words = owner_words();
    words.erase(words.begin() + 12);  // max_position_lots
    refused(words, "missing field", "max_position_lots");
    words = owner_words();
    words[14] = "reason_code=0";
    refused(words, "zero reason code", "reason_code");
    words = owner_words();
    words.push_back("account=2");
    refused(words, "duplicate field", "account");
    words = owner_words();
    words.push_back("max_exposure=1");
    refused(words, "unknown field", "max_exposure");
    words = owner_words();
    words[13] = "allow_short=yes";
    refused(words, "non-numeric allow_short", "allow_short");
    words[13] = "allow_short=2";
    refused(words, "non-boolean allow_short", "allow_short");
    words = owner_words();
    words[7] = "max_reserved_lots=10";  // below one order's 25 lots: the kernel's own validity rule
    refused(words, "reserved below one order", "invalid");
    words = owner_words();
    words[12] = "max_position_lots=-1";
    refused(words, "negative position limit", "max_position_lots");
    words = owner_words();
    words[8] = "max_order_notional_micros=9223372036854775808";  // 2^63: would wrap negative as atoms
    refused(words, "notional beyond int64", "max_order_notional_micros");
    words = owner_words();
    words[11] = "maximum_fee_ppm=4294967296";  // 2^32: would truncate to 0 as a uint32
    refused(words, "fee rate beyond uint32", "maximum_fee_ppm");
}

void risk_limits_are_canonical_and_described_in_lots_and_dollars() {
    auto reordered = owner_words();
    std::reverse(reordered.begin(), reordered.end());
    const auto a = parse_risk_limits_verb(owner_words()), b = parse_risk_limits_verb(reordered);
    require(risk_limits_canonical(*a.limits) == risk_limits_canonical(*b.limits),
            "the same limits in another order are a different command");
    auto changed = owner_words();
    changed[11] = "maximum_fee_ppm=15001";
    require(risk_limits_canonical(*parse_risk_limits_verb(changed).limits) != risk_limits_canonical(*a.limits),
            "the canonical text does not bind the fee rate");
    const auto text = describe_risk_limits(*a.limits);
    for (const auto* needle : {"25 lots", "$25.000000", "1.5000 %", "100.0000 %", "long-only", "20260929"})
        require(text.find(needle) != std::string::npos, std::string("summary lacks '") + needle + "':\n" + text);
}

}  // namespace

// A volume program as the edge writes it (model/.../shadow/volume.py, churn):
// five generic features, rules on feature 4, the price at index 5.
void a_five_feature_volume_program_parses() {
    std::string text = valid_text();
    text.replace(text.find("policy.id = 21"), 14, "policy.id = 22");
    text.replace(text.find("feature_schema_id = 31"), 22, "feature_schema_id = 32");
    text.replace(text.find("features.generic = 4"), 20, "features.generic = 5");
    text.replace(text.find("rule.1"), text.find("order.time_in_force") - text.find("rule.1"),
                 "rule.1 = 4 gt 0 enter_long\nrule.2 = 4 le 0 exit\n");
    const auto result = parse(text);
    require(result.program.has_value(), "the churn program was refused: " + result.error);
    const auto& p = *result.program;
    require(p.policy_id == PolicyId{22} && p.feature_schema_id == FeatureSchemaId{32} && p.generic_features == 5 &&
                p.rules.size() == 2 && p.rules[0].feature == 4 && p.rules[0].action == ShadowAction::enter_long &&
                p.rules[1].feature == 4 && p.rules[1].op == PredicateOperator::less_than_or_equal &&
                p.rules[1].action == ShadowAction::exit,
            "the churn program differs from the text");
    std::string beyond = text;
    beyond.replace(beyond.find("rule.2 = 4"), 10, "rule.2 = 6");  // 0..4 generic, 5 = price, 6 does not exist
    require(!parse(beyond).program, "a rule on a feature past the price was accepted");
}

void the_exits_only_token_is_zero_one_or_a_bad_tick() {
    require(parse_exits_only(std::nullopt) == std::optional<bool>(false), "an absent token stopped buys");
    require(parse_exits_only(std::string_view("0")) == std::optional<bool>(false), "0 stopped buys");
    require(parse_exits_only(std::string_view("1")) == std::optional<bool>(true), "1 did not stop buys");
    for (const std::string_view bad : {"", "2", "true", "01", " 1"})
        require(!parse_exits_only(bad).has_value(), "exits_only='" + std::string(bad) + "' was guessed");
}

int main() {
    try {
        a_valid_file_parses_to_the_hand_built_program();
        bad_files_are_refused_with_the_line();
        state_starts_fresh_then_advances_generation_and_skips_the_reservation();
        a_new_cluster_starts_the_source_again();
        a_corrupt_state_is_refused_not_reset();
        authority_is_split_between_setup_and_ticks();
        apply_codes_have_their_enumerator_names();
        strategy_modes_parse_by_name();
        the_risk_limits_verb_builds_every_field();
        bad_risk_limits_are_refused_before_sending();
        risk_limits_are_canonical_and_described_in_lots_and_dollars();
        the_exits_only_token_is_zero_one_or_a_bad_tick();
        a_five_feature_volume_program_parses();
        std::cout << "shadow submitter tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "shadow submitter tests failed: " << error.what() << '\n';
        return 1;
    }
}
