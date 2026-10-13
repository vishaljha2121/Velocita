#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace velocita::v2::swap_signing {

inline constexpr std::size_t kMaxApiKeyStampBytes = 375;

enum class StampCode : std::uint8_t { encoded, invalid_public_key_encoding, invalid_der_signature };

class ApiKeyStamp final {
public:
    [[nodiscard]] std::string_view view() const noexcept { return {text_.data(), length_}; }
    [[nodiscard]] bool empty() const noexcept { return length_ == 0; }

private:
    friend StampCode encode_api_key_stamp(std::span<const std::byte>, std::span<const std::byte>, ApiKeyStamp&) noexcept;
    std::array<char, kMaxApiKeyStampBytes> text_{};
    std::uint16_t length_{};
};

[[nodiscard]] StampCode encode_api_key_stamp(std::span<const std::byte> compressed_public_key,
    std::span<const std::byte> der_signature, ApiKeyStamp& destination) noexcept;

}
