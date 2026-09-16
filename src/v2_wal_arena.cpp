#include "velocita/v2/wal_arena.hpp"

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace velocita::v2 {
namespace {

// "VWB2". Distinct from the record magic inside the payload, so a block header
// can never be mistaken for a record and vice versa.
constexpr std::uint32_t kBlockMagic = 0x32425756U;
constexpr std::uint16_t kBlockVersion = 1;

void put_le(std::vector<std::byte>& out, const std::uint64_t value,
            const std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        out.push_back(static_cast<std::byte>((value >> (i * 8U)) & 0xFFU));
    }
}

std::uint64_t get_le(std::span<const std::byte> bytes, std::size_t& offset,
                     const std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        value |= static_cast<std::uint64_t>(
                     static_cast<unsigned char>(bytes[offset + i]))
                 << (i * 8U);
    }
    offset += width;
    return value;
}

}  // namespace

std::vector<std::byte> frame_block(const std::uint64_t generation,
                                   const std::uint64_t sequence,
                                   const std::span<const std::byte> payload) {
    std::vector<std::byte> bytes;
    bytes.reserve(kWalBlockBytes);
    put_le(bytes, kBlockMagic, 4);
    put_le(bytes, kBlockVersion, 2);
    put_le(bytes, 0, 2);
    put_le(bytes, generation, 8);
    put_le(bytes, sequence, 8);
    put_le(bytes, payload.size(), 4);
    const auto crc_offset = bytes.size();
    put_le(bytes, 0, 4);
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    // Checksum covers the header WITH THE CHECKSUM FIELD ZEROED plus the used
    // payload only. Covering trailing padding would make a block's validity
    // depend on bytes nothing wrote.
    const auto checksum = crc32(bytes);
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[crc_offset + i] =
            static_cast<std::byte>((checksum >> (i * 8U)) & 0xFFU);
    }
    // Pad to the fixed block size so every block occupies the same file offset
    // stride and a torn tail is a partial BLOCK rather than a shifted stream.
    bytes.resize(kWalBlockBytes, std::byte{0});
    return bytes;
}

std::optional<DecodedBlock> decode_block(const std::span<const std::byte> bytes) {
    if (bytes.size() < kWalBlockHeaderBytes) return std::nullopt;
    std::size_t offset = 0;
    if (get_le(bytes, offset, 4) != kBlockMagic) return std::nullopt;
    if (get_le(bytes, offset, 2) != kBlockVersion) return std::nullopt;
    offset += 2;
    DecodedBlock block;
    block.generation = get_le(bytes, offset, 8);
    block.sequence = get_le(bytes, offset, 8);
    const auto byte_count = get_le(bytes, offset, 4);
    const auto stored = get_le(bytes, offset, 4);
    if (byte_count > kWalBlockPayloadBytes) return std::nullopt;
    if (bytes.size() < kWalBlockHeaderBytes + byte_count) return std::nullopt;

    std::vector<std::byte> covered(bytes.begin(),
                                   bytes.begin() +
                                       static_cast<std::ptrdiff_t>(
                                           kWalBlockHeaderBytes + byte_count));
    for (std::size_t i = 0; i < 4; ++i) {
        covered[kWalBlockHeaderBytes - 4 + i] = std::byte{0};
    }
    if (crc32(covered) != stored) return std::nullopt;

    block.payload.assign(bytes.begin() +
                             static_cast<std::ptrdiff_t>(kWalBlockHeaderBytes),
                         bytes.begin() +
                             static_cast<std::ptrdiff_t>(kWalBlockHeaderBytes +
                                                         byte_count));
    return block;
}

WalBlockArena::WalBlockArena() : blocks_(kWalArenaBlocks) {
    for (auto& block : blocks_) block.bytes.resize(kWalBlockPayloadBytes);
}

bool WalBlockArena::acquire_open_block() {
    for (std::size_t slot = 0; slot < blocks_.size(); ++slot) {
        if (blocks_[slot].state != BlockState::durable) continue;
        auto& block = blocks_[slot];
        // A NEW GENERATION EVERY TIME THE SLOT IS REUSED. This is what makes a
        // completion from a previous use recognisable; without it a slow writer
        // could release bytes that a different record now occupies.
        block.generation = next_generation_++;
        block.sequence = next_sequence_++;
        block.state = BlockState::open;
        block.used = 0;
        block.highest_record_index = 0;
        open_slot_ = slot;
        return true;
    }
    return false;
}

void WalBlockArena::finish_open_block() {
    if (!open_slot_.has_value()) return;
    auto& block = blocks_[*open_slot_];
    if (block.used == 0) {
        // An empty block is returned rather than sealed. Sealing it would spend
        // a sequence number and a write on nothing.
        block.state = BlockState::durable;
        open_slot_.reset();
        return;
    }
    block.state = BlockState::sealed;
    sealed_.push_back(*open_slot_);
    open_slot_.reset();
}

