#include "velocita/v2/paper_venue_store.hpp"

#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace velocita::v2::paper {
namespace {

constexpr std::size_t kHeaderBytes = kPaperStateMagic.size() + 4 + 4;
constexpr std::size_t kDigestBytes = 32;
constexpr std::size_t kMaxFileBytes = 1U << 20;

class Writer {
public:
    explicit Writer(std::vector<std::byte>& out) : out_(out) {}
    void u8(std::uint8_t value) { out_.push_back(static_cast<std::byte>(value)); }
    void u32(std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    void u64(std::uint64_t value) {
        for (std::size_t i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    void i64(std::int64_t value) { u64(static_cast<std::uint64_t>(value)); }
    void f64(double value) { u64(std::bit_cast<std::uint64_t>(value)); }
    void bytes(std::span<const std::byte> value) { out_.insert(out_.end(), value.begin(), value.end()); }

private:
    std::vector<std::byte>& out_;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> in) : in_(in) {}
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] bool done() const noexcept { return ok_ && at_ == in_.size(); }
    std::uint8_t u8() {
        if (!ok_ || at_ >= in_.size()) {
            ok_ = false;
            return 0;
        }
        return std::to_integer<std::uint8_t>(in_[at_++]);
    }
    std::uint32_t u32() {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) value |= std::uint32_t{u8()} << (8 * i);
        return value;
    }
    std::uint64_t u64() {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < 8; ++i) value |= std::uint64_t{u8()} << (8 * i);
        return value;
    }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    double f64() { return std::bit_cast<double>(u64()); }
    bool flag() {
        const auto value = u8();
        if (value > 1) ok_ = false;
        return value == 1;
    }
    void bytes(std::span<std::byte> out) {
        for (auto& byte : out) byte = static_cast<std::byte>(u8());
    }
    void fail() noexcept { ok_ = false; }

private:
    std::span<const std::byte> in_;
    std::size_t at_{};
    bool ok_{true};
};

void write_observation(Writer& w, const VenueObservation& o) {
    w.u8(static_cast<std::uint8_t>(o.kind));
    w.u64(o.venue_id.value());
    w.u64(o.venue_session_id.value());
    w.u64(o.venue_sequence.value());
    w.u64(o.cluster_order_id.value());
    w.u64(o.effect_id.value());
    w.u64(o.venue_order_id.value());
    w.u64(o.execution_id.value());
    w.u8(static_cast<std::uint8_t>(o.side));
    w.i64(o.price.atoms());
    w.i64(o.quantity.atoms());
    w.i64(o.fee.atoms());
    w.bytes(o.evidence_digest.bytes);
}

VenueObservation read_observation(Reader& r) {
    VenueObservation o;
    o.kind = static_cast<VenueObservationKind>(r.u8());
    o.venue_id = VenueId{r.u64()};
    o.venue_session_id = VenueSessionId{r.u64()};
    o.venue_sequence = VenueSequence{r.u64()};
    o.cluster_order_id = ClusterOrderId{r.u64()};
    o.effect_id = EffectId{r.u64()};
    o.venue_order_id = VenueOrderId{r.u64()};
    o.execution_id = ExecutionId{r.u64()};
    o.side = static_cast<Side>(r.u8());
    o.price = PriceTicks::from_atoms(r.i64());
    o.quantity = QuantityLots::from_atoms(r.i64());
    o.fee = FeeMicros::from_atoms(r.i64());
    r.bytes(o.evidence_digest.bytes);
    return o;
}

