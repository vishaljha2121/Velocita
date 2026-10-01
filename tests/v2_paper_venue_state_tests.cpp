// The paper venue's durable state: a restored venue answers exactly as the
// venue it was captured from, every write is durable before the answer that
// depends on it, and a torn or corrupt file is refused, never reset.
//
// Economics (hand-computed; quote micros per tick-lot = 10):
//   BUY  1 lot @ 60'001: notional 600'010; fee ceil(600'010 * 115 / 10'000) = ceil(6'900.115) = 6'901
//   SELL 25 lots @ 59'001: notional 14'750'250; fee 14'750'250 * 120 / 10'000 = 177'003 exactly
#include "velocita/v2/digest.hpp"
#include "velocita/v2/paper_venue_store.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using namespace velocita::v2;
using namespace velocita::v2::paper;
using sim::QueryOutcome;
using sim::SendOutcome;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::int64_t kFunding = 25'000'000;
constexpr std::int64_t kBuyPrice = 60'001;
constexpr std::int64_t kBuyNotional = 600'010;
constexpr std::int64_t kBuyFee = 6'901;
const std::array<std::byte, 16> kIncarnation{std::byte{0xAB}, std::byte{0x5A}, std::byte{0x02}};
const std::array<std::byte, 16> kOtherIncarnation{std::byte{0x01}};

