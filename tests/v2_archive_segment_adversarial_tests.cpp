#include "velocita/v2/archive_segment.hpp"
#include "velocita/v2/authority_barrier.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace velocita::v2;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<std::byte> hex(std::string_view text) {
    std::vector<std::byte> result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<std::byte>(std::stoul(std::string{text.substr(i, 2)}, nullptr, 16)));
    return result;
}
VerifiedStaticIdentity identity() {
    const auto bytes = hex(
        "563249447c000000010000000102030405060708090a0b0c0d0e0f10"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "0c000c0001000100020001000101030011000000010100000100010011000000"
        "4a5cd70c7355448c05ae9d61053151ab8202198996d3317eecfc6a857d353719");
    VerifiedStaticIdentity result;
    require(VerifiedStaticIdentity::decode(bytes, result) == StaticIdentityCodecError::none,
            "identity fixture");
    return result;
}
VerifiedStaticIdentity verify(StaticIdentity value) {
    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
    const auto encoded = encode_static_identity(value, bytes);
    require(encoded.error == StaticIdentityCodecError::none, "encode alternate identity");
    VerifiedStaticIdentity result;
    require(VerifiedStaticIdentity::decode(std::span{bytes}.first(encoded.bytes_written), result) ==
                StaticIdentityCodecError::none, "verify alternate identity");
    return result;
}

const std::vector<std::byte>& golden() {
    static const auto bytes = hex(
        "56324153010060007402000002000000110000000000000012000000000000000300000000000000"
        "040000000000000006000000000000007b6a13e548fef5ff55053c1c513155ab40409fae8a165c5b"
        "d36e064d43a2cd7a0000000000000000110000000000000004000000000000000000008065000000"
        "ba0000000000000056324142900000000100a0a1a2a3a4a5a6a7a8a9aaabacadaeaf030000000000"
        "00000500000000000000404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d"
        "5e5f07000000000000000b00000000000000808182838485868788898a8b8c8d8e8f909192939495"
        "969798999a9b9c9d9e9f2af7e8c9814b571e927bba39f81b642aa3b31577fbd12361b3a8c3a36cd3"
        "cb1924a61bdbd89462ee32b167a70d07ed8ca4e66ec032be1d73493e64e71fcbba8d7e1997715783"
        "72d92eb2c9ffa317cb382b5f8c32b94e2cc05122995ce6dfc0a11200000000000000060000000000"
        "0000f9ffffff65000000ba0000000000000056324142900000000100a0a1a2a3a4a5a6a7a8a9aaab"
        "acadaeaf03000000000000000500000000000000404142434445464748494a4b4c4d4e4f50515253"
        "5455565758595a5b5c5d5e5f07000000000000000b00000000000000808182838485868788898a8b"
        "8c8d8e8f909192939495969798999a9b9c9d9e9f2af7e8c9814b571e927bba39f81b642aa3b31577"
        "fbd12361b3a8c3a36cd3cb1924a61bdbd89462ee32b167a70d07ed8ca4e66ec032be1d73493e64e7"
        "1fcbba8dbab7b21e293b566476835ee144aa22b23d53848af045e7dec8a4b7fce090263c0e15be84"
        "6aad2678fc44abae18a1bccb5ee2aef6c9b03216d9bdaf9ce9285412");
    return bytes;
}
std::span<const std::byte> domain(std::string_view value) {
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}
void put(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i)
        bytes[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 255);
}
void reseal_segment(std::vector<std::byte>& bytes) {
    Sha256 hash;
    hash.update(domain("velocita.v2.archive-segment.v1"));
    hash.update(std::span{bytes}.first(bytes.size() - 32));
    const auto digest = hash.finish();
    std::copy(digest.bytes.begin(), digest.bytes.end(), bytes.end() - 32);
}
void reseal_record(std::vector<std::byte>& bytes, std::size_t offset, std::size_t payload_size = 186) {
    Sha256 hash;
    hash.update(domain("velocita.v2.archive-record.v1"));
    hash.update(std::span{bytes}.subspan(56, 32));
    hash.update(std::span{bytes}.subspan(offset, 32 + payload_size));
    const auto digest = hash.finish();
    std::copy(digest.bytes.begin(), digest.bytes.end(), bytes.begin() + offset + 32 + payload_size);
    reseal_segment(bytes);
}