void write_payload(Writer& w, const PaperState& state) {
    const auto& origin = state.origin;
    w.bytes(origin.cluster_incarnation);
    w.i64(origin.funding.atoms());
    w.u8(origin.adopted ? 1 : 0);
    w.i64(origin.adopted_lots.atoms());
    w.i64(origin.adopted_cost.atoms());
    w.i64(origin.adopted_at_index);
    w.u64(origin.adopted_at_state_version);

    const auto& venue = state.venue;
    w.u64(venue.venue_id.value());
    w.u64(venue.instrument_id.value());
    w.u64(venue.random_state);
    w.u64(venue.venue_sequence);
    w.u64(venue.venue_order_counter);
    w.u64(venue.execution_counter);
    w.u64(venue.ordinal);
    w.i64(venue.balances.quote.atoms());
    w.i64(venue.balances.base.atoms());
    w.i64(venue.balances.fees_paid.atoms());
    w.i64(venue.ledger.committed_through);
    w.i64(venue.ledger.fee_micros_total);
    w.i64(venue.ledger.volume_micros_buy);
    w.i64(venue.ledger.volume_micros_sell);
    w.f64(venue.ledger.last_fill_wall_seconds);
    w.u32(static_cast<std::uint32_t>(venue.order_count));
    for (std::size_t i = 0; i < venue.order_count; ++i) {
        const auto& order = venue.orders[i];
        w.u64(order.cluster_order_id.value());
        w.u64(order.effect_id.value());
        w.u64(order.venue_order_id.value());
        w.u8(static_cast<std::uint8_t>(order.side));
        w.i64(order.price.atoms());
        w.i64(order.quantity.atoms());
        w.i64(order.notional.atoms());
        w.i64(order.fee.atoms());
    }
    w.u32(static_cast<std::uint32_t>(venue.report_count));
    for (std::size_t i = 0; i < venue.report_count; ++i) {
        const auto& report = venue.reports[i];
        w.u64(report.ordinal);
        w.u8(report.withheld ? 1 : 0);
        write_observation(w, report.observation);
    }
}

void read_payload(Reader& r, PaperState& state) {
    auto& origin = state.origin;
    r.bytes(origin.cluster_incarnation);
    origin.funding = MoneyMicros::from_atoms(r.i64());
    origin.adopted = r.flag();
    origin.adopted_lots = QuantityLots::from_atoms(r.i64());
    origin.adopted_cost = MoneyMicros::from_atoms(r.i64());
    origin.adopted_at_index = r.i64();
    origin.adopted_at_state_version = r.u64();

    auto& venue = state.venue;
    venue.venue_id = VenueId{r.u64()};
    venue.instrument_id = InstrumentId{r.u64()};
    venue.random_state = r.u64();
    venue.venue_sequence = r.u64();
    venue.venue_order_counter = r.u64();
    venue.execution_counter = r.u64();
    venue.ordinal = r.u64();
    venue.balances.quote = MoneyMicros::from_atoms(r.i64());
    venue.balances.base = QuantityLots::from_atoms(r.i64());
    venue.balances.fees_paid = FeeMicros::from_atoms(r.i64());
    venue.ledger.committed_through = r.i64();
    venue.ledger.fee_micros_total = r.i64();
    venue.ledger.volume_micros_buy = r.i64();
    venue.ledger.volume_micros_sell = r.i64();
    venue.ledger.last_fill_wall_seconds = r.f64();
    venue.order_count = r.u32();
    if (venue.order_count > venue.orders.size()) return r.fail();
    for (std::size_t i = 0; i < venue.order_count; ++i) {
        auto& order = venue.orders[i];
        order.cluster_order_id = ClusterOrderId{r.u64()};
        order.effect_id = EffectId{r.u64()};
        order.venue_order_id = VenueOrderId{r.u64()};
        order.side = static_cast<Side>(r.u8());
        order.price = PriceTicks::from_atoms(r.i64());
        order.quantity = QuantityLots::from_atoms(r.i64());
        order.notional = MoneyMicros::from_atoms(r.i64());
        order.fee = FeeMicros::from_atoms(r.i64());
    }
    venue.report_count = r.u32();
    if (venue.report_count > venue.reports.size()) return r.fail();
    for (std::size_t i = 0; i < venue.report_count; ++i) {
        auto& report = venue.reports[i];
        report.ordinal = r.u64();
        report.withheld = r.flag();
        report.observation = read_observation(r);
    }
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
    do {
        r = ::fsync(fd);
    } while (r < 0 && errno == EINTR);
    return r == 0;
}