PaperVenueConfig config(std::int64_t quote = kFunding, InstrumentId instrument = InstrumentId{4}) {
    PaperVenueConfig value;
    value.spec = {VenueId{9}, instrument, 1'000'000, 1'000, 8};
    value.profile = {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, instrument, InstrumentSpecVersion{7},
                     MoneyMicros::from_atoms(10), PriceTicks::from_atoms(1), PriceTicks::from_atoms(1'000'000),
                     QuantityLots::from_atoms(1), QuantityLots::from_atoms(100'000)};
    value.buy_fee_bps = 115;
    value.sell_fee_bps = 120;
    value.quote_balance = MoneyMicros::from_atoms(quote);
    value.latency = 1;
    return value;
}

OrderEffect effect(std::uint64_t id, Side side, std::int64_t price = kBuyPrice, std::int64_t lots = 1) {
    OrderEffect value;
    value.effect_id = EffectId{1'000 + id};
    value.cluster_order_id = ClusterOrderId{2'000 + id};
    value.committed_directive_index = 1;
    value.executor_generation = ExecutorGeneration{3};
    value.kill_epoch = KillEpoch{2};
    value.venue_id = VenueId{9};
    value.account_id = AccountId{5};
    value.instrument_id = InstrumentId{4};
    value.operation = EffectOperation::submit;
    value.side = side;
    value.price = PriceTicks::from_atoms(price);
    value.quantity = QuantityLots::from_atoms(lots);
    value.time_in_force = TimeInForce::fill_or_kill;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.decision_id = DecisionId{id};
    value.risk_version = RiskVersion{1};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{1};
    value.payload_digest.bytes[0] = std::byte{0x77};
    return value;
}

std::vector<Digest256> fingerprints(std::span<const VenueObservation> reports) {
    std::vector<Digest256> out;
    for (const auto& report : reports) out.push_back(observation_fingerprint(report));
    return out;
}

std::vector<VenueObservation> poll_all(const sim::VenueTransport& venue, sim::Tick now) {
    std::array<VenueObservation, 32> out{};
    const auto count = venue.poll(out, now);
    return {out.begin(), out.begin() + static_cast<std::ptrdiff_t>(count)};
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        auto pattern = (std::filesystem::temp_directory_path() / "v2-paper-state-XXXXXX").string();
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        require(::mkdtemp(bytes.data()) != nullptr, "mkdtemp failed");
        path = bytes.data();
    }
    ~TempDir() {
        ::chmod(path.c_str(), 0700);
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::vector<std::byte> read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> out(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) out[i] = static_cast<std::byte>(raw[i]);
    return out;
}

void write_file(const std::filesystem::path& file, std::span<const std::byte> bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// Replaces the trailing digest so a deliberately edited payload is digest-correct.
void redigest(std::vector<std::byte>& bytes) {
    const auto covered = std::span<const std::byte>(bytes).first(bytes.size() - 32);
    const auto digest = sha256_digest(covered);
    std::copy(digest.bytes.begin(), digest.bytes.end(), bytes.end() - 32);
}

// A venue with history: order 1 filled and acknowledged, order 2 filled with
// its reports delivered but not acknowledged, order 3 a killed sell (nothing
// recorded, its rejection report owned).
void give_history(const sim::VenueTransport& venue) {
    require(venue.send(effect(1, Side::buy), 10) == SendOutcome::accepted, "order 1 was not accepted");
    for (const auto& report : poll_all(venue, 100)) venue.acknowledge(report);
    require(venue.send(effect(2, Side::buy), 110) == SendOutcome::accepted, "order 2 was not accepted");
    require(poll_all(venue, 200).size() == 2, "order 2 did not report ack + fill");
    require(venue.send(effect(3, Side::sell, 59'001, 5), 210) == SendOutcome::business_rejected,
            "a 5-lot sell against 2 lots was not killed");
}

// ---------------------------------------------------------------------------

void restored_venue_answers_exactly_as_the_original() {
    PaperVenue original(config(), PaperFaultPolicy::clean(), 7);
    give_history(paper_venue_transport(original));
    auto image = std::make_unique<PaperVenueImage>();
    original.capture(*image);
    PaperState state;
    state.venue = *image;
    PaperState decoded;
    require(decode_paper_state(encode_paper_state(state), decoded) == PaperStateError::none, "the image did not decode");

    // A venue configured with a different balance: restore must replace it.
    PaperVenue restored(config(1), PaperFaultPolicy::clean(), 99);
    require(restored.restore(decoded.venue), "a sound image was refused");

    // Two buys of 1 lot: 25'000'000 - 2 * (600'010 + 6'901) = 23'786'178.
    require(restored.balances().quote.atoms() == kFunding - 2 * (kBuyNotional + kBuyFee) &&
                restored.balances().base.atoms() == 2 && restored.balances().fees_paid.atoms() == 2 * kBuyFee,
            "the restored balances are not the two fills");
    require(restored.balances() == original.balances(), "restored balances differ from the original");

    // Owned reports: a new process redelivers what the old one never acknowledged.
    original.restart_delivery();
    const auto left = poll_all(paper_venue_transport(original), 1'000);
    const auto right = poll_all(paper_venue_transport(restored), 1'000);
    require(left.size() == 3 && fingerprints(left) == fingerprints(right),
            "the restored venue does not own the same three reports (order 2 ack + fill, order 3 rejection)");

    // Idempotency and queries answer as before.
    require(restored.send(effect(1, Side::buy), 2'000) == SendOutcome::venue_duplicate &&
                original.send(effect(1, Side::buy), 2'000) == SendOutcome::venue_duplicate,
            "a resend of known order 1 was not a venue duplicate after restore");
    for (const std::uint64_t id : {1U, 2U, 3U}) {
        const auto a = original.query(ClusterOrderId{2'000 + id}, 2'000);
        const auto b = restored.query(ClusterOrderId{2'000 + id}, 2'000);
        require(a.outcome == b.outcome && a.venue_order_id == b.venue_order_id &&
                    a.filled_quantity == b.filled_quantity,
                "query of order " + std::to_string(id) + " differs after restore");
    }
    require(restored.query(ClusterOrderId{2'003}, 2'000).outcome == QueryOutcome::not_found,
            "the killed order 3 became known");

    // Identities continue; none is reused.
    require(restored.send(effect(4, Side::buy), 3'000) == SendOutcome::accepted &&
                original.send(effect(4, Side::buy), 3'000) == SendOutcome::accepted,
            "order 4 was not accepted");
    const auto next_left = poll_all(paper_venue_transport(original), 4'000);
    const auto next_right = poll_all(paper_venue_transport(restored), 4'000);
    require(next_left.size() == 2 && fingerprints(next_left) == fingerprints(next_right),
            "the next order's reports differ after restore (sequence or execution id reused)");
    for (const auto& report : next_right)
        for (const auto& earlier : right)
            require(report.venue_sequence != earlier.venue_sequence, "a venue sequence was reused after restore");
}

void restore_refuses_an_image_of_another_row_or_a_broken_invariant() {
    PaperVenue original(config(), PaperFaultPolicy::clean(), 7);
    give_history(paper_venue_transport(original));
    auto image = std::make_unique<PaperVenueImage>();
    original.capture(*image);
    PaperVenue other(config(kFunding, InstrumentId{5}), PaperFaultPolicy::clean(), 7);
    require(!other.restore(*image), "an image of instrument 4 restored into an instrument 5 venue");

    const auto refused = [&](auto mutate, const char* what) {
        auto broken = std::make_unique<PaperVenueImage>(*image);
        mutate(*broken);
        PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
        const auto before = venue.balances();
        require(!venue.restore(*broken), std::string("restore accepted ") + what);
        require(venue.balances() == before && venue.order_count() == 0, std::string("a refused restore mutated: ") + what);
    };
    refused([](PaperVenueImage& v) { v.balances.base = QuantityLots::from_atoms(-1); }, "a negative base");
    refused([](PaperVenueImage& v) { v.order_count = kMaxPaperOrders + 1; }, "too many orders");
    refused([](PaperVenueImage& v) { v.orders[1].cluster_order_id = v.orders[0].cluster_order_id; },
            "two orders with one identity");
    refused([](PaperVenueImage& v) { v.venue_order_counter = 0; }, "an order id above the counter");
    refused([](PaperVenueImage& v) { v.reports[0].observation.venue_sequence = VenueSequence{v.venue_sequence + 1}; },
            "a report sequence above the counter");
    refused([](PaperVenueImage& v) { v.reports[0].observation.kind = VenueObservationKind::effect_unknown; },
            "a report kind the venue never produces");
}

void encoded_state_round_trips_and_every_corruption_is_refused() {
    PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
    give_history(paper_venue_transport(venue));
    PaperState state;
    state.origin.cluster_incarnation = kIncarnation;
    state.origin.funding = MoneyMicros::from_atoms(kFunding);
    venue.capture(state.venue);
    const auto bytes = encode_paper_state(state);
    PaperState decoded;
    require(decode_paper_state(bytes, decoded) == PaperStateError::none, "canonical bytes did not decode");
    require(encode_paper_state(decoded) == bytes, "decode then encode changed the bytes");

    // Layout: magic [0,8), version [8,12), payload length [12,16), payload, digest (last 32).
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        auto flipped = bytes;
        flipped[at] ^= std::byte{0x01};
        const auto error = decode_paper_state(flipped, decoded);
        const auto expected = at < 8 ? PaperStateError::bad_magic
                              : at >= 12 && at < 16 ? PaperStateError::torn
                                                    : PaperStateError::digest_mismatch;
        require(error == expected, "a flip at byte " + std::to_string(at) + " gave " + describe(error) +
                                       ", expected " + describe(expected));
    }
    for (const auto length : {std::size_t{0}, std::size_t{20}, bytes.size() - 1}) {
        const auto torn = std::span<const std::byte>(bytes).first(length);
        require(decode_paper_state(torn, decoded) == PaperStateError::torn,
                "a file cut to " + std::to_string(length) + " bytes was not refused as torn");
    }
    auto longer = bytes;
    longer.push_back(std::byte{0});
    require(decode_paper_state(longer, decoded) == PaperStateError::torn, "a trailing byte was not refused");

    auto version = bytes;
    version[8] = std::byte{2};
    redigest(version);
    require(decode_paper_state(version, decoded) == PaperStateError::bad_version, "version 2 was not refused");

    // Digest-correct, but the payload's order count exceeds capacity. The
    // order count sits after the fixed origin (16 + 8 + 1 + 8 + 8 + 8 + 8 = 57)
    // and venue header (7 * 8 + 3 * 8 + 4 * 8 + 8 = 120) fields.
    auto overfull = bytes;
    const std::size_t order_count_at = 16 + 57 + 120;
    const std::uint32_t too_many = kMaxPaperOrders + 1;
    for (std::size_t i = 0; i < 4; ++i) overfull[order_count_at + i] = static_cast<std::byte>(too_many >> (8 * i));
    redigest(overfull);
    require(decode_paper_state(overfull, decoded) == PaperStateError::malformed,
            "an order count above capacity was not refused as malformed");
}

void store_is_atomic_and_create_refuses_an_existing_file() {
    TempDir dir;
    const auto file = dir.path / "paper.state";
    require(load_paper_state(file).error == PaperStateError::missing, "an absent file was not reported missing");
    const auto fresh = fresh_paper_state(config(), kIncarnation, MoneyMicros::from_atoms(kFunding));
    require(fresh.error.empty(), "fresh bootstrap refused: " + fresh.error);
    require(create_paper_state(file, *fresh.state) == PaperStateError::none, "create failed");
    require(create_paper_state(file, *fresh.state) == PaperStateError::exists, "create replaced an existing file");
    require(read_file(file) == encode_paper_state(*fresh.state), "the file is not the canonical bytes");
    require(!std::filesystem::exists(dir.path / "paper.state.tmp"), "the temporary file was left behind");

    write_file(file, std::vector<std::byte>{});
    require(load_paper_state(file).error == PaperStateError::torn, "an empty file was not refused as torn");
    write_file(file, std::vector<std::byte>(64, std::byte{'x'}));
    require(load_paper_state(file).error == PaperStateError::bad_magic, "garbage was not refused");
    require(store_paper_state(dir.path / "absent" / "paper.state", encode_paper_state(*fresh.state)) ==
                PaperStateError::write_failed,
            "a write into a missing directory reported success");
}

void fresh_and_adopted_bootstraps_record_how_the_account_began() {
    const auto fresh = fresh_paper_state(config(), kIncarnation, MoneyMicros::from_atoms(kFunding));
    require(fresh.state->venue.balances.quote.atoms() == kFunding && fresh.state->venue.balances.base.atoms() == 0 &&
                !fresh.state->origin.adopted && fresh.state->origin.funding.atoms() == kFunding,
            "a fresh account is not flat with the whole funding");

    // The live case: long 25 lots bought for $21.315 including fees, funded with $25.
    const auto adopted = adopted_paper_state(config(), kIncarnation, MoneyMicros::from_atoms(kFunding),
                                             QuantityLots::from_atoms(25), MoneyMicros::from_atoms(21'315'000), 4'242, 77);
    require(adopted.error.empty(), "adoption refused: " + adopted.error);
    const auto& venue = adopted.state->venue;
    require(venue.balances.base.atoms() == 25 && venue.balances.quote.atoms() == kFunding - 21'315'000,
            "adoption did not set base = kernel lots and quote = funding - cost");
    require(venue.ledger.committed_through == 4'242, "the adoption read is not the first reconciliation cut");
    const auto& origin = adopted.state->origin;
    require(origin.adopted && origin.adopted_lots.atoms() == 25 && origin.adopted_cost.atoms() == 21'315'000 &&
                origin.adopted_at_index == 4'242 && origin.adopted_at_state_version == 77 &&
                origin.cluster_incarnation == kIncarnation,
            "the adoption event is not recorded");

    // The adopted base is sellable: the exit is not killed insufficient_base.
    PaperVenue sell(config(), PaperFaultPolicy::clean(), 7);
    require(sell.restore(venue), "the adopted image was refused");
    require(sell.send(effect(9, Side::sell, 59'001, 25), 10) == SendOutcome::accepted,
            "a 25-lot sell against the adopted 25 lots was not accepted");
    // 3'685'000 + 14'750'250 - 177'003 = 18'258'247
    require(sell.balances().quote.atoms() == 18'258'247 && sell.balances().base.atoms() == 0,
            "the adopted exit did not settle at the sell-side cost");

    const auto refused = [](const PaperBootstrap& result, const char* what, const char* reason) {
        require(!result.error.empty() && !result.state, std::string("adoption accepted ") + what);
        require(result.error.find(reason) != std::string::npos,
                std::string("adoption of ") + what + " was refused for another reason: " + result.error);
    };
    refused(adopted_paper_state(config(), kIncarnation, MoneyMicros::from_atoms(kFunding), QuantityLots{},
                                MoneyMicros{}, 1, 1),
            "a flat kernel", "no position");
    refused(adopted_paper_state(config(), kIncarnation, MoneyMicros::from_atoms(kFunding),
                                QuantityLots::from_atoms(25), MoneyMicros::from_atoms(kFunding + 1), 1, 1),
            "a cost above the funding", "exceeds the funding");
    refused(adopted_paper_state(config(), kIncarnation, MoneyMicros::from_atoms(kFunding),
                                QuantityLots::from_atoms(25), MoneyMicros::from_atoms(-1), 1, 1),
            "a negative cost", "must not be negative");
    refused(adopted_paper_state(config(), kIncarnation, MoneyMicros::from_atoms(kFunding),
                                QuantityLots::from_atoms(25), MoneyMicros::from_atoms(1), 0, 1),
            "no committed read", "applied index");
}

void committed_ledger_counts_fills_only_and_never_moves_back() {
    PaperVenue venue(config(), PaperFaultPolicy::clean(), 7);
    VenueObservation ack;
    ack.kind = VenueObservationKind::order_acknowledged;
    venue.record_committed({50, ack, 0, 1'000.0});
    require(venue.ledger().committed_through == 50 && venue.ledger().fee_micros_total == 0 &&
                venue.ledger().last_fill_wall_seconds == 0,
            "an acknowledgement counted as a fill");
    VenueObservation fill;
    fill.kind = VenueObservationKind::fill;
    fill.side = Side::sell;
    fill.fee = FeeMicros::from_atoms(177'003);
    venue.record_committed({40, fill, 14'750'250, 2'000.5});
    const auto ledger = venue.ledger();
    require(ledger.committed_through == 50, "an older commit index moved the cut back");
    require(ledger.fee_micros_total == 177'003 && ledger.volume_micros_sell == 14'750'250 &&
                ledger.volume_micros_buy == 0 && ledger.last_fill_wall_seconds == 2'000.5,
            "the committed sell fill was not booked to fees, sell volume and the last-fill time");
}

void durable_venue_writes_before_it_answers_and_restart_reproduces_it() {
    TempDir dir;
    const auto file = dir.path / "paper.state";
    const auto fresh = fresh_paper_state(config(), kIncarnation, MoneyMicros::from_atoms(kFunding));
    require(create_paper_state(file, *fresh.state) == PaperStateError::none, "create failed");
    std::vector<VenueObservation> order_two;
    {
        auto opened = DurablePaperVenue::open(file, config(), PaperFaultPolicy::clean(), kIncarnation);
        require(opened.error == PaperStateError::none, std::string("open failed: ") + describe(opened.error));
        const auto transport = opened.venue->transport();
        const auto book = opened.venue->book();
        require(transport.send(effect(1, Side::buy), 10) == SendOutcome::accepted, "send refused");
        // Before any report is released, the file already holds the order and its reports.
        auto on_disk = load_paper_state(file);
        require(on_disk.error == PaperStateError::none && on_disk.state->venue.order_count == 1 &&
                    on_disk.state->venue.report_count == 2 && on_disk.state->venue.balances.base.atoms() == 1,
                "the send's order and reports were not durable before send returned");
        const auto reports = poll_all(transport, 100);
        require(reports.size() == 2, "order 1 did not report twice");
        for (const auto& report : reports) {
            const auto notional = report.kind == VenueObservationKind::fill ? kBuyNotional : 0;
            book.committed_fn(book.context, {77, report, notional, 1'234.0});
            transport.acknowledge(report);
        }
        on_disk = load_paper_state(file);
        require(on_disk.error == PaperStateError::none, "the state file is unreadable after the acknowledgements");
        require(on_disk.state->venue.report_count == 0 && on_disk.state->venue.ledger.committed_through == 77 &&
                    on_disk.state->venue.ledger.fee_micros_total == kBuyFee &&
                    on_disk.state->venue.ledger.last_fill_wall_seconds == 1'234.0,
                "the acknowledgement and the committed fill were not written together");
        require(transport.send(effect(2, Side::buy), 200) == SendOutcome::accepted, "order 2 refused");
        order_two = poll_all(transport, 300);  // delivered, never acknowledged: the process dies here
    }
    auto reopened = DurablePaperVenue::open(file, config(), PaperFaultPolicy::clean(), kIncarnation);
    require(reopened.error == PaperStateError::none, "reopen failed");
    const auto transport = reopened.venue->transport();
    require(transport.send(effect(1, Side::buy), 10) == SendOutcome::venue_duplicate,
            "a resend of order 1 after restart was not a venue duplicate");
    require(transport.query(ClusterOrderId{2'001}, 10).filled_quantity.atoms() == 1,
            "order 1 is not known filled after restart");
    const auto redelivered = poll_all(transport, 10);
    require(order_two.size() == 2 && fingerprints(redelivered) == fingerprints(order_two),
            "order 2's unacknowledged reports were not redelivered unchanged after restart");
    sim::VenueBook book;
    reopened.venue->book().read_fn(reopened.venue->book().context, book);
    require(book.base.atoms() == 2 && book.committed_through == 77 && book.fee_micros_total == kBuyFee &&
                book.volume_micros_buy == kBuyNotional && book.last_fill_wall_seconds == 1'234.0 &&
                book.unacknowledged_reports == 2 && book.pending_order_count == 1 &&
                book.pending_orders[0] == ClusterOrderId{2'002},
            "the reopened venue's book lost the base, the cut, the fill ledger or the owned reports");
}

// Blocks every write under SUB by swapping the directory for a plain file of
// the same name: creating "SUB/x" then fails with ENOTDIR even for root (a
// read-only chmod does not stop root, and the image builds run its tests as root).
struct WriteBlock {
    std::filesystem::path sub;
    std::filesystem::path held;
    void block() {
        std::filesystem::rename(sub, held);
        std::ofstream(sub) << "blocked";
    }
    void unblock() {
        std::filesystem::remove(sub);
        std::filesystem::rename(held, sub);
    }
};

void failed_write_latches_the_venue_and_the_file_stays_truthful() {
    TempDir dir;
    WriteBlock writes{dir.path / "state", dir.path / "state.held"};
    std::filesystem::create_directories(writes.sub);
    const auto file = writes.sub / "paper.state";
    const auto fresh = fresh_paper_state(config(), kIncarnation, MoneyMicros::from_atoms(kFunding));
    require(create_paper_state(file, *fresh.state) == PaperStateError::none, "create failed");
    auto opened = DurablePaperVenue::open(file, config(), PaperFaultPolicy::clean(), kIncarnation);
    const auto transport = opened.venue->transport();
    writes.block();
    // Executed in memory, not on disk: on disk it never happened, so the
    // answer is ambiguous and nothing of it may be released.
    require(transport.send(effect(1, Side::buy), 10) == SendOutcome::ambiguous,
            "a send that could not be made durable was not reported ambiguous");
    require(opened.venue->failed(), "the failed write did not latch the venue");
    require(poll_all(transport, 1'000).empty(), "a failed venue released reports");
    require(transport.query(ClusterOrderId{2'001}, 1'000).outcome == QueryOutcome::unavailable,
            "a failed venue answered a query");
    require(transport.send(effect(2, Side::buy), 1'000) == SendOutcome::pre_send_failure,
            "a failed venue accepted another send");
    writes.unblock();
    auto reopened = DurablePaperVenue::open(file, config(), PaperFaultPolicy::clean(), kIncarnation);
    require(reopened.error == PaperStateError::none, "reopen after a failed write failed");
    require(reopened.venue->venue().balances().quote.atoms() == kFunding &&
                reopened.venue->venue().balances().base.atoms() == 0,
            "the file shows balances the failed write never made durable");
    require(reopened.venue->transport().query(ClusterOrderId{2'001}, 10).outcome == QueryOutcome::not_found,
            "an order never made durable is known after restart");

    // A query changes durable state too (its fault draw); one that cannot be
    // written answers nothing rather than a fact the file does not hold.
    const auto healthy = reopened.venue->transport();
    require(healthy.send(effect(5, Side::buy), 20) == SendOutcome::accepted, "order 5 refused");
    writes.block();
    require(healthy.query(ClusterOrderId{2'005}, 30).outcome == QueryOutcome::unavailable,
            "a query whose state could not be written still answered");
    require(reopened.venue->failed(), "the failed query write did not latch the venue");
    writes.unblock();
}

void open_refuses_another_cluster_another_venue_and_never_resets() {
    TempDir dir;
    const auto file = dir.path / "paper.state";
    require(DurablePaperVenue::open(file, config(), PaperFaultPolicy::clean(), kIncarnation).error ==
                PaperStateError::missing,
            "opening an absent file did not report it missing");
    require(!std::filesystem::exists(file), "opening an absent file created one");
    const auto fresh = fresh_paper_state(config(), kIncarnation, MoneyMicros::from_atoms(kFunding));
    require(create_paper_state(file, *fresh.state) == PaperStateError::none, "create failed");
    require(DurablePaperVenue::open(file, config(), PaperFaultPolicy::clean(), kOtherIncarnation).error ==
                PaperStateError::other_cluster,
            "a file of another cluster opened");
    require(DurablePaperVenue::open(file, config(kFunding, InstrumentId{5}), PaperFaultPolicy::clean(), kIncarnation)
                    .error == PaperStateError::other_venue,
            "a file of another instrument opened");
    auto bytes = read_file(file);
    bytes[40] ^= std::byte{0x10};
    write_file(file, bytes);
    require(DurablePaperVenue::open(file, config(), PaperFaultPolicy::clean(), kIncarnation).error ==
                PaperStateError::digest_mismatch,
            "a corrupt file opened");
    require(read_file(file) == bytes, "a refused open rewrote the corrupt file");
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"restored_venue_answers_exactly_as_the_original", restored_venue_answers_exactly_as_the_original},
        {"restore_refuses_an_image_of_another_row_or_a_broken_invariant",
         restore_refuses_an_image_of_another_row_or_a_broken_invariant},
        {"encoded_state_round_trips_and_every_corruption_is_refused",
         encoded_state_round_trips_and_every_corruption_is_refused},
        {"store_is_atomic_and_create_refuses_an_existing_file", store_is_atomic_and_create_refuses_an_existing_file},
        {"fresh_and_adopted_bootstraps_record_how_the_account_began",
         fresh_and_adopted_bootstraps_record_how_the_account_began},
        {"committed_ledger_counts_fills_only_and_never_moves_back",
         committed_ledger_counts_fills_only_and_never_moves_back},
        {"durable_venue_writes_before_it_answers_and_restart_reproduces_it",
         durable_venue_writes_before_it_answers_and_restart_reproduces_it},
        {"failed_write_latches_the_venue_and_the_file_stays_truthful",
         failed_write_latches_the_venue_and_the_file_stays_truthful},
        {"open_refuses_another_cluster_another_venue_and_never_resets",
         open_refuses_another_cluster_another_venue_and_never_resets},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
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
