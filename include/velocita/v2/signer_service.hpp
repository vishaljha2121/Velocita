// The inspecting signer of ADR 0019: policy, replay protection, a durable sequence floor, an audit log and its Unix-socket wire.
#pragma once

#include "velocita/v2/venue_signer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace velocita::v2::signer {

inline constexpr std::size_t kMaxPayloadBytes = 4'096;
inline constexpr std::size_t kMaxHandleBytes = 128;
inline constexpr std::size_t kSignedHandleCache = 256;
inline constexpr std::size_t kRequestFixedBytes = 4 + 4 + 7 * 8 + 32 + 2 * 8 + 1 + 3 * 8 + 2 + 1;
inline constexpr std::size_t kMaxRequestBytes =
    kRequestFixedBytes + kMaxHandleBytes + kMaxSignPayloads * (2 + kMaxPayloadBytes) + 32;
inline constexpr std::size_t kMaxResponseBytes =
    4 + 4 + 1 + 8 + 4 + 1 + kMaxSignPayloads * (2 + kMaxSignatureBytes) + 32;
inline constexpr std::size_t kTestKeyBytes = 32;
inline constexpr std::string_view kTestKeyHeader = "velocita-test-signing-key-v1\n";
inline constexpr std::string_view kTestSignatureDomain = "velocita-test-signer/v1|";

struct DecodedSwap {
    Side side{};
    std::int64_t base_atoms{};
    std::int64_t quote_bound_atoms{};
    std::string_view handle{};
};

using PayloadDecoder = std::optional<DecodedSwap> (*)(std::string_view payload) noexcept;

[[nodiscard]] std::optional<DecodedSwap> decode_mock_swap_payload(std::string_view payload) noexcept;

struct SignerPolicy {
    CallerId caller{};
    VenueId venue_id{};
    AccountId account_id{};
    InstrumentId instrument_id{};
};

struct AuditRecord {
    std::uint64_t request_sequence{};
    EffectId effect_id{};
    SignerVerdict verdict{SignerVerdict::unavailable};
    std::uint32_t key_version{};
    PolicyVersion policy_version{};
    std::uint64_t signature_id{};
    Digest256 payload_digest{};

    constexpr bool operator==(const AuditRecord&) const noexcept = default;
};

struct KeyHolder {
    bool (*sign_fn)(void* context, std::string_view payload, Signature& out) noexcept {};
    void* context{};
    std::uint32_t key_version{};
};

struct AuditSink {
    bool (*append_fn)(void* context, const AuditRecord& record) noexcept {};
    void* context{};
};

struct SequenceStore {
    bool (*persist_fn)(void* context, std::uint64_t floor) noexcept {};
    void* context{};
};

struct SignerClock {
    sim::Tick (*now_fn)(void* context) noexcept {};
    void* context{};
};

class SignerCore final {
public:
    SignerCore(const SignerPolicy& policy, PayloadDecoder decoder, KeyHolder key, AuditSink audit,
               SequenceStore store, SignerClock clock, std::uint64_t sequence_floor) noexcept;

    [[nodiscard]] SignerResult sign(const SigningRequest& request) noexcept;
    [[nodiscard]] std::uint64_t sequence_floor() const noexcept { return floor_; }

private:
    [[nodiscard]] SignerResult refuse(const SigningRequest& request, SignerVerdict verdict) noexcept;
    [[nodiscard]] bool already_signed(std::string_view handle) const noexcept;

    SignerPolicy policy_;
    PayloadDecoder decoder_;
    KeyHolder key_;
    AuditSink audit_;
    SequenceStore store_;
    SignerClock clock_;
    std::uint64_t floor_;
    std::size_t handle_count_{};
    std::size_t handle_next_{};
    std::array<Digest256, kSignedHandleCache> handles_{};
};

struct OwnedSigningRequest {
    SigningRequest request{};
    std::vector<char> text;
};

enum class WireError : std::uint8_t { none, truncated, invalid_magic, length_mismatch, too_large, invalid_value, digest_mismatch };

struct EncodeResult {
    WireError error{WireError::none};
    std::size_t bytes_written{};
};

[[nodiscard]] EncodeResult encode_request(const SigningRequest& request, std::span<std::byte> output) noexcept;
[[nodiscard]] WireError decode_request(std::span<const std::byte> input, OwnedSigningRequest& destination);
[[nodiscard]] EncodeResult encode_response(const SignerResult& result, std::span<std::byte> output) noexcept;
[[nodiscard]] WireError decode_response(std::span<const std::byte> input, SignerResult& destination) noexcept;
[[nodiscard]] std::optional<std::size_t> frame_length(std::span<const std::byte> prefix) noexcept;

class TestKey final {
public:
    [[nodiscard]] static std::optional<TestKey> load(const std::filesystem::path& file) noexcept;
    [[nodiscard]] static std::string file_text(std::span<const std::byte, kTestKeyBytes> key);
    [[nodiscard]] KeyHolder holder(std::uint32_t key_version) noexcept;

private:
    static bool sign(void* context, std::string_view payload, Signature& out) noexcept;
    std::array<std::byte, kTestKeyBytes> key_{};
};

enum class FloorError : std::uint8_t { none, missing, already_exists, invalid_file, io_failure };

class FloorFile final {
public:
    [[nodiscard]] static FloorError create(const std::filesystem::path& file) noexcept;
    [[nodiscard]] static FloorError open(const std::filesystem::path& file, FloorFile& destination) noexcept;
    [[nodiscard]] std::uint64_t floor() const noexcept { return floor_; }
    [[nodiscard]] SequenceStore store() noexcept;

private:
    static bool persist(void* context, std::uint64_t floor) noexcept;
    std::filesystem::path file_;
    std::uint64_t floor_{};
};

class AuditFile final {
public:
    AuditFile() noexcept = default;
    ~AuditFile();
    AuditFile(const AuditFile&) = delete;
    AuditFile& operator=(const AuditFile&) = delete;
    [[nodiscard]] bool open(const std::filesystem::path& file) noexcept;
    [[nodiscard]] AuditSink sink() noexcept;
    [[nodiscard]] static std::string line(const AuditRecord& record);

private:
    static bool append(void* context, const AuditRecord& record) noexcept;
    int fd_{-1};
};

[[nodiscard]] SignerClock monotonic_clock() noexcept;

struct ServerCounters {
    std::uint64_t served{};
    std::uint64_t peer_refusals{};
    std::uint64_t malformed{};
    std::uint64_t io_failures{};
};

class SignerServer final {
public:
    SignerServer() noexcept = default;
    ~SignerServer();
    SignerServer(const SignerServer&) = delete;
    SignerServer& operator=(const SignerServer&) = delete;

    [[nodiscard]] bool listen(const std::filesystem::path& socket, std::uint32_t allowed_uid) noexcept;
    bool serve_once(SignerCore& core, int timeout_ms);
    [[nodiscard]] const ServerCounters& counters() const noexcept { return counters_; }

private:
    int fd_{-1};
    std::filesystem::path path_;
    std::uint32_t allowed_uid_{};
    ServerCounters counters_{};
};

class SocketSigner final {
public:
    SocketSigner(std::filesystem::path socket, int timeout_ms) noexcept;
    [[nodiscard]] Signer handle() noexcept { return {&SocketSigner::dispatch, this}; }
    [[nodiscard]] SignerResult sign(const SigningRequest& request);

private:
    static SignerResult dispatch(void* self, const SigningRequest& request) {
        return static_cast<SocketSigner*>(self)->sign(request);
    }
    std::filesystem::path socket_;
    int timeout_ms_;
};

}