std::filesystem::path parent_of(const std::filesystem::path& file) {
    return file.parent_path().empty() ? std::filesystem::path(".") : file.parent_path();
}

}

const char* describe(const PaperStateError error) noexcept {
    switch (error) {
    case PaperStateError::none: return "none";
    case PaperStateError::missing: return "paper state file missing";
    case PaperStateError::exists: return "paper state file already exists";
    case PaperStateError::unreadable: return "paper state file unreadable";
    case PaperStateError::torn: return "paper state file is torn (length disagrees with its header)";
    case PaperStateError::bad_magic: return "not a paper state file (magic)";
    case PaperStateError::bad_version: return "paper state file has an unknown version";
    case PaperStateError::digest_mismatch: return "paper state file digest mismatch (corrupt)";
    case PaperStateError::malformed: return "paper state file breaks a field or capacity rule";
    case PaperStateError::other_cluster: return "paper state file belongs to another cluster incarnation";
    case PaperStateError::other_venue: return "paper state file belongs to another venue or instrument";
    case PaperStateError::write_failed: return "paper state write failed";
    }
    return "unknown";
}

std::vector<std::byte> encode_paper_state(const PaperState& state) {
    std::vector<std::byte> payload;
    Writer body(payload);
    write_payload(body, state);
    std::vector<std::byte> out;
    out.reserve(kHeaderBytes + payload.size() + kDigestBytes);
    Writer w(out);
    w.bytes(kPaperStateMagic);
    w.u32(kPaperStateVersion);
    w.u32(static_cast<std::uint32_t>(payload.size()));
    w.bytes(payload);
    const auto digest = sha256_digest(out);
    w.bytes(digest.bytes);
    return out;
}

PaperStateError decode_paper_state(const std::span<const std::byte> bytes, PaperState& out) {
    if (bytes.size() < kHeaderBytes + kDigestBytes) return PaperStateError::torn;
    if (!std::equal(kPaperStateMagic.begin(), kPaperStateMagic.end(), bytes.begin()))
        return PaperStateError::bad_magic;
    Reader header(bytes.subspan(kPaperStateMagic.size(), 8));
    const auto version = header.u32();
    const auto payload_bytes = header.u32();
    if (static_cast<std::size_t>(payload_bytes) + kHeaderBytes + kDigestBytes != bytes.size())
        return PaperStateError::torn;
    const auto covered = bytes.first(kHeaderBytes + payload_bytes);
    const auto digest = sha256_digest(covered);
    if (!std::equal(digest.bytes.begin(), digest.bytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(covered.size())))
        return PaperStateError::digest_mismatch;
    if (version != kPaperStateVersion) return PaperStateError::bad_version;
    Reader r(bytes.subspan(kHeaderBytes, payload_bytes));
    out = {};
    read_payload(r, out);
    if (!r.done()) return PaperStateError::malformed;
    const auto& origin = out.origin;
    if (origin.funding.atoms() < 0 || (origin.adopted && (origin.adopted_lots.atoms() <= 0 ||
                                                          origin.adopted_cost.atoms() < 0 ||
                                                          origin.adopted_at_index <= 0)))
        return PaperStateError::malformed;
    return PaperStateError::none;
}

