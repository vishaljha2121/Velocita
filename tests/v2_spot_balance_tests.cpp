#include "velocita/v2/spot_balance.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace velocita::v2;
void require(bool okay, const char* name) { if (!okay) throw std::runtime_error(name); }
struct Rig {
    ExpectedSpotBalance book;
    SpotBalanceSnapshot snapshot;
    Rig() {
        book.account_and_assets_digest.bytes[0] = std::byte{1};
        book.base_lots = 3; book.base_atoms_per_lot = 10'000'000;
        book.residual_base_atoms = 1'896'934; book.quote_atoms = 1'006'989;
        snapshot.account_and_assets_digest = book.account_and_assets_digest;
        snapshot.received_at_ms = 1'000;
        snapshot.base = {31'896'934, 31'896'934, 0};
        snapshot.quote = {1'006'989, 1'006'989, 0};
    }
    SpotBalanceCheck check() const { return compare_spot_balance(book, snapshot, 1'010, 10); }
};
void residual_is_part_of_inventory() {
    Rig r;
    require(r.check().verdict == SpotBalanceVerdict::matching_snapshot_only, "three lots plus residual matches exact atoms");
    r.book.residual_base_atoms = 0;
    const auto result = r.check();
    require(result.verdict == SpotBalanceVerdict::unexplained_delta && result.base_delta_atoms == 1'896'934,
            "residual cannot disappear through lot rounding");
    r.book.quote_atoms = 25'000'000;
    require(r.check().quote_delta_atoms == -23'993'011, "paper funding cannot substitute for live cash");
}
void snapshot_cannot_resolve_unknown_activity() {
    Rig r; r.book.unresolved_orders = 1;
    require(r.check().verdict == SpotBalanceVerdict::unresolved_activity, "matching balances do not resolve an unknown order");
    r.book.unresolved_orders = 0; r.snapshot.base.held_atoms = 1; --r.snapshot.base.available_atoms;
    require(r.check().verdict == SpotBalanceVerdict::held_funds, "held inventory prevents a flat baseline");
    r.snapshot.base = {31'896'934,31'896'934,0};
    r.snapshot.quote.held_atoms = 1; --r.snapshot.quote.available_atoms;
    require(r.check().verdict == SpotBalanceVerdict::held_funds, "held quote prevents a flat baseline");
}
void identity_and_freshness_are_required() {
    Rig r; r.snapshot.account_and_assets_digest.bytes[1] = std::byte{1};
    require(r.check().verdict == SpotBalanceVerdict::identity_mismatch, "another account or asset binding refused");
    r.snapshot.account_and_assets_digest = r.book.account_and_assets_digest;
    r.snapshot.received_at_ms = 999;
    require(r.check().verdict == SpotBalanceVerdict::stale, "snapshot outside age boundary refused");
    r.snapshot.received_at_ms = 1'011;
    require(r.check().verdict == SpotBalanceVerdict::stale, "future snapshot refused without unsigned underflow");
}
void malformed_and_overflowing_balances_are_refused() {
    Rig r; r.snapshot.base.available_atoms = -1;
    require(r.check().verdict == SpotBalanceVerdict::invalid, "negative amount refused");
    r.snapshot.base = {31'896'934,31'896'934,1};
    require(r.check().verdict == SpotBalanceVerdict::invalid, "total must equal available plus held");
    r.snapshot.base.held_atoms = 0;
    r.book.base_lots = std::numeric_limits<std::int64_t>::max();
    require(r.check().verdict == SpotBalanceVerdict::invalid, "lot multiplication overflow refused");
    r.book.base_lots = 3; r.book.residual_base_atoms = r.book.base_atoms_per_lot;
    require(r.check().verdict == SpotBalanceVerdict::invalid, "residual must be less than one lot");
}
}
int main(int argc, char** argv) {
    const std::pair<const char*, void (*)()> cases[] = {
        {"residual_is_part_of_inventory", residual_is_part_of_inventory},
        {"snapshot_cannot_resolve_unknown_activity", snapshot_cannot_resolve_unknown_activity},
        {"identity_and_freshness_are_required", identity_and_freshness_are_required},
        {"malformed_and_overflowing_balances_are_refused", malformed_and_overflowing_balances_are_refused}};
    unsigned ran = 0, failed = 0;
    for (const auto& [name, test] : cases) {
        if (argc == 2 && std::string_view(argv[1]) != name) continue;
        ++ran;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    return ran && !failed ? 0 : 1;
}