struct Visits {
    std::size_t calls{};
    std::size_t refuse_at{std::numeric_limits<std::size_t>::max()};
    bool exact{true};
};
bool visit(const ArchiveRecordView& record, void* context) noexcept {
    auto& state = *static_cast<Visits*>(context);
    const auto ordinal = state.calls++;
    if (ordinal >= 2) { state.exact = false; return false; }
    const auto payload = std::span{golden()}.subspan(128 + ordinal * 250, 186);
    state.exact = state.exact && record.index == 17 + ordinal &&
        record.term == (ordinal == 0 ? 4 : 6) && record.entry_type == 101 &&
        record.entry_id == (ordinal == 0 ? std::numeric_limits<std::int32_t>::min() : -7) &&
        std::equal(record.payload.begin(), record.payload.end(), payload.begin(), payload.end());
    return ordinal != state.refuse_at;
}
ArchiveSegmentMetadata sentinel() {
    ArchiveSegmentMetadata value{};
    value.first_index = 99; value.last_index = 103; value.predecessor_term = 13;
    value.first_term = 17; value.last_term = 19; value.record_count = 5;
    value.total_bytes = 789; value.segment_digest.bytes.fill(std::byte{0xa5});
    return value;
}
bool is_sentinel(const ArchiveSegmentMetadata& value) {
    const auto expected = sentinel();
    return value.first_index == expected.first_index && value.last_index == expected.last_index &&
        value.predecessor_term == expected.predecessor_term && value.first_term == expected.first_term &&
        value.last_term == expected.last_term && value.record_count == expected.record_count &&
        value.total_bytes == expected.total_bytes && value.segment_digest == expected.segment_digest;
}
void refused(std::span<const std::byte> bytes, const char* reason,
             const VerifiedStaticIdentity& expected = identity()) {
    ArchiveSegmentScratch scratch{};
    auto destination = sentinel();
    Visits visits;
    const auto result = decode_archive_segment(bytes, expected, visit, &visits, scratch, destination);
    require(result.error != ArchiveSegmentError::none, reason);
    require(visits.calls == 0, "invalid later record reached an earlier visitor");
    require(is_sentinel(destination), "refusal changed destination metadata");
}

