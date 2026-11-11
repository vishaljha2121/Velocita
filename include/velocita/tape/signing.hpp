// Ed25519 signing of tree heads, through OpenSSL. Keys are PEM files.
#pragma once

#include "velocita/tape/types.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <span>

namespace velocita::tape {

class SigningKey {
public:
    static SigningKey load(const std::filesystem::path& pem);
    static SigningKey generate();
    ~SigningKey();
    SigningKey(SigningKey&&) noexcept;
    SigningKey& operator=(SigningKey&&) noexcept;
    [[nodiscard]] std::array<std::byte, kSignatureBytes> sign(std::span<const std::byte> message) const;
    void save_private(const std::filesystem::path& pem) const;
    void save_public(const std::filesystem::path& pem) const;

private:
    explicit SigningKey(void* key) : key_(key) {}
    void* key_{};
};

class VerifyKey {
public:
    static VerifyKey load(const std::filesystem::path& pem);
    ~VerifyKey();
    VerifyKey(VerifyKey&&) noexcept;
    VerifyKey& operator=(VerifyKey&&) noexcept;
    [[nodiscard]] bool verify(std::span<const std::byte> message, std::span<const std::byte> signature) const;

private:
    explicit VerifyKey(void* key) : key_(key) {}
    void* key_{};
};

}
