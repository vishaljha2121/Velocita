#include "velocita/v2/swap_reconcile.hpp"
#include "velocita/v2/swap_codec.hpp"
#include "velocita/v2/digest.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace velocita::v2;
using namespace velocita::v2::swap;
void require(bool yes, const char* name) { if (!yes) throw std::runtime_error(name); }
Digest256 digest(unsigned n) { Digest256 d; d.bytes[0] = static_cast<std::byte>(n); return d; }
std::string row(std::string_view symbol, std::string_view total, std::string_view available,
                std::string_view held = "0", std::string_view chain = "solana") {
    return "{\"symbol\":\"" + std::string(symbol) + "\",\"chain\":\"" + std::string(chain) +
           "\",\"venue\":\"defi\",\"total\":\"" + std::string(total) + "\",\"available\":\"" +
           std::string(available) + "\",\"held\":\"" + std::string(held) + "\"}";
}
std::string body(std::string_view sol = "0.031896934", std::string_view usdc = "1.006989") {
    return "{\"data\":[" + row("SOL", sol, sol) + "," + row("USDC", usdc, usdc) + "]}";
}
struct FakeReader {
    State state{};
    SwapBalanceBinding binding{};
    DeclaredOutsideBalance outside{};
    CommittedBalanceReadBarrier barrier{};
    BalanceReadContext read{};
    SwapReconcileCut cut{};
    std::string response{body()};
    FakeReader() {
        Initialize opening;
        opening.binding = {digest(1), digest(2), digest(3), AccountId{2}, InstrumentId{2},
                           PolicyId{901}, PolicyVersion{1}, RiskPolicyVersion{1}};
        opening.limits = {QuoteAtoms{5'000'000}, QuoteAtoms{2'000'000}, QuoteAtoms{10'000},
                          QuoteAtoms{500'000}, BaseAtoms{1'000'000'000}, BaseAtoms{},
                          QuoteAtoms{125'000'000}, QuoteAtoms{50'000'000}, 10, 20};
        opening.base = BaseAtoms{30'000'000}; opening.quote = QuoteAtoms{1'002'000};
        opening.funding_evidence = digest(4); opening.kill_epoch = 1; opening.executor_generation = 1;
        require(apply(state, opening, Role::operator_role, 100) == Code::applied, "fake opening admitted");
        binding = {opening.binding, digest(9), 10'000'000};
        outside = {digest(9), digest(11), digest(10), 900, 1'896'934, 4'989};
        const auto partition = outside_balance_digest(outside);
        barrier = {digest(1), digest(11), digest(12), 10, 1000, 1001};
        read = {digest(9), digest(11), digest(13), partition, 1002, 1003};
        cut = {digest(1), digest(11), digest(13), partition, 10, 1010, 10};
    }
    SwapReconcileResult run() const { return reconcile_swap_balance(state, binding, outside, barrier, read, cut, response); }
    void start() {
        Intent i;
        i.binding = state.opening.binding; i.effect = EffectId{1}; i.side = Side::buy;
        i.quote_input = QuoteAtoms{1'000'000}; i.max_quote_debit = QuoteAtoms{1'002'000};
        i.min_base_receive = BaseAtoms{9'000'000}; i.fee_cap = QuoteAtoms{2'000};
        i.source_time = 100; i.sign_by = 120; i.kill_epoch = 1; i.executor_generation = 1;
        require(apply(state, i, Role::strategy, 100) == Code::applied, "fake reserve admitted");
        require(apply(state, Prepare{EffectId{1}, digest(20), digest(21), 1}, Role::gateway, 101) == Code::applied,
                "fake prepare admitted");
        require(apply(state, Attempt{EffectId{1}, digest(20)}, Role::gateway, 102) == Code::applied,
                "fake attempt admitted");
    }
    Settlement fill() const {
        Settlement x;
        x.effect = EffectId{1}; x.custody = digest(3); x.payload = digest(20);
        x.transaction = digest(22); x.evidence = digest(23); x.outcome = Outcome::succeeded;
        x.base_credit = BaseAtoms{9'100'123}; x.quote_debit = QuoteAtoms{1'002'000};
        x.fee = QuoteAtoms{2'000}; x.finalized = true;
        return x;
    }
};
void exact_partition_and_response() {
    FakeReader r;
    const auto before = r.state;
    const auto result = r.run();
    require(result.code == SwapReconcileCode::compared_snapshot_only &&
            result.comparison.verdict == SpotBalanceVerdict::matching_snapshot_only,
            "managed plus explicit outside atoms matches snapshot only");
    require(result.wallet.base.total_atoms == 31'896'934 && result.wallet.quote.total_atoms == 1'006'989 &&
            result.wallet.received_at_ms == 1003 && r.state == before,
            "independent wallet atom oracle never changes managed funding");
    require(to_hex(result.response_digest) == "0a69e55e27497957d6f4009cb2c253afd2b441d5746f43a15dc08099e6a3f6b9", "response bytes have independently pinned digest");
    require(to_hex(outside_balance_digest(r.outside)) == "aa14c5892515c1f19773b1f878ccafc390ce4adbc1e0e417bacf3efd8e5c2a06", "canonical outside declaration digest oracle");
    r.response = "{\"data\":[" + row("USDC", "1.006989", "1.006989") + "," + row("SOL", "0.031896934", "0.031896934") + "]}";
    require(r.run().comparison.verdict == SpotBalanceVerdict::matching_snapshot_only, "row order never changes asset mapping");
}
void rows_and_exact_decimals() {
    FakeReader r;
    r.response = "{\"data\":[" + row("SOL", "0.031896934", "0.031896934") + "]}";
    require(r.run().code == SwapReconcileCode::row_identity, "missing USDC row never guessed as zero");
    r.response = "{\"data\":[" + row("SOL", "0.031896934", "0.031896934") + "," + row("SOL", "0", "0") + "]}";
    require(r.run().code == SwapReconcileCode::row_identity, "duplicate SOL rows refused");
    r.response = "{\"data\":[" + row("SOL", "0.031896934", "0.031896934", "0", "base") + "," + row("USDC", "1.006989", "1.006989") + "]}";
    require(r.run().code == SwapReconcileCode::row_identity, "same symbol on another chain refused");
    for (const auto* amount : {"0.0318969341", "-0.1", "1e0", "9223372037", "NaN"}) {
        r.response = body(amount);
        require(r.run().code == SwapReconcileCode::bad_amount, "invalid or nonrepresentable SOL decimal refused");
    }
    r.response = body("0.031896934", "1.0069891");
    require(r.run().code == SwapReconcileCode::bad_amount, "USDC seventh decimal never rounded");
    r.response = "{\"data\":[" + row("SOL", "0.031896934", "0.031896934") + "," + row("USDC", "1.006989", "1.006988", "0.000001") + "]}";
    require(r.run().code == SwapReconcileCode::held_funds, "one held USDC atom blocks comparison");
    r.response = "{\"data\":[" + row("SOL", "0.031896934", "0.031896933", "0.000000001") + "," + row("USDC", "1.006989", "1.006989") + "]}";
    require(r.run().code == SwapReconcileCode::held_funds, "one held SOL atom blocks comparison");
    r.response = "{\"data\":[" + row("SOL", "0.031896934", "0.031896934", "0.000000001") + "," + row("USDC", "1.006989", "1.006989") + "]}";
    require(r.run().code == SwapReconcileCode::bad_amount, "inconsistent total available held refused");
    r.response = "{\"data\":[],\"data\":[]}";
    require(r.run().code == SwapReconcileCode::malformed_response, "duplicate JSON member refused");
    r.response.assign(32769, ' ');
    require(r.run().code == SwapReconcileCode::malformed_response, "oversized response refused");
}
void independent_time_and_result_barriers() {
    FakeReader r; r.cut.required_result_index = 11;
    require(r.run().code == SwapReconcileCode::state_watermark, "newly known in flight result invalidates earlier adequate state");
    r = FakeReader{}; r.barrier.request_started_ms = 999;
    require(r.run().code == SwapReconcileCode::stale_state, "fresh receive cannot refresh stale state request");
    r = FakeReader{}; r.read.request_started_ms = 1000;
    require(r.run().code == SwapReconcileCode::stale_observation, "balance request must follow completed state barrier");
    r = FakeReader{}; r.read.received_at_ms = 1011;
    require(r.run().code == SwapReconcileCode::stale_observation, "future balance receive refused");
    r = FakeReader{}; r.cut.now_ms = 1013;
    r.barrier.request_started_ms = 1003; r.barrier.received_at_ms = 1003;
    r.read.request_started_ms = 1003; r.read.received_at_ms = 1013;
    require(r.run().comparison.verdict == SpotBalanceVerdict::matching_snapshot_only, "inclusive age and ordered equality boundary accepted");
    r.cut.now_ms = 1014;
    require(r.run().code == SwapReconcileCode::stale_state, "one millisecond beyond request age refused");
    r = FakeReader{}; r.read.request_id = digest(99);
    require(r.run().code == SwapReconcileCode::identity_mismatch, "late reply from prior request refused");
    r = FakeReader{}; r.cut.clock_epoch = digest(99);
    require(r.run().code == SwapReconcileCode::identity_mismatch, "restart cannot reuse prior clock epoch");
    r = FakeReader{}; r.outside.clock_epoch = digest(99);
    require(r.run().code == SwapReconcileCode::identity_mismatch, "outside declaration from old clock epoch refused");
    r = FakeReader{}; r.barrier.incarnation = digest(99);
    require(r.run().code == SwapReconcileCode::identity_mismatch, "other cluster state cannot satisfy barrier");
    r = FakeReader{}; r.barrier.applied_index = std::numeric_limits<std::uint64_t>::max();
    require(r.run().code == SwapReconcileCode::invalid_context, "negative signed Raft index cast to unsigned refused");
    r = FakeReader{}; r.barrier.read_evidence_digest = {};
    require(r.run().code == SwapReconcileCode::invalid_context, "missing state read evidence refused");
}
void external_changes_and_pinned_partition() {
    FakeReader r; r.response = body("0.031896935");
    auto result = r.run();
    require(result.code == SwapReconcileCode::compared_snapshot_only &&
            result.comparison.verdict == SpotBalanceVerdict::unexplained_delta && result.comparison.base_delta_atoms == 1,
            "one atom external deposit remains unexplained");
    r.response = body("0.031896934", "1.006988"); result = r.run();
    require(result.comparison.verdict == SpotBalanceVerdict::unexplained_delta && result.comparison.quote_delta_atoms == -1,
            "one atom external withdrawal never inferred as fill");
    r = FakeReader{}; ++r.outside.base_atoms;
    require(r.run().code == SwapReconcileCode::partition_mismatch, "changing outside atoms cannot reuse pinned declaration");
    r = FakeReader{}; r.cut.outside_partition_digest = digest(99);
    require(r.run().code == SwapReconcileCode::partition_mismatch, "partition changed during read refuses old response");
    r = FakeReader{}; r.outside.base_atoms = std::numeric_limits<std::int64_t>::max();
    r.cut.outside_partition_digest = r.read.outside_partition_digest = outside_balance_digest(r.outside);
    require(r.run().code == SwapReconcileCode::overflow, "managed plus outside atom addition cannot overflow");
    r = FakeReader{}; r.outside.declared_at_ms = 1002;
    r.cut.outside_partition_digest = r.read.outside_partition_digest = outside_balance_digest(r.outside);
    require(r.run().code == SwapReconcileCode::partition_mismatch, "outside partition must precede state read");
    r = FakeReader{}; r.read.account_and_assets_digest = digest(88);
    require(r.run().code == SwapReconcileCode::identity_mismatch, "different authenticated account context refused");
}
void unknown_restart_and_late_settlement() {
    FakeReader r; r.start();
    require(apply(r.state, MarkUnknown{EffectId{1}}, Role::gateway, 130) == Code::applied, "fake unknown admitted");
    const auto unknown = r.state;
    auto result = r.run();
    require(result.code == SwapReconcileCode::projection_refused && result.projection_code == SwapBalanceCode::unresolved_activity &&
            r.state == unknown && r.state.reserved_quote == QuoteAtoms{1'002'000}, "matching wallet never resolves unknown or releases holds");
    std::array<std::byte, max_state_bytes> bytes{};
    const auto encoded = encode_state(r.state, bytes);
    require(encoded.error == CodecError::none, "unknown state serialized");
    State restarted;
    require(decode_state({bytes.data(), encoded.size}, restarted) == CodecError::none && restarted == unknown,
            "restart restores exact unknown reservation");
    r.state = restarted;
    require(r.run().code == SwapReconcileCode::projection_refused, "restart does not resolve unknown from balance alone");
    auto fill = r.fill(); fill.finalized = false;
    require(apply(r.state, fill, Role::gateway, 131) == Code::invalid && r.state == unknown,
            "nonfinal late fixture does not become a terminal fill");
    fill.finalized = true;
    require(apply(r.state, fill, Role::gateway, 132) == Code::applied, "separately supplied finalized fixture settles");
    r.response = body("0.040997057", "0.004989"); r.cut.required_result_index = 11;
    require(r.run().code == SwapReconcileCode::state_watermark, "known late settlement needs covering state read");
    r.barrier.applied_index = 11;
    result = r.run();
    require(result.comparison.verdict == SpotBalanceVerdict::matching_snapshot_only && r.state.base == BaseAtoms{39'100'123} &&
            r.state.quote == QuoteAtoms{0}, "late settled atom oracle matches without adopting outside balances");
    require(apply(r.state, fill, Role::gateway, 133) == Code::duplicate, "duplicate finalized fixture remains idempotent");
}
}
int main(int argc, char** argv) {
    const std::pair<const char*, void (*)()> cases[] = {
        {"exact_partition_and_response", exact_partition_and_response},
        {"rows_and_exact_decimals", rows_and_exact_decimals},
        {"independent_time_and_result_barriers", independent_time_and_result_barriers},
        {"external_changes_and_pinned_partition", external_changes_and_pinned_partition},
        {"unknown_restart_and_late_settlement", unknown_restart_and_late_settlement}};
    unsigned ran = 0, failed = 0;
    for (const auto& [name, test] : cases) {
        if (argc == 2 && std::string_view(argv[1]) != name) continue;
        ++ran;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    return ran && !failed ? 0 : 1;
}