struct Source {
    std::size_t calls{};
    std::size_t fail_on{std::numeric_limits<std::size_t>::max()};
};
bool record_at(void* context, std::uint32_t ordinal, ArchiveRecordView& destination) noexcept {
    auto& source = *static_cast<Source*>(context);
    if (++source.calls == source.fail_on || ordinal >= 2) return false;
    destination = {17 + ordinal, ordinal == 0 ? 4 : 6,
                   ordinal == 0 ? std::numeric_limits<std::int32_t>::min() : -7, 101,
                   std::span{golden()}.subspan(128 + ordinal * 250, 186)};
    return true;
}
void test_independent_golden_and_portable_identity() {
    require(golden().size() == 628, "independent golden length");
    const auto local = identity();
    auto other_value = local.value();
    other_value.local_node_id = 257;
    const auto other = verify(other_value);
    require(local.manifest_digest() == other.manifest_digest(), "local ID leaked into shared identity");
    for (const auto& expected : {local, other}) {
        ArchiveSegmentScratch scratch{};
        ArchiveSegmentMetadata metadata{};
        Visits visits;
        require(decode_archive_segment(golden(), expected, visit, &visits, scratch, metadata).error ==
                    ArchiveSegmentError::none, "independent marker golden refused");
        require(visits.calls == 2 && visits.exact, "archived tuples or full payload differ from authored oracle");
        require(metadata.first_index == 17 && metadata.last_index == 18 &&
                metadata.predecessor_term == 3 && metadata.first_term == 4 && metadata.last_term == 6 &&
                metadata.record_count == 2 && metadata.total_bytes == 628 &&
                to_hex(metadata.segment_digest) == "0e15be846aad2678fc44abae18a1bccb5ee2aef6c9b03216d9bdaf9ce9285412",
                "golden metadata mismatch");
        Source source;
        std::array<std::byte, 628> output{};
        const auto encoded = encode_archive_segment({&source, 2, record_at}, 3, expected, output, scratch);
        require(encoded.error == ArchiveSegmentError::none && encoded.bytes_written == 628,
                "golden encode refused");
        require(std::equal(output.begin(), output.end(), golden().begin()), "portable encoder differs from independent golden");
    }
    other_value.incarnation[0] ^= std::byte{1};
    refused(golden(), "foreign incarnation accepted", verify(other_value));
    refused(golden(), "unverified expected identity accepted", VerifiedStaticIdentity{});
}
void test_every_truncation_and_bit_flip() {
    for (std::size_t size = 0; size < golden().size(); ++size)
        refused(std::span{golden()}.first(size), "truncated archive accepted");
    auto bytes = golden();
    bytes.push_back(std::byte{});
    refused(bytes, "trailing archive byte accepted");
    bytes = golden();
    bytes.back() ^= std::byte{1};
    refused(bytes, "segment digest footer corruption accepted");
    for (std::size_t offset = 0; offset < golden().size(); ++offset) {
        bytes = golden();
        bytes[offset] ^= std::byte{1};
        refused(bytes, "single-byte archive corruption accepted");
    }
}
void test_resealed_semantic_corruption_before_any_visitor() {
    static_assert(kAuthorityBarrierEntryType != -1 && kSwapAuthorityBarrierEntryType != -1);
    struct Change { std::size_t offset; std::uint64_t value; std::size_t width; };
    for (const auto change : std::array{
        Change{346, 19, 8},
        Change{354, 3, 8},
        Change{366, 0xffffffffU, 4},
        Change{374, 1, 4},
    }) {
        auto bytes = golden();
        put(bytes, change.offset, change.value, change.width);
        if (change.offset == 354) put(bytes, 48, 3, 8);
        reseal_record(bytes, 346);
        refused(bytes, change.offset == 346 ? "resealed second-record index gap accepted" :
                       change.offset == 354 ? "resealed decreasing terms accepted" :
                       change.offset == 366 ? "resealed unknown entry type accepted" :
                                              "resealed record reserved byte accepted");
    }
    for (const auto change : std::array{
        Change{12, 1, 4}, Change{16, 0, 8}, Change{24, 19, 8},
        Change{32, 0, 8}, Change{32, 5, 8}, Change{40, 5, 8},
        Change{48, 7, 8}, Change{88, 1, 4}, Change{92, 1, 4},
        Change{16, std::uint64_t{1} << 63, 8},
    }) {
        auto bytes = golden();
        put(bytes, change.offset, change.value, change.width);
        reseal_segment(bytes);
        refused(bytes, "resealed invalid header accepted");
    }
    auto bytes = golden();
    bytes[346 + 32 + 122] ^= std::byte{1};
    const auto marker_hash = sha256_digest(std::span{bytes}.subspan(346 + 32 + 8, 146));
    std::copy(marker_hash.bytes.begin(), marker_hash.bytes.end(), bytes.begin() + 346 + 32 + 154);
    reseal_record(bytes, 346);
    refused(bytes, "foreign marker authority domain accepted");

    bytes = golden();
    bytes[346 + 32 + 186] ^= std::byte{1};
    reseal_segment(bytes);
    refused(bytes, "bad record digest hidden by valid outer digest");
}
void test_partial_consumer_and_source_failure_contract() {
    ArchiveSegmentScratch scratch{};
    auto metadata = sentinel();
    Visits visits{0, 1, true};
    const auto decoded = decode_archive_segment(golden(), identity(), visit, &visits, scratch, metadata);
    require(decoded.error == ArchiveSegmentError::visitor_refused && visits.calls == 2 && visits.exact,
            "visitor refusal or consumed prefix not reported");
    require(is_sentinel(metadata), "visitor refusal published success metadata");

    std::array<std::byte, 628> output{};
    output.fill(std::byte{0xa5});
    Source source{0, 2};
    auto encoded = encode_archive_segment({&source, 2, record_at}, 3, identity(), output, scratch);
    require(encoded.error == ArchiveSegmentError::source_read_failed && encoded.bytes_written == 0,
            "preflight source error reported success");
    require(std::all_of(output.begin(), output.end(), [](auto byte) { return byte == std::byte{0xa5}; }),
            "preflight source error touched output");
    source = {0, 3};
    encoded = encode_archive_segment({&source, 2, record_at}, 3, identity(), output, scratch);
    require(encoded.error == ArchiveSegmentError::source_read_failed && encoded.bytes_written == 0,
            "second-pass source error reported a publishable segment");
}