void WalBlockArena::seal_open_block() { finish_open_block(); }

StageOutcome WalBlockArena::stage(const std::span<const std::byte> record_bytes,
                                  const std::uint64_t record_index) {
    if (record_bytes.size() > kWalBlockPayloadBytes) {
        return StageOutcome::record_too_large;
    }
    if (!open_slot_.has_value() && !acquire_open_block()) {
        return StageOutcome::backpressure;
    }
    // NO RECORD STRADDLES A BLOCK. When the remainder will not hold it, the
    // block is sealed short and a fresh one acquired. The wasted tail is the
    // price of making every torn write a bad block rather than a plausible
    // short record.
    if (blocks_[*open_slot_].used + record_bytes.size() > kWalBlockPayloadBytes) {
        finish_open_block();
        if (!acquire_open_block()) return StageOutcome::backpressure;
    }

    auto& block = blocks_[*open_slot_];
    std::memcpy(block.bytes.data() + block.used, record_bytes.data(),
                record_bytes.size());
    block.used += record_bytes.size();
    block.highest_record_index = std::max(block.highest_record_index, record_index);
    return StageOutcome::staged;
}

std::optional<BlockDescriptor> WalBlockArena::next_sealed() {
    if (sealed_.empty()) return std::nullopt;
    const auto slot = sealed_.front();
    const auto& block = blocks_[slot];
    return BlockDescriptor{slot, block.generation, block.sequence,
                           block.highest_record_index, block.used};
}

std::span<const std::byte> WalBlockArena::block_bytes(
    const BlockDescriptor& descriptor) const {
    const auto& block = blocks_[descriptor.slot];
    return {block.bytes.data(), descriptor.byte_count};
}

bool WalBlockArena::complete(const BlockDescriptor& descriptor) {
    if (descriptor.slot >= blocks_.size()) return false;
    auto& block = blocks_[descriptor.slot];
    // THE STALE-COMPLETION GUARD. Same slot, different generation means this
    // completion belongs to a use that has already been released and recycled.
    // Honouring it would mark bytes durable that have never been written.
    if (block.generation != descriptor.generation) return false;
    if (block.state != BlockState::sealed) return false;

    const auto position = std::find(sealed_.begin(), sealed_.end(), descriptor.slot);
    if (position == sealed_.end()) return false;
    // In order. A later block becoming durable before an earlier one would let
    // the durable index cover a gap, which is exactly the claim it must never
    // make.
    if (position != sealed_.begin()) return false;
    sealed_.erase(position);

    block.state = BlockState::durable;
    if (block.highest_record_index > durable_index_.load(std::memory_order_relaxed)) {
        durable_index_.store(block.highest_record_index, std::memory_order_release);
    }
    return true;
}

std::size_t WalBlockArena::sealed_count() const noexcept { return sealed_.size(); }

std::size_t WalBlockArena::free_blocks() const noexcept {
    std::size_t free = 0;
    for (const auto& block : blocks_) {
        if (block.state == BlockState::durable) ++free;
    }
    return free;
}

BlockState WalBlockArena::state_of(const std::size_t slot) const noexcept {
    return blocks_[slot].state;
}

std::uint64_t WalBlockArena::generation_of(const std::size_t slot) const noexcept {
    return blocks_[slot].generation;
}

WalSegment::WalSegment(std::filesystem::path data_dir)
    : path_(std::move(data_dir) / "v2-wal-blocks.bin") {
    std::filesystem::create_directories(path_.parent_path());
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) throw std::runtime_error("cannot open " + path_.string());
    struct stat info {};
    if (::fstat(fd_, &info) == 0) {
        // Resume at a block boundary. A partial tail block is overwritten rather
        // than appended after, because a block that never completed cannot be
        // part of the durable prefix and leaving it in place would put a torn
        // block in the middle of the file.
        next_offset_ = (static_cast<std::uint64_t>(info.st_size) / kWalBlockBytes) *
                       kWalBlockBytes;
    }
}

WalSegment::~WalSegment() {
    if (fd_ >= 0) ::close(fd_);
}

