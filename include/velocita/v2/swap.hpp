#pragma once

#include "velocita/v2/contracts.hpp"

#include <cstdint>
#include <type_traits>
#include <variant>

namespace velocita::v2::swap {

struct BaseAtomTag;
struct QuoteAtomTag;
using BaseAtoms = StrongId<BaseAtomTag>;
using QuoteAtoms = StrongId<QuoteAtomTag>;

enum class Role : std::uint8_t { operator_role = 1, strategy, gateway };
enum class Mode : std::uint8_t { normal = 1, reduce_only, halted };
enum class Phase : std::uint8_t { none, reserved, prepared, attempted, unknown, conflict };
enum class Outcome : std::uint8_t { succeeded = 1, failed_without_charge };
enum class Code : std::uint8_t {
    applied, duplicate, invalid, unauthorized, uninitialized, already_initialized,
    stale, expired, fenced, busy, stopped, insufficient_funds, purchase_cap,
    unsafe_bound, overflow, wrong_effect, bad_transition, conflict, capacity
};

struct Binding {
    Digest256 incarnation{}, catalog{}, custody{};
    AccountId account{};
    InstrumentId instrument{};
    PolicyId policy{};
    PolicyVersion policy_version{};
    RiskPolicyVersion risk_policy_version{};
    constexpr bool operator==(const Binding&) const noexcept = default;
};

struct Limits {
    QuoteAtoms max_purchase_total{}, max_buy_debit{}, max_fee{}, max_realized_loss{};
    BaseAtoms max_sell_input{}, max_transient_base{};
    QuoteAtoms max_buy_price_per_sol{}, min_sell_price_per_sol{};
    std::uint64_t max_source_age{}, max_sign_lifetime{};
    constexpr bool operator==(const Limits&) const noexcept = default;
};

struct Initialize {
    Binding binding{};
    Limits limits{};
    BaseAtoms base{};
    QuoteAtoms quote{}, base_cost{};
    Digest256 funding_evidence{};
    std::uint64_t kill_epoch{}, executor_generation{};
    constexpr bool operator==(const Initialize&) const noexcept = default;
};

struct Intent {
    Binding binding{};
    EffectId effect{};
    Side side{};
    QuoteAtoms quote_input{}, max_quote_debit{}, min_quote_receive{}, fee_cap{};
    BaseAtoms base_input{}, min_base_receive{}, transient_base_hold{};
    std::uint64_t source_time{}, sign_by{}, kill_epoch{}, executor_generation{};
    constexpr bool operator==(const Intent&) const noexcept = default;
};

struct Prepare {
    EffectId effect{};
    Digest256 payload{}, handle{};
    std::uint64_t payload_count{};
    constexpr bool operator==(const Prepare&) const noexcept = default;
};

struct Attempt {
    EffectId effect{};
    Digest256 payload{};
    constexpr bool operator==(const Attempt&) const noexcept = default;
};

struct MarkUnknown {
    EffectId effect{};
    constexpr bool operator==(const MarkUnknown&) const noexcept = default;
};

struct ReleaseUnsent {
    EffectId effect{};
    Digest256 evidence{};
    constexpr bool operator==(const ReleaseUnsent&) const noexcept = default;
};

struct Settlement {
    EffectId effect{};
    Digest256 custody{}, payload{}, transaction{}, evidence{};
    Outcome outcome{};
    BaseAtoms base_debit{}, base_credit{};
    QuoteAtoms quote_debit{}, quote_credit{}, fee{};
    bool finalized{};
    constexpr bool operator==(const Settlement&) const noexcept = default;
};

struct Control {
    Mode mode{};
    std::uint64_t kill_epoch{}, executor_generation{};
    constexpr bool operator==(const Control&) const noexcept = default;
};

struct AmendBuyPrice {
    Digest256 opening_digest{}, review_evidence{};
    QuoteAtoms expected_price{}, replacement_price{};
    std::uint64_t expected_revision{}, kill_epoch{}, executor_generation{};
    constexpr bool operator==(const AmendBuyPrice&) const noexcept = default;
};

struct BuyPriceAmendment {
    QuoteAtoms price{};
    Digest256 evidence{};
    std::uint64_t kill_epoch{}, executor_generation{}, event_time{};
    constexpr bool operator==(const BuyPriceAmendment&) const noexcept = default;
};

using Command = std::variant<Initialize, Intent, Prepare, Attempt, MarkUnknown,
                             ReleaseUnsent, Settlement, Control, AmendBuyPrice>;

struct Pending {
    Phase phase{};
    Intent intent{};
    Digest256 payload{}, handle{};
    constexpr bool operator==(const Pending&) const noexcept = default;
};

struct State {
    bool initialized{}, conflicted{};
    Initialize opening{};
    BaseAtoms base{}, reserved_base{};
    QuoteAtoms quote{}, reserved_quote{}, cost{}, fees{}, purchases{};
    std::int64_t realized_pnl{};
    Mode mode{Mode::halted};
    std::uint64_t kill_epoch{}, executor_generation{}, event_time{}, last_effect{};
    Pending pending{};
    Settlement conflict_evidence{};
    ReleaseUnsent last_release{};
    std::array<Settlement, 16> settlements{};
    std::uint8_t settlement_count{};
    std::uint32_t settled_total{};
    BuyPriceAmendment buy_price_amendment{};
    constexpr bool operator==(const State&) const noexcept = default;
};

struct PaperQuote {
    QuoteAtoms price_per_sol{};
    std::uint32_t fee_ppm{};
    BaseAtoms buy_base_deduction{};
    QuoteAtoms exact_buy_fee{};
    bool use_exact_buy_fee{};
};

struct PaperFill {
    Code code{Code::invalid};
    Settlement settlement{};
};

[[nodiscard]] Digest256 opening_digest(const Initialize&) noexcept;
[[nodiscard]] QuoteAtoms effective_buy_price(const State&) noexcept;

[[nodiscard]] Code apply(State& state, const Command& command, Role role,
                         std::uint64_t event_time) noexcept;
[[nodiscard]] Role required_role(const Command& command) noexcept;
[[nodiscard]] bool valid_state(const State& state) noexcept;
[[nodiscard]] PaperFill simulate(const State& state, PaperQuote quote,
                                  Digest256 transaction, Digest256 evidence) noexcept;

static_assert(std::is_trivially_copyable_v<State>);
static_assert(sizeof(State) <= 4096);
static_assert(sizeof(Command) <= 512);

}