void test_resealed_invalid_command_is_not_skipped() {
    const auto command = hex(
        "56324b43380000000c000600010000000000000001000000000000000100000000000000000000000000000000000000000000000000000000000000"
        "0100000000000000758705cbffdf0e83b706a857b56ace24f4617b3cbc0c2ab973629d0ba4fff64b");
    require(command.size() == 100, "command fixture length");
    std::vector<std::byte> bytes(456);
    std::copy_n(golden().begin(), 96, bytes.begin());
    put(bytes, 8, bytes.size(), 4);
    for (std::size_t ordinal = 0; ordinal < 2; ++ordinal) {
        const auto start = 96 + ordinal * 164;
        std::copy_n(golden().begin() + 96 + ordinal * 250, 32, bytes.begin() + start);
        put(bytes, start + 20, 0, 4);
        put(bytes, start + 24, command.size(), 4);
        std::copy(command.begin(), command.end(), bytes.begin() + start + 32);
        reseal_record(bytes, start, command.size());
    }
    ArchiveSegmentScratch scratch{};
    ArchiveSegmentMetadata metadata{};
    require(decode_archive_segment(bytes, identity(), nullptr, nullptr, scratch, metadata).error ==
                ArchiveSegmentError::none && metadata.record_count == 2,
            "valid repeated command history rejected");
    const auto second_payload = 96 + 164 + 32;
    put(bytes, second_payload + 8, kKernelCodecSchemaVersion + 1, 2);
    const auto digest = sha256_digest(std::span{bytes}.subspan(second_payload + 8, 60));
    std::copy(digest.bytes.begin(), digest.bytes.end(), bytes.begin() + second_payload + 68);
    reseal_record(bytes, 96 + 164, command.size());
    refused(bytes, "resealed unsupported command schema accepted");
}
}

int main() {
    try {
        test_independent_golden_and_portable_identity();
        test_every_truncation_and_bit_flip();
        test_resealed_semantic_corruption_before_any_visitor();
        test_partial_consumer_and_source_failure_contract();
        test_resealed_invalid_command_is_not_skipped();
        std::cout << "archive adversarial tests passed; sizeof(Sha256)=" << sizeof(Sha256)
                  << " scratch=" << sizeof(ArchiveSegmentScratch)
                  << " record_view=" << sizeof(ArchiveRecordView)
                  << " source=" << sizeof(ArchiveRecordSource)
                  << " metadata=" << sizeof(ArchiveSegmentMetadata) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "archive adversarial tests failed: " << error.what() << '\n';
        return 1;
    }
}
