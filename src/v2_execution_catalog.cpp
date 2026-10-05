#include "velocita/v2/digest.hpp"
#include "velocita/v2/execution_catalog.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <type_traits>

namespace velocita::v2 {
namespace {
constexpr std::array magic{std::byte{'V'}, std::byte{'2'}, std::byte{'E'},
                           std::byte{'C'}};
constexpr std::array domain{
    std::byte{'v'}, std::byte{'e'}, std::byte{'l'}, std::byte{'o'},
    std::byte{'c'}, std::byte{'i'}, std::byte{'t'}, std::byte{'a'},
    std::byte{'.'}, std::byte{'e'}, std::byte{'x'}, std::byte{'e'},
    std::byte{'c'}, std::byte{'u'}, std::byte{'t'}, std::byte{'i'},
    std::byte{'o'}, std::byte{'n'}, std::byte{'-'}, std::byte{'c'},
    std::byte{'a'}, std::byte{'t'}, std::byte{'a'}, std::byte{'l'},
    std::byte{'o'}, std::byte{'g'}, std::byte{'.'}, std::byte{'v'},
    std::byte{'1'}, std::byte{0}};
constexpr std::size_t kCatalogHeaderBytes = 12;
constexpr std::size_t kAccountIdBytes = 8;
constexpr std::size_t kInstrumentProfileBytes = 66;
constexpr std::size_t kVenueCapabilitiesBytes = 21;
constexpr std::size_t kFactorBytes = 8 + 8 + 1;
constexpr std::size_t kEncoderIdentityBytes = 2 + 2;
constexpr std::size_t kTextLengthBytes = 4;
constexpr std::size_t kFixedRowBytes =
    kAccountIdBytes + kInstrumentProfileBytes + kVenueCapabilitiesBytes +
    kFactorBytes + kEncoderIdentityBytes + kTextLengthBytes;
static_assert(kFixedRowBytes == 120);
static_assert(kCatalogHeaderBytes + kMaxExecutionCatalogRows *
                  (kFixedRowBytes + 4 * kMaxCatalogTextBytes) == 8076);
static_assert(8076 <= kMaxExecutionCatalogBytes);

class Writer {
public:
    explicit Writer(const std::span<std::byte> bytes) : bytes_(bytes) {}

    template <class Value> void put(const Value value) {
        if (remaining() < sizeof(Value)) {
            valid_ = false;
            return;
        }
        using Unsigned = std::make_unsigned_t<Value>;
        const auto bits = static_cast<Unsigned>(value);
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            bytes_[offset_++] =
                std::byte((bits >> (8 * index)) & static_cast<Unsigned>(255));
        }
    }

    void raw(const std::span<const std::byte> value) {
        if (remaining() < value.size()) {
            valid_ = false;
            return;
        }
        std::copy(value.begin(), value.end(),
                  bytes_.begin() + static_cast<std::ptrdiff_t>(offset_));
        offset_ += value.size();
    }

    [[nodiscard]] bool finished() const noexcept {
        return valid_ && offset_ == bytes_.size();
    }

private:
    [[nodiscard]] std::size_t remaining() const noexcept {
        return offset_ <= bytes_.size() ? bytes_.size() - offset_ : 0;
    }

