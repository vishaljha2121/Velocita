#include "velocita/v2/local_generation.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace velocita::v2;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<std::byte> hex(std::string_view value) {
    std::vector<std::byte> result;
    for (std::size_t i = 0; i < value.size(); i += 2)
        result.push_back(static_cast<std::byte>(std::stoul(std::string{value.substr(i, 2)}, nullptr, 16)));
    return result;
}
VerifiedStaticIdentity identity() {
    const auto bytes = hex(
        "563249447c000000010000000102030405060708090a0b0c0d0e0f10"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "0b000b0001000100010001000101030011000000010100000100010011000000"
        "1b3d30a9e57f85108ab28320f24135276cc02342c3cf0183b618c042aab007bb");
    VerifiedStaticIdentity id;
    require(VerifiedStaticIdentity::decode(bytes, id) == StaticIdentityCodecError::none,
            "independent identity fixture refused");
    return id;
}
const std::vector<std::byte>& golden() {
    static const auto bytes = hex(
        "56324c4701008000a0000000000000004a614f5d781d5d8fbb67a13c76d98dd5be8b161da30e14ce95c240a61798d30b"
        "1100000000000000080000000000000003000000000000000700000000000000"
        "a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
        "00000000000000000000000000000000"
        "b0599cdd2ceebf28431c1a2b37af4cf0751c832fc79f30c94291841e2a853985");
    return bytes;
}
LocalGenerationSelector authored() {
    LocalGenerationSelector value{8, 3, 7, {}};
    for (std::size_t i = 0; i < 32; ++i)
        value.prepared_base_digest.bytes[i] = static_cast<std::byte>(160 + i);
    return value;
}
void put(std::vector<std::byte>& bytes, std::size_t at, std::uint64_t value,
         std::size_t width = 8) {
    for (std::size_t i = 0; i < width; ++i)
        bytes[at+i] = static_cast<std::byte>(value >> (8*i));
}
void reseal(std::vector<std::byte>& bytes) {
    constexpr std::string_view domain = "velocita.v2.local-generation-selector.v1";
    Sha256 hash;
    hash.update({reinterpret_cast<const std::byte*>(domain.data()), domain.size()});
    hash.update(std::span{bytes}.first(128));
    const auto digest = hash.finish();
    std::copy(digest.bytes.begin(), digest.bytes.end(), bytes.begin() + 128);
}
void refused(std::span<const std::byte> bytes, const std::string& why,
             LocalGenerationError expected = LocalGenerationError::none) {
    auto output = authored(); output.generation = 99;
    const auto before = output;
    const auto result = decode_local_generation_selector(bytes, identity(), output);
    require(result != LocalGenerationError::none, why + " accepted");
    if (expected != LocalGenerationError::none)
        require(result == expected, why + " wrong refusal");
    require(output == before, why + " changed destination");
}
void golden_and_failures() {
    require(golden().size() == 160, "literal length");
    LocalGenerationSelector result;
    require(decode_local_generation_selector(golden(), identity(), result) ==
            LocalGenerationError::none && result == authored(), "independent golden decode");
    std::array<std::byte, 167> bytes; bytes.fill(std::byte{0x55});
    auto encoded = encode_local_generation_selector(authored(), identity(), bytes);
    require(encoded.error == LocalGenerationError::none && encoded.bytes_written == 160,
            "independent golden encode");
    require(std::equal(golden().begin(), golden().end(), bytes.begin()), "literal byte mismatch");
    require(std::all_of(bytes.begin()+160, bytes.end(), [](auto b){return b == std::byte{0x55};}),
            "encoder changed tail");
    for (std::size_t n = 0; n < 160; ++n) {
        refused(std::span{golden()}.first(n), "truncation " + std::to_string(n));
        bytes.fill(std::byte{0x55}); const auto before = bytes;
        encoded = encode_local_generation_selector(authored(), identity(), std::span{bytes}.first(n));
        require(encoded.error == LocalGenerationError::output_too_small &&
                encoded.bytes_written == 0 && bytes == before, "short encode modified output");
    }
    auto extra = golden(); extra.push_back(std::byte{}); refused(extra, "trailing byte");
    for (std::size_t i = 0; i < 160; ++i) {
        auto bad = golden(); bad[i] ^= std::byte{1};
        refused(bad, "corruption " + std::to_string(i));
    }
    auto check_field = [](std::size_t at, std::uint64_t value, std::size_t width,
                          LocalGenerationError error, const char* label) {
        auto bad = golden(); put(bad, at, value, width); reseal(bad);
        refused(bad, label, error);
    };
    check_field(48, 257, 4, LocalGenerationError::wrong_owner, "other configured voter");
    auto foreign = golden(); foreign[16] ^= std::byte{1}; reseal(foreign);
    refused(foreign, "foreign manifest", LocalGenerationError::foreign_manifest);
    check_field(56, 0, 8, LocalGenerationError::invalid_value, "zero generation");
    check_field(64, 0, 8, LocalGenerationError::invalid_value, "zero sequence");
    check_field(72, 0, 8, LocalGenerationError::invalid_value, "missing predecessor");
    check_field(72, 8, 8, LocalGenerationError::invalid_value, "same predecessor");
    check_field(72, 9, 8, LocalGenerationError::invalid_value, "newer predecessor");
    check_field(64, 1, 8, LocalGenerationError::invalid_value, "first selection with predecessor");
    for (auto at : {12U, 13U, 14U, 15U, 52U, 53U, 54U, 55U,
                    112U, 113U, 114U, 115U, 116U, 117U, 118U, 119U,
                    120U, 121U, 122U, 123U, 124U, 125U, 126U, 127U})
        check_field(at, 1, 1, LocalGenerationError::nonzero_reserved, "reserved byte");
    auto zero_base = golden(); std::fill(zero_base.begin()+80, zero_base.begin()+112, std::byte{});
    reseal(zero_base); refused(zero_base, "zero base digest", LocalGenerationError::invalid_value);
    auto bad_value = authored(); bad_value.prepared_base_digest = {};
    bytes.fill(std::byte{0x55}); const auto before = bytes;
    require(encode_local_generation_selector(bad_value, identity(), bytes).error !=
            LocalGenerationError::none && bytes == before, "invalid encode changed output");
    auto sentinel = authored(); const auto original = sentinel;
    require(decode_local_generation_selector(golden(), {}, sentinel) ==
            LocalGenerationError::invalid_identity && sentinel == original, "unverified identity decode");
    require(encode_local_generation_selector(authored(), {}, bytes).error ==
            LocalGenerationError::invalid_identity && bytes == before, "unverified identity encode");
}
void transitions() {
    const auto old = authored();
    std::size_t cases{};
    for (std::uint64_t generation = 6; generation <= 12; ++generation)
        for (std::uint64_t sequence = 1; sequence <= 6; ++sequence)
            for (std::uint64_t previous = 0; previous <= 12; ++previous) {
                auto next = authored(); next.generation = generation;
                next.selection_sequence = sequence; next.previous_generation = previous;
                const bool expected = generation > 8 && sequence == 4 && previous == 8;
                require((validate_local_generation_transition(old, next) == LocalGenerationError::none)
                        == expected, "transition matrix differs from authored arithmetic");
                ++cases;
            }
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    auto terminal = authored(); terminal.generation = maximum;
    terminal.selection_sequence = maximum; terminal.previous_generation = maximum - 1;
    std::array<std::byte, 160> bytes{};
    require(encode_local_generation_selector(terminal, identity(), bytes).error ==
            LocalGenerationError::none, "terminal counters cannot be serialized");
    LocalGenerationSelector decoded;
    require(decode_local_generation_selector(bytes, identity(), decoded) ==
            LocalGenerationError::none && decoded == terminal, "terminal counters cannot recover");
    auto wrapped = authored(); wrapped.generation = 1; wrapped.selection_sequence = 1;
    wrapped.previous_generation = 0;
    require(validate_local_generation_transition(terminal, wrapped) != LocalGenerationError::none,
            "exhausted counters wrapped");
    auto genesis = wrapped; genesis.generation = 5;
    auto next = authored(); next.generation = 8; next.selection_sequence = 2; next.previous_generation = 5;
    require(validate_local_generation_transition(genesis, next) == LocalGenerationError::none,
            "generation gap from failed preparation incorrectly refused");
    genesis.prepared_base_digest = {};
    require(validate_local_generation_transition(genesis, next) != LocalGenerationError::none,
            "invalid old value accepted");
    auto penultimate = authored(); penultimate.generation = maximum - 1;
    penultimate.selection_sequence = maximum - 1; penultimate.previous_generation = maximum - 2;
    require(validate_local_generation_transition(penultimate, terminal) == LocalGenerationError::none,
            "last nonwrapping transition incorrectly refused");
    std::cout << "transition cells=" << cases << " terminal counters recoverable, not advanceable\n";
}
void overlapping_storage() {
    struct Storage { LocalGenerationSelector value; std::array<std::byte, 160> extra; } storage{};
    auto bytes = std::as_writable_bytes(std::span{&storage, 1}).first(160);
    storage.value = authored();
    require(encode_local_generation_selector(storage.value, identity(), bytes).error ==
            LocalGenerationError::none, "overlapping encoder refused");
    require(std::equal(bytes.begin(), bytes.end(), golden().begin()), "encoder read input after overwriting it");
    require(decode_local_generation_selector(bytes, identity(), storage.value) ==
            LocalGenerationError::none && storage.value == authored(), "decoder published before finishing reads");
    std::cout << "native value bytes=" << sizeof(LocalGenerationSelector) << " wire bytes=160\n";
}
}
int main() {
    try {
        golden_and_failures(); transitions(); overlapping_storage();
        std::cout << "local generation independent bytes and adversarial checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "local generation adversarial: " << error.what() << '\n';
        return 1;
    }
}
