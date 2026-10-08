#include "velocita/v2/bound_metadata.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {
using namespace velocita;
using namespace velocita::v2;

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
std::vector<std::byte> hex(const std::string& text) {
    std::vector<std::byte> result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<std::byte>(std::stoul(text.substr(i, 2), nullptr, 16)));
    return result;
}
VerifiedStaticIdentity identity() {
    auto bytes = hex(
        "563249447c000000010000000102030405060708090a0b0c0d0e0f10"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "0b000b0001000100010001000101030011000000010100000100010011000000"
        "1b3d30a9e57f85108ab28320f24135276cc02342c3cf0183b618c042aab007bb");
    VerifiedStaticIdentity result;
    require(VerifiedStaticIdentity::decode(bytes, result) == StaticIdentityCodecError::none,
            "identity fixture");
    return result;
}
std::array<std::byte, kBoundMetadataBytes> encode(PersistentMetadata value) {
    std::array<std::byte, kBoundMetadataBytes> result{};
    BoundFrameScratch scratch{};
    const auto encoded = encode_bound_metadata(value, identity(), result, scratch);
    require(encoded.error == BoundFrameError::none && encoded.bytes_written == result.size(),
            "metadata encode");
    return result;
}

void test_golden_and_semantics() {
    const auto expected = hex(
        "56324246010001007000000018000000"
        "4a614f5d781d5d8fbb67a13c76d98dd5be8b161da30e14ce95c240a61798d30b"
        "1100000000000000"
        "070000000000000011000000000000000300000000000000"
        "bd1a042fd227ae08b1f4fb86290bfcc3bfee262a442756f4da906c9309f23bf4");
    const auto actual = encode({7, 17, 3});
    require(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()),
            "independent metadata golden changed");
    PersistentMetadata destination{99, 17, 88};
    BoundFrameScratch scratch{};
    require(decode_bound_metadata(expected, identity(), destination, scratch) ==
                BoundFrameError::none,
            "golden decode");
    require(destination.term == 7 && destination.voted_for == 17 && destination.commit_index == 3,
            "decoded fields");

    for (const auto invalid : {PersistentMetadata{-1, -1, 0}, PersistentMetadata{0, 1, 0},
                               PersistentMetadata{0, -1, 1}, PersistentMetadata{1, 99, 0},
                               PersistentMetadata{1, 0, 0}, PersistentMetadata{1, -1, -1}}) {
        std::array<std::byte, kBoundMetadataBytes> output{};
        output.fill(std::byte{0xa5});
        require(encode_bound_metadata(invalid, identity(), output, scratch).error ==
                    BoundFrameError::invalid_payload,
                "invalid tuple encoded");
        require(std::all_of(output.begin(), output.end(),
                            [](std::byte b) { return b == std::byte{0xa5}; }),
                "failed encode changed output");
    }
    auto corrupt = actual;
    corrupt[68] = std::byte{1};
    destination = {99, 17, 88};
    require(decode_bound_metadata(corrupt, identity(), destination, scratch) ==
                BoundFrameError::digest_mismatch,
            "corruption accepted");
    require(destination.term == 99 && destination.voted_for == 17 && destination.commit_index == 88,
            "failed decode changed destination");
}

void test_store_bounds_temp_and_symlink() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("velocita-bound-metadata-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directory(root);
    BoundMetadataStore store(root, identity());
    try {
        bool absent{};
        try { (void)store.load(); }
        catch (const BoundMetadataError& error) { absent = error.kind() == BoundMetadataErrorKind::absent; }
        require(absent, "missing metadata defaulted");
        const auto initial = encode({2, 257, 1});
        std::ofstream output(store.path(), std::ios::binary);
        output.write(reinterpret_cast<const char*>(initial.data()), initial.size());
        output.close();
        const auto loaded = store.load();
        require(loaded.term == 2 && loaded.voted_for == 257 && loaded.commit_index == 1,
                "store round trip");

        std::filesystem::copy_file(store.path(), store.temporary_path());
        require(store.load().term == 2 && std::filesystem::exists(store.temporary_path()),
                "load promoted or refused valid temp");
        store.save({3, 17, 2});
        require(store.load().term == 3 && !std::filesystem::exists(store.temporary_path()),
                "save did not replace valid stale temp");

        std::filesystem::create_symlink(store.path(), store.temporary_path());
        bool refused{};
        try { (void)store.load(); }
        catch (const BoundMetadataError& error) {
            refused = error.kind() == BoundMetadataErrorKind::invalid_temp_lineage;
        }
        require(refused, "temporary symlink accepted");
        std::filesystem::remove(store.temporary_path());

        std::filesystem::resize_file(store.path(), kBoundMetadataBytes + 1);
        bool oversized{};
        try { (void)store.load(); }
        catch (const BoundMetadataError& error) {
            oversized = error.kind() == BoundMetadataErrorKind::frame &&
                        error.frame_error() == BoundFrameError::length_mismatch;
        }
        require(oversized, "oversized metadata accepted");
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }
    std::filesystem::remove_all(root);
}
}

int main() {
    try {
        test_golden_and_semantics();
        test_store_bounds_temp_and_symlink();
        std::cout << "bound metadata tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "bound metadata tests failed: " << error.what() << '\n';
        return 1;
    }
}