    std::span<std::byte> bytes_;
    std::size_t offset_{};
    bool valid_{true};
};
class Reader {
public:
    explicit Reader(std::span<const std::byte> b) : b_(b) {}
    template <class T> T get() {
        using U = std::make_unsigned_t<T>;
        if (left() < sizeof(T)) {
            ok_ = false;
            return {};
        }
        U u{};
        for (std::size_t i = 0; i < sizeof(T); ++i)
            u |= static_cast<U>(std::to_integer<unsigned char>(b_[o_ + i]))
                 << (8 * i);
        o_ += sizeof(T);
        if constexpr (std::is_signed_v<T>)
            return std::bit_cast<T>(u);
        else
            return u;
    }
    bool raw(std::span<std::byte> v) {
        if (left() < v.size()) {
            ok_ = false;
            return false;
        }
        std::copy_n(b_.begin() + static_cast<std::ptrdiff_t>(o_), v.size(),
                    v.begin());
        o_ += v.size();
        return true;
    }
    bool ok() const { return ok_; }
    bool done() const { return ok_ && o_ == b_.size(); }

private:
    std::size_t left() const { return o_ <= b_.size() ? b_.size() - o_ : 0; }
    std::span<const std::byte> b_;
    std::size_t o_{};
    bool ok_{true};
};

bool text_ok(const CatalogText &t) {
    if (t.length == 0 || t.length > kMaxCatalogTextBytes)
        return false;
    for (std::size_t i = 0; i < t.length; ++i) {
        auto c = static_cast<unsigned char>(t.bytes[i]);
        if (c < 0x20 || c > 0x7e)
            return false;
    }
    return true;
}
bool factor_ok(const ExecutionCatalogRow &r) {
    if (r.tick_quote_micros <= 0 || r.lot_base_atoms <= 0 ||
        r.base_decimals > 18)
        return false;
    __int128 scale = 1;
    for (std::uint8_t i = 0; i < r.base_decimals; ++i)
        scale *= 10;
    const __int128 product =
        static_cast<__int128>(r.tick_quote_micros) * r.lot_base_atoms;
    return product % scale == 0 &&
           product / scale == r.instrument.quote_micros_per_tick_lot.atoms();
}
bool logical_equal(const CatalogText &a, const CatalogText &b) {
    return a.length == b.length &&
           std::equal(a.bytes.begin(), a.bytes.begin() + a.length,
                      b.bytes.begin());
}
bool row_ok(const ExecutionCatalogRow &r) {
    return r.account_id.value() != 0 &&
           validate(r.instrument) == ContractError::none &&
           validate(r.capabilities) == ContractError::none &&
           r.instrument.venue_id == r.capabilities.venue_id &&
           r.encoder_family_id != 0 && r.encoder_schema_version != 0 &&
           factor_ok(r) && text_ok(r.network_identity) &&
           text_ok(r.base_asset_identity) && text_ok(r.quote_asset_identity) &&
           text_ok(r.routing_key) &&
           !logical_equal(r.base_asset_identity, r.quote_asset_identity);
}
auto key(const ExecutionCatalogRow &r) {
    return std::array<std::uint64_t, 5>{
        r.account_id.value(), r.instrument.venue_id.value(),
        r.instrument.instrument_id.value(),
        r.instrument.instrument_spec_version.value(),
        r.capabilities.venue_capabilities_version.value()};
}
bool same_instrument(const ExecutionCatalogRow &a,
                     const ExecutionCatalogRow &b) {
    return a.instrument.venue_id == b.instrument.venue_id &&
           a.instrument.instrument_id == b.instrument.instrument_id &&
           a.instrument.instrument_spec_version ==
               b.instrument.instrument_spec_version;
}
bool same_facts(const ExecutionCatalogRow &a, const ExecutionCatalogRow &b) {
    return a.instrument == b.instrument &&
           a.tick_quote_micros == b.tick_quote_micros &&
           a.lot_base_atoms == b.lot_base_atoms &&
           a.base_decimals == b.base_decimals &&
           a.encoder_family_id == b.encoder_family_id &&
           a.encoder_schema_version == b.encoder_schema_version &&
           logical_equal(a.network_identity, b.network_identity) &&
           logical_equal(a.base_asset_identity, b.base_asset_identity) &&
           logical_equal(a.quote_asset_identity, b.quote_asset_identity) &&
           logical_equal(a.routing_key, b.routing_key);
}
ExecutionCatalogError validate_rows(std::span<const ExecutionCatalogRow> rows) {
    if (rows.empty() || rows.size() > kMaxExecutionCatalogRows)
        return ExecutionCatalogError::row_count_out_of_range;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!row_ok(rows[i]))
            return ExecutionCatalogError::invalid_value;
        if (i && key(rows[i]) == key(rows[i - 1]))
            return ExecutionCatalogError::duplicate_key;
        if (i && key(rows[i]) < key(rows[i - 1]))
            return ExecutionCatalogError::non_canonical_order;
        for (std::size_t j = 0; j < i; ++j) {
            if (rows[j].account_id == rows[i].account_id &&
                (!logical_equal(rows[j].network_identity,
                                rows[i].network_identity) ||
                 !logical_equal(rows[j].quote_asset_identity,
                                rows[i].quote_asset_identity)))
                return ExecutionCatalogError::account_denomination_conflict;
            if (same_instrument(rows[j], rows[i]) &&
                !same_facts(rows[j], rows[i]))
                return ExecutionCatalogError::cross_row_conflict;
            if (rows[j].capabilities.venue_id ==
                    rows[i].capabilities.venue_id &&
                rows[j].capabilities.venue_capabilities_version ==
                    rows[i].capabilities.venue_capabilities_version &&
                rows[j].capabilities != rows[i].capabilities)
                return ExecutionCatalogError::cross_row_conflict;
        }
    }
    return ExecutionCatalogError::none;
}
void write_text(Writer &w, const CatalogText &t) {
    w.put(t.length);
    w.raw(std::as_bytes(std::span{t.bytes}).first(t.length));
}
void write_row(Writer &w, const ExecutionCatalogRow &r) {
    w.put(r.account_id.value());
    auto &p = r.instrument;
    w.put(p.schema_version);
    w.put(p.venue_id.value());
    w.put(p.instrument_id.value());
    w.put(p.instrument_spec_version.value());
    w.put(p.quote_micros_per_tick_lot.atoms());
    w.put(p.minimum_price.atoms());
    w.put(p.maximum_price.atoms());
    w.put(p.minimum_quantity.atoms());
    w.put(p.maximum_quantity.atoms());
    auto &c = r.capabilities;
    w.put(c.schema_version);
    w.put(c.venue_id.value());
    w.put(c.venue_capabilities_version.value());
    w.put(c.directive_mask);
    w.put(c.time_in_force_mask);
    w.put(c.supports_post_only);
    w.put(r.tick_quote_micros);
    w.put(r.lot_base_atoms);
    w.put(r.base_decimals);
    w.put(r.encoder_family_id);
    w.put(r.encoder_schema_version);
    write_text(w, r.network_identity);
    write_text(w, r.base_asset_identity);
    write_text(w, r.quote_asset_identity);
    write_text(w, r.routing_key);
}
bool read_text(Reader &r, CatalogText &t) {
    auto n = r.get<std::uint8_t>();
    if (!r.ok() || n == 0 || n > kMaxCatalogTextBytes)
        return false;
    t.length = n;
    return r.raw(std::as_writable_bytes(std::span{t.bytes}).first(n));
}
bool read_row(Reader &r, ExecutionCatalogRow &x) {
    x.account_id = AccountId{r.get<std::uint64_t>()};
    auto &p = x.instrument;
    p.schema_version = r.get<std::uint16_t>();
    p.venue_id = VenueId{r.get<std::uint64_t>()};
    p.instrument_id = InstrumentId{r.get<std::uint64_t>()};
    p.instrument_spec_version = InstrumentSpecVersion{r.get<std::uint64_t>()};
    p.quote_micros_per_tick_lot =
        MoneyMicros::from_atoms(r.get<std::int64_t>());
    p.minimum_price = PriceTicks::from_atoms(r.get<std::int64_t>());
    p.maximum_price = PriceTicks::from_atoms(r.get<std::int64_t>());
    p.minimum_quantity = QuantityLots::from_atoms(r.get<std::int64_t>());
    p.maximum_quantity = QuantityLots::from_atoms(r.get<std::int64_t>());
    auto &c = x.capabilities;
    c.schema_version = r.get<std::uint16_t>();
    c.venue_id = VenueId{r.get<std::uint64_t>()};
    c.venue_capabilities_version =
        VenueCapabilitiesVersion{r.get<std::uint64_t>()};
    c.directive_mask = r.get<std::uint8_t>();
    c.time_in_force_mask = r.get<std::uint8_t>();
    c.supports_post_only = r.get<std::uint8_t>();
    x.tick_quote_micros = r.get<std::int64_t>();
    x.lot_base_atoms = r.get<std::int64_t>();
    x.base_decimals = r.get<std::uint8_t>();
    x.encoder_family_id = r.get<std::uint16_t>();
    x.encoder_schema_version = r.get<std::uint16_t>();
    return r.ok() && read_text(r, x.network_identity) &&
           read_text(r, x.base_asset_identity) &&
           read_text(r, x.quote_asset_identity) && read_text(r, x.routing_key);
}
Digest256 root_digest(std::span<const std::byte> b) {
    std::array<std::byte, domain.size() + kMaxExecutionCatalogBytes> p{};
    std::copy(domain.begin(), domain.end(), p.begin());
    std::copy(b.begin(), b.end(),
              p.begin() + static_cast<std::ptrdiff_t>(domain.size()));
    return sha256_digest(
        std::span<const std::byte>{p}.first(domain.size() + b.size()));
}
}

