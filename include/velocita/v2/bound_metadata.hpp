#pragma once

#include "velocita/storage.hpp"
#include "velocita/v2/bound_frame.hpp"

#include <filesystem>
#include <stdexcept>

namespace velocita::v2 {

enum class BoundMetadataErrorKind : std::uint8_t {
    absent,
    io,
    frame,
    invalid_temp_lineage,
};

class BoundMetadataError final : public std::runtime_error {
public:
    BoundMetadataError(BoundMetadataErrorKind kind, BoundFrameError frame_error,
                       const std::string& message);
    [[nodiscard]] BoundMetadataErrorKind kind() const noexcept { return kind_; }
    [[nodiscard]] BoundFrameError frame_error() const noexcept { return frame_error_; }

private:
    BoundMetadataErrorKind kind_;
    BoundFrameError frame_error_;
};

[[nodiscard]] BoundFrameEncodeResult encode_bound_metadata(
    const PersistentMetadata& value, const VerifiedStaticIdentity& identity,
    std::span<std::byte> output, BoundFrameScratch& scratch) noexcept;

[[nodiscard]] BoundFrameError decode_bound_metadata(
    std::span<const std::byte> input, const VerifiedStaticIdentity& identity,
    PersistentMetadata& destination, BoundFrameScratch& scratch) noexcept;

// Read exactly one regular, non-symlink directory entry. The bounded reader
// consumes at most 113 bytes (the 112-byte frame plus one oversize lookahead).
// When requested, resync happens only after the complete object is validated.
[[nodiscard]] PersistentMetadata read_bound_metadata_at(
    int directory_fd, const char* name, const VerifiedStaticIdentity& identity,
    bool resync = false);

class BoundMetadataStore final {
public:
    explicit BoundMetadataStore(std::filesystem::path data_dir,
                                VerifiedStaticIdentity identity,
                                MetadataCheckpoints checkpoints = {});
    [[nodiscard]] PersistentMetadata load() const;
    void save(const PersistentMetadata& value) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::filesystem::path temporary_path() const;

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
    VerifiedStaticIdentity identity_;
    MetadataCheckpoints checkpoints_;
};

} // namespace velocita::v2