std::size_t WalSegment::flush(WalBlockArena& arena) {
    std::vector<BlockDescriptor> written;
    while (const auto descriptor = arena.next_sealed()) {
        const auto framed = frame_block(descriptor->generation, descriptor->sequence,
                                        arena.block_bytes(*descriptor));
        std::size_t done = 0;
        while (done < framed.size()) {
            const auto result =
                ::pwrite(fd_, framed.data() + done, framed.size() - done,
                         static_cast<off_t>(next_offset_ + done));
            if (result <= 0) throw std::runtime_error("WAL block write failed");
            done += static_cast<std::size_t>(result);
        }
        next_offset_ += kWalBlockBytes;
        written.push_back(*descriptor);
        // Released only after the sync below. Marking a block durable here would
        // be claiming durability for bytes still in the page cache.
        if (!arena.complete(*descriptor)) break;
    }
    if (written.empty()) return 0;
    // `fdatasync` where it exists, `fsync` where it does not. macOS has no
    // `fdatasync`, and F_FULLFSYNC is the only call there that actually reaches
    // the platter - but the development host is not the deployment target, and
    // pretending otherwise in a durability path would be the wrong kind of
    // portability.
#if defined(__APPLE__)
    if (::fsync(fd_) != 0) throw std::runtime_error("WAL fsync failed");
#else
    if (::fdatasync(fd_) != 0) throw std::runtime_error("WAL fdatasync failed");
#endif
    return written.size();
}

namespace {

// Rewrite a whole segment from records, packing blocks the same way staging
// does so a rewritten segment is indistinguishable from a staged one.
void rewrite_segment(const int fd, std::uint64_t& next_offset,
                     const std::vector<WalRecord>& kept) {
    std::vector<std::byte> payload;
    std::uint64_t sequence = 1;
    std::uint64_t offset = 0;
    const auto emit = [&]() {
        if (payload.empty()) return;
        const auto framed = frame_block(sequence, sequence, payload);
        std::size_t done = 0;
        while (done < framed.size()) {
            const auto result = ::pwrite(fd, framed.data() + done, framed.size() - done,
                                         static_cast<off_t>(offset + done));
            if (result <= 0) throw std::runtime_error("WAL rewrite failed");
            done += static_cast<std::size_t>(result);
        }
        offset += kWalBlockBytes;
        ++sequence;
        payload.clear();
    };
    for (const auto& record : kept) {
        auto framed = encode_wal_record(record);
        if (framed.size() > kWalBlockPayloadBytes) {
            throw std::runtime_error("a retained record no longer fits a block");
        }
        if (payload.size() + framed.size() > kWalBlockPayloadBytes) emit();
        payload.insert(payload.end(), framed.begin(), framed.end());
    }
    emit();
#if defined(__APPLE__)
    if (::fsync(fd) != 0) throw std::runtime_error("WAL rewrite fsync failed");
#else
    if (::fdatasync(fd) != 0) throw std::runtime_error("WAL rewrite fsync failed");
#endif
    // Drop whatever followed, so a shorter log cannot leave older blocks
    // readable past its own end.
    if (::ftruncate(fd, static_cast<off_t>(offset)) != 0) {
        throw std::runtime_error("WAL rewrite truncate failed");
    }
    next_offset = offset;
}

}  // namespace

void WalSegment::truncate_from(const std::uint64_t first_removed_index) {
    auto records = read_records();
    std::vector<WalRecord> kept;
    for (auto& record : records) {
        if (record.index < first_removed_index) kept.push_back(std::move(record));
    }
    if (kept.size() == records.size()) return;
    rewrite_segment(fd_, next_offset_, kept);
}

void WalSegment::discard_through(const std::uint64_t last_removed_index) {
    if (last_removed_index == 0) return;
    auto records = read_records();
    std::vector<WalRecord> kept;
    for (auto& record : records) {
        if (record.index > last_removed_index) kept.push_back(std::move(record));
    }
    if (kept.size() == records.size()) return;
    rewrite_segment(fd_, next_offset_, kept);
}

std::vector<WalRecord> WalSegment::read_records() const {
    std::vector<WalRecord> records;
    struct stat info {};
    if (::fstat(fd_, &info) != 0) return records;
    const auto size = static_cast<std::uint64_t>(info.st_size);

    std::vector<std::byte> block(kWalBlockBytes);
    for (std::uint64_t offset = 0; offset + kWalBlockBytes <= size;
         offset += kWalBlockBytes) {
        std::size_t done = 0;
        while (done < kWalBlockBytes) {
            const auto result = ::pread(fd_, block.data() + done, kWalBlockBytes - done,
                                        static_cast<off_t>(offset + done));
            if (result <= 0) break;
            done += static_cast<std::size_t>(result);
        }
        if (done != kWalBlockBytes) break;
        const auto decoded = decode_block(block);
        // STOP at the first block that does not verify. Everything after it is
        // unreachable by definition; reading past would be inventing history.
        if (!decoded) break;
        for (auto& record : decode_wal_records(decoded->payload)) {
            records.push_back(std::move(record));
        }
    }
    return records;
}

}  // namespace velocita::v2