EncodeExecutionCatalogResult
encode_execution_catalog(std::span<const ExecutionCatalogRow> rows,
                         std::span<std::byte> out) noexcept {
    auto e = validate_rows(rows);
    if (e != ExecutionCatalogError::none)
        return {e, 0, {}};
    std::size_t n = kCatalogHeaderBytes;
    for (auto &r : rows)
        n += kFixedRowBytes + r.network_identity.length +
             r.base_asset_identity.length + r.quote_asset_identity.length +
             r.routing_key.length;
    if (n > kMaxExecutionCatalogBytes || out.size() < n)
        return {ExecutionCatalogError::output_too_small, 0, {}};
    std::array<std::byte, kMaxExecutionCatalogBytes> scratch{};
    Writer w(std::span<std::byte>{scratch}.first(n));
    w.raw(magic);
    w.put(kExecutionCatalogSchemaVersion);
    w.put(static_cast<std::uint16_t>(rows.size()));
    w.put(static_cast<std::uint32_t>(n));
    for (auto &r : rows)
        write_row(w, r);
    if (!w.finished())
        return {ExecutionCatalogError::invalid_value, 0, {}};
    auto digest = root_digest(std::span<const std::byte>{scratch}.first(n));
    std::copy_n(scratch.begin(), n, out.begin());
    return {ExecutionCatalogError::none, n, digest};
}

