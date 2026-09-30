#include "velocita/v2/digest.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::atomic<std::size_t> allocations{};
}

void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* value = std::malloc(size)) return value;
    throw std::bad_alloc{};
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::span<const std::byte> bytes(std::string_view value) {
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

void require_digest(const Digest256& actual, std::string_view expected,
                    std::string_view context) {
    require(to_hex(actual) == expected, std::string(context) + " digest mismatch");
}

void test_standard_vectors_and_padding_boundaries() {
    struct Vector { std::string input; std::string_view expected; };
    const std::array<Vector, 7> vectors{{
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {std::string(55, 'a'), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {std::string(56, 'a'), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {std::string(63, 'a'), "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {std::string(64, 'a'), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {std::string(65, 'a'), "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    }};
    for (const auto& vector : vectors) {
        Sha256 digest;
        digest.update(bytes(vector.input));
        require_digest(digest.finish(), vector.expected,
                       "stream length " + std::to_string(vector.input.size()));
        require(digest.finish() == sha256_digest(bytes(vector.input)),
                "one-shot and streaming SHA-256 differ");
    }
}

void test_chunks_repeatable_finish_and_continuation() {
    constexpr std::string_view message =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Sha256 digest;
    digest.update({});
    for (std::size_t offset = 0, chunk = 1; offset < message.size(); ++chunk) {
        const auto count = std::min(chunk, message.size() - offset);
        digest.update(bytes(message).subspan(offset, count));
        digest.update({});
        offset += count;
    }
    constexpr std::string_view expected =
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
    require_digest(digest.finish(), expected, "irregular chunks");
    require_digest(digest.finish(), expected, "repeated finish");

    Sha256 continuation;
    continuation.update(bytes("abc"));
    require_digest(continuation.finish(),
                   "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                   "prefix finish");
    continuation.update(bytes("def"));
    require_digest(continuation.finish(),
                   "bef57ec7f53a6d40beb640a780a639c83bc29ac8a9816f1fc6c5c6dcd93c4721",
                   "update after finish");

    Sha256 first;
    Sha256 second;
    first.update(bytes("abc"));
    second.update(bytes("abcdef"));
    require_digest(first.finish(),
                   "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                   "independent first state");
    require_digest(second.finish(),
                   "bef57ec7f53a6d40beb640a780a639c83bc29ac8a9816f1fc6c5c6dcd93c4721",
                   "independent second state");
}

void test_million_a_and_no_hash_allocations() {
    const std::string block(1'000, 'a');
    Sha256 digest;
    const auto before = allocations.load(std::memory_order_relaxed);
    for (int index = 0; index < 1'000; ++index) digest.update(bytes(block));
    const auto actual = digest.finish();
    const auto after = allocations.load(std::memory_order_relaxed);
    require(after == before, "Sha256 update/finish allocated memory");
    require_digest(actual,
                   "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
                   "million a");
}
}  // namespace

int main() {
    try {
        static_assert(sizeof(Sha256) <= 112, "incremental SHA-256 owner exceeded bounded state");
        test_standard_vectors_and_padding_boundaries();
        test_chunks_repeatable_finish_and_continuation();
        test_million_a_and_no_hash_allocations();
        std::cout << "incremental digest tests passed; sizeof(Sha256)=" << sizeof(Sha256) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "incremental digest tests failed: " << error.what() << '\n';
        return 1;
    }
}
