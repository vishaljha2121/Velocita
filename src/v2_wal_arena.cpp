#include "velocita/v2/wal_arena.hpp"

#include <algorithm>
#include <cstring>

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

}  // namespace velocita::v2
