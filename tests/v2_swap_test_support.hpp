#pragma once
#include "v2_shadow_catalog_rows.hpp"
#include "velocita/v2/swap_kernel.hpp"
namespace swap_test {
using namespace velocita::v2;
inline Digest256 digest(unsigned n) { Digest256 d; d.bytes[0] = static_cast<std::byte>(n); return d; }
inline swap::Initialize opening() {
    swap::Initialize o;
    o.binding = {digest(1), shadow_catalog::truemarkets_two_row_catalog().root(), digest(3),
        AccountId{2}, InstrumentId{2}, PolicyId{901}, PolicyVersion{1}, RiskPolicyVersion{1}};
    o.limits = {swap::QuoteAtoms{5'000'000}, swap::QuoteAtoms{2'000'000}, swap::QuoteAtoms{10'000},
        swap::QuoteAtoms{500'000}, swap::BaseAtoms{1'000'000'000}, swap::BaseAtoms{3'000'000},
        swap::QuoteAtoms{125'000'000}, swap::QuoteAtoms{50'000'000}, 10'000, 20'000};
    o.quote = swap::QuoteAtoms{5'000'000}; o.funding_evidence = digest(4);
    o.kill_epoch = 1; o.executor_generation = 1; return o;
}
inline swap::Intent buy(const swap::Initialize& o, std::uint64_t effect = 1) {
    swap::Intent i; i.binding = o.binding; i.effect = EffectId{effect}; i.side = Side::buy;
    i.quote_input = swap::QuoteAtoms{1'000'000}; i.max_quote_debit = swap::QuoteAtoms{1'002'000};
    i.fee_cap = swap::QuoteAtoms{2'000}; i.min_base_receive = swap::BaseAtoms{9'000'000};
    i.source_time = 100; i.sign_by = 20'100; i.kill_epoch = 1; i.executor_generation = 1; return i;
}
inline Command command(std::uint64_t caller, std::uint64_t seq, swap::Command c, std::uint64_t time = 100) {
    swap::Record r{swap::required_role(c), time, std::move(c)};
    return {{CallerId{caller}, RequestSequence{seq}, swap_record_digest(r)}, ApplySpotSwap{r}};
}
}