ExecutionCatalogError
VerifiedExecutionCatalog::decode(std::span<const std::byte> b,
                                 const Digest256 &expected,
                                 VerifiedExecutionCatalog &dest) noexcept {
    if (expected.is_zero())
        return ExecutionCatalogError::invalid_value;
    if (b.size() < kCatalogHeaderBytes)
        return ExecutionCatalogError::truncated;
    if (b.size() > kMaxExecutionCatalogBytes)
        return ExecutionCatalogError::length_mismatch;
    if (!std::equal(magic.begin(), magic.end(), b.begin()))
        return ExecutionCatalogError::invalid_magic;
    Reader r(b.subspan(4));
    auto schema = r.get<std::uint16_t>();
    auto count = r.get<std::uint16_t>();
    auto length = r.get<std::uint32_t>();
    if (schema != kExecutionCatalogSchemaVersion)
        return ExecutionCatalogError::unsupported_schema;
    if (count == 0 || count > kMaxExecutionCatalogRows)
        return ExecutionCatalogError::row_count_out_of_range;
    if (length > b.size())
        return ExecutionCatalogError::truncated;
    if (length != b.size())
        return ExecutionCatalogError::length_mismatch;
    VerifiedExecutionCatalog candidate;
    candidate.row_count_ = count;
    for (std::size_t i = 0; i < count; ++i)
        if (!read_row(r, candidate.rows_[i]))
            return ExecutionCatalogError::truncated;
    if (!r.done())
        return ExecutionCatalogError::length_mismatch;
    auto e = validate_rows(
        std::span<const ExecutionCatalogRow>{candidate.rows_}.first(count));
    if (e != ExecutionCatalogError::none)
        return e;
    candidate.root_ = root_digest(b);
    if (candidate.root_ != expected)
        return ExecutionCatalogError::digest_mismatch;
    dest = candidate;
    return ExecutionCatalogError::none;
}

const ExecutionCatalogRow *
VerifiedExecutionCatalog::lookup(AccountId a, VenueId v, InstrumentId i,
                                 InstrumentSpecVersion s,
                                 VenueCapabilitiesVersion c) const noexcept {
    for (std::size_t n = 0; n < row_count_; ++n) {
        const auto &r = rows_[n];
        if (r.account_id == a && r.instrument.venue_id == v &&
            r.instrument.instrument_id == i &&
            r.instrument.instrument_spec_version == s &&
            r.capabilities.venue_capabilities_version == c)
            return &r;
    }
    return nullptr;
}
}