PaperStateLoad load_paper_state(const std::filesystem::path& file) {
    const int in = ::open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0) return {errno == ENOENT ? PaperStateError::missing : PaperStateError::unreadable, nullptr};
    std::vector<std::byte> bytes;
    std::array<std::byte, 8192> chunk{};
    bool failed = false;
    while (bytes.size() <= kMaxFileBytes) {
        const auto n = ::read(in, chunk.data(), chunk.size());
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) failed = true;
        if (n <= 0) break;
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + n);
    }
    ::close(in);
    if (failed) return {PaperStateError::unreadable, nullptr};
    if (bytes.size() > kMaxFileBytes) return {PaperStateError::torn, nullptr};
    auto state = std::make_unique<PaperState>();
    const auto error = decode_paper_state(bytes, *state);
    if (error != PaperStateError::none) return {error, nullptr};
    return {PaperStateError::none, std::move(state)};
}

PaperStateError store_paper_state(const std::filesystem::path& file, const std::span<const std::byte> bytes) {
    const int dir = ::open(parent_of(file).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) return PaperStateError::write_failed;
    const auto name = file.filename().string();
    const auto temporary = name + ".tmp";
    const int out = ::openat(dir, temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    bool ok = out >= 0 && write_all(out, bytes) && sync(out);
    if (out >= 0) ok = (::close(out) == 0) && ok;
    ok = ok && ::renameat(dir, temporary.c_str(), dir, name.c_str()) == 0 && sync(dir);
    ::close(dir);
    return ok ? PaperStateError::none : PaperStateError::write_failed;
}

PaperStateError create_paper_state(const std::filesystem::path& file, const PaperState& state) {
    struct stat existing{};
    if (::lstat(file.c_str(), &existing) == 0) return PaperStateError::exists;
    if (errno != ENOENT) return PaperStateError::unreadable;
    return store_paper_state(file, encode_paper_state(state));
}

PaperBootstrap fresh_paper_state(const PaperVenueConfig& config, const std::array<std::byte, 16>& cluster_incarnation,
                                 const MoneyMicros funding) {
    if (funding.atoms() < 0) return {"funding must not be negative", nullptr};
    auto venue_config = config;
    venue_config.quote_balance = funding;
    venue_config.base_balance = {};
    const PaperVenue venue(venue_config, PaperFaultPolicy::clean(), 1);
    if (!venue.configured()) return {"paper venue refused its configuration", nullptr};
    auto state = std::make_unique<PaperState>();
    state->origin.cluster_incarnation = cluster_incarnation;
    state->origin.funding = funding;
    venue.capture(state->venue);
    return {"", std::move(state)};
}

PaperBootstrap adopted_paper_state(const PaperVenueConfig& config,
                                   const std::array<std::byte, 16>& cluster_incarnation, const MoneyMicros funding,
                                   const QuantityLots kernel_lots, const MoneyMicros cost,
                                   const std::int64_t applied_index, const std::uint64_t state_version) {
    if (kernel_lots.atoms() <= 0) return {"the kernel holds no position to adopt; use a fresh start", nullptr};
    if (cost.atoms() < 0) return {"cost basis must not be negative", nullptr};
    if (funding.atoms() < cost.atoms()) return {"cost basis exceeds the funding", nullptr};
    if (applied_index <= 0) return {"adoption needs the applied index of a committed read", nullptr};
    auto venue_config = config;
    venue_config.quote_balance = MoneyMicros::from_atoms(funding.atoms() - cost.atoms());
    venue_config.base_balance = kernel_lots;
    PaperVenue venue(venue_config, PaperFaultPolicy::clean(), 1);
    if (!venue.configured()) return {"paper venue refused its configuration", nullptr};
    venue.record_committed({applied_index, {}, 0, 0});
    auto state = std::make_unique<PaperState>();
    state->origin = {cluster_incarnation, funding, true, kernel_lots, cost, applied_index, state_version};
    venue.capture(state->venue);
    return {"", std::move(state)};
}

DurablePaperVenue::DurablePaperVenue(std::filesystem::path file, const PaperVenueConfig& config,
                                     const PaperFaultPolicy& faults, PaperStateOrigin origin)
    : file_(std::move(file)), origin_(origin), venue_(config, faults, 1), scratch_(std::make_unique<PaperState>()) {}

DurablePaperVenue::Opened DurablePaperVenue::open(const std::filesystem::path& file, const PaperVenueConfig& config,
                                                  const PaperFaultPolicy& faults,
                                                  const std::array<std::byte, 16>& cluster_incarnation) {
    auto loaded = load_paper_state(file);
    if (loaded.error != PaperStateError::none) return {loaded.error, nullptr};
    if (loaded.state->origin.cluster_incarnation != cluster_incarnation) return {PaperStateError::other_cluster, nullptr};
    std::unique_ptr<DurablePaperVenue> venue(new DurablePaperVenue(file, config, faults, loaded.state->origin));
    if (!venue->venue_.configured() || !venue->venue_.restore(loaded.state->venue))
        return {PaperStateError::other_venue, nullptr};
    venue->stored_ = encode_paper_state(*loaded.state);
    return {PaperStateError::none, std::move(venue)};
}

bool DurablePaperVenue::persist() {
    if (failed_) return false;
    scratch_->origin = origin_;
    venue_.capture(scratch_->venue);
    auto bytes = encode_paper_state(*scratch_);
    if (bytes == stored_) return true;
    if (store_paper_state(file_, bytes) != PaperStateError::none) {
        failed_ = true;
        return false;
    }
    stored_ = std::move(bytes);
    return true;
}

SendOutcome DurablePaperVenue::send(void* self, const OrderEffect& effect, const Tick now) {
    auto& venue = *static_cast<DurablePaperVenue*>(self);
    if (venue.failed_) return SendOutcome::pre_send_failure;
    const auto outcome = venue.venue_.send(effect, now);
    if (!venue.persist() && outcome != SendOutcome::pre_send_failure) return SendOutcome::ambiguous;
    return outcome;
}

std::size_t DurablePaperVenue::poll(void* self, const std::span<VenueObservation> out, const Tick now) {
    auto& venue = *static_cast<DurablePaperVenue*>(self);
    return venue.failed_ ? 0 : venue.venue_.poll(out, now);
}

void DurablePaperVenue::acknowledge(void* self, const VenueObservation& observation) {
    auto& venue = *static_cast<DurablePaperVenue*>(self);
    if (venue.failed_) return;
    venue.venue_.acknowledge(observation);
    (void)venue.persist();
}

QueryResult DurablePaperVenue::query(void* self, const ClusterOrderId order, const Tick now) {
    auto& venue = *static_cast<DurablePaperVenue*>(self);
    const QueryResult silence{QueryOutcome::unavailable, {}, {}, {}, false, false, false, false};
    if (venue.failed_) return silence;
    const auto answer = venue.venue_.query(order, now);
    return venue.persist() ? answer : silence;
}

VenueSessionId DurablePaperVenue::session(void* self) {
    return static_cast<DurablePaperVenue*>(self)->venue_.session();
}

void DurablePaperVenue::read_book(void* self, sim::VenueBook& out) {
    auto& venue = *static_cast<DurablePaperVenue*>(self);
    venue.venue_.read_book(out);
    if (venue.failed_) out.available = false;
}

void DurablePaperVenue::committed(void* self, const sim::CommittedEvidence& evidence) {
    static_cast<DurablePaperVenue*>(self)->venue_.record_committed(evidence);
}

sim::VenueTransport DurablePaperVenue::transport() noexcept {
    sim::VenueTransport value;
    value.send_fn = &DurablePaperVenue::send;
    value.poll_fn = &DurablePaperVenue::poll;
    value.acknowledge_fn = &DurablePaperVenue::acknowledge;
    value.query_fn = &DurablePaperVenue::query;
    value.session_fn = &DurablePaperVenue::session;
    value.context = this;
    return value;
}

sim::VenueBookSource DurablePaperVenue::book() noexcept {
    return {&DurablePaperVenue::read_book, &DurablePaperVenue::committed, this};
}

}
