#include "velocita/v2/wal_arena.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace velocita::v2 {
namespace {

constexpr std::uint32_t kBlockMagic = 0x32425756U;
constexpr std::uint32_t kWriterIdleSpins = 256;
constexpr auto kWriterIdleSleep = std::chrono::microseconds(200);
constexpr std::uint16_t kBlockVersion = 2;

void visit_segment_records(const int fd, const VerifiedStaticIdentity& identity,
                           const std::function<void(const WalRecord&)>& visitor);

void sync_directory(const std::filesystem::path& path) {
    const int directory = ::open(path.c_str(), O_RDONLY);
    if (directory < 0) throw std::runtime_error("WAL directory open failed");
    const int result = ::fsync(directory);
    ::close(directory);
    if (result != 0) throw std::runtime_error("WAL directory sync failed");
}

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

}

std::vector<std::byte> frame_block(const std::uint64_t generation,
                                   const std::uint64_t sequence,
                                   const std::span<const std::byte> payload) {
    if (!generation || !sequence || payload.empty() || payload.size() > kWalBlockPayloadBytes)
        throw BoundWalError(BoundFrameError::invalid_payload, "invalid WAL block shape");
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

    const auto checksum = crc32(bytes);
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[crc_offset + i] =
            static_cast<std::byte>((checksum >> (i * 8U)) & 0xFFU);
    }
    bytes.resize(kWalBlockBytes, std::byte{0});
    return bytes;
}

std::optional<DecodedBlock> decode_block(const std::span<const std::byte> bytes) {
    if (bytes.size() < kWalBlockHeaderBytes) return std::nullopt;
    std::size_t offset = 0;
    if (get_le(bytes, offset, 4) != kBlockMagic) return std::nullopt;
    if (get_le(bytes, offset, 2) != kBlockVersion)
        throw BoundWalError(BoundFrameError::unsupported_schema, "unsupported WAL block version");
    if (bytes.size() != kWalBlockBytes) return std::nullopt;
    if (get_le(bytes, offset, 2) != 0) return std::nullopt;
    DecodedBlock block;
    block.generation = get_le(bytes, offset, 8);
    block.sequence = get_le(bytes, offset, 8);
    const auto byte_count = get_le(bytes, offset, 4);
    const auto stored = get_le(bytes, offset, 4);
    if (!block.generation || !block.sequence || !byte_count || byte_count > kWalBlockPayloadBytes)
        return std::nullopt;
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

std::optional<BlockDescriptor> WalBlockArena::next_unsubmitted() {
    if (submitted_ >= sealed_.size()) return std::nullopt;
    const auto slot = sealed_[submitted_++];
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
    if (block.generation != descriptor.generation) return false;
    if (block.state != BlockState::sealed) return false;

    const auto position = std::find(sealed_.begin(), sealed_.end(), descriptor.slot);
    if (position == sealed_.end()) return false;
    if (position != sealed_.begin()) return false;
    sealed_.erase(position);
    if (submitted_ > 0) --submitted_;

    block.state = BlockState::durable;
    if (block.highest_record_index > durable_index_.load(std::memory_order_relaxed)) {
        durable_index_.store(block.highest_record_index, std::memory_order_release);
    }
    return true;
}

void WalBlockArena::seed_durable_index(const std::uint64_t index) noexcept {
    if (index > durable_index_.load(std::memory_order_relaxed)) {
        durable_index_.store(index, std::memory_order_release);
    }
}

void WalBlockArena::rewind_durable_index(const std::uint64_t surviving_index) {
    if (open_slot_ || !sealed_.empty() || submitted_ != 0) {
        throw std::logic_error("cannot rewind durability with outstanding WAL work");
    }
    durable_index_.store(std::min(surviving_index, durable_index()),
                         std::memory_order_release);
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

WalSegment::WalSegment(std::filesystem::path data_dir, const VerifiedStaticIdentity& identity,
                       StorageCheckpoints checkpoints)
    : path_(std::move(data_dir) / "v2-wal-blocks.bin"), identity_(identity), checkpoints_(checkpoints) {
    visit_existing_records(path_.parent_path(), identity_, [](const WalRecord&) {});
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd_ < 0) throw std::runtime_error("cannot open " + path_.string());
    try { sync_directory(path_.parent_path()); }
    catch (...) { ::close(fd_); fd_ = -1; throw; }
    struct stat info {};
    if (::fstat(fd_, &info) != 0 || !S_ISREG(info.st_mode)) {
        ::close(fd_); fd_ = -1;
        throw std::runtime_error("cannot inspect writable WAL segment");
    }
    reserved_through_ = static_cast<std::uint64_t>(info.st_size);

    std::array<std::byte, kWalBlockBytes> block{};
    next_offset_ = 0;
    try {
        while (next_offset_ + kWalBlockBytes <= reserved_through_) {
            std::size_t done = 0;
            while (done < kWalBlockBytes) {
                const auto result = ::pread(fd_, block.data() + done, kWalBlockBytes - done,
                                            static_cast<off_t>(next_offset_ + done));
                if (result < 0 && errno == EINTR) continue;
                if (result < 0) throw std::runtime_error("cannot read WAL append frontier");
                if (result == 0) throw std::runtime_error("WAL changed during append frontier scan");
                done += static_cast<std::size_t>(result);
            }
            if (!decode_block(block)) break;
            next_offset_ += kWalBlockBytes;
        }
    } catch (...) { ::close(fd_); fd_ = -1; throw; }
}

WalSegment::~WalSegment() {
    if (fd_ >= 0) ::close(fd_);
}

void WalSegment::reserve_through(const std::uint64_t offset) {
    if (offset <= reserved_through_) return;
    const auto target = offset + kWalPrealloctionBlocks * kWalBlockBytes;
    if (::ftruncate(fd_, static_cast<off_t>(target)) != 0) {
        throw std::runtime_error("WAL preallocation failed");
    }
    reserved_through_ = target;
}

void WalSegment::write_block(const BlockDescriptor& descriptor,
                             const std::span<const std::byte> payload) {
    reserve_through(next_offset_ + kWalBlockBytes);
    const auto framed = frame_block(descriptor.generation, descriptor.sequence, payload);
    checkpoints_(StorageCheckpoint::before_block_write);
    std::size_t done = 0;
    while (done < framed.size()) {
        const auto result = ::pwrite(fd_, framed.data() + done, framed.size() - done,
                                     static_cast<off_t>(next_offset_ + done));
        if (result <= 0) throw std::runtime_error("WAL block write failed");
        done += static_cast<std::size_t>(result);
    }
    next_offset_ += kWalBlockBytes;
}

void WalSegment::sync() {
#if defined(__APPLE__)
    checkpoints_(StorageCheckpoint::before_file_sync);
    if (::fsync(fd_) != 0) throw std::runtime_error("WAL fsync failed");
#else
    checkpoints_(StorageCheckpoint::before_file_sync);
    if (::fdatasync(fd_) != 0) throw std::runtime_error("WAL fdatasync failed");
#endif
}

std::size_t WalSegment::flush(WalBlockArena& arena) {
    std::vector<BlockDescriptor> written;
    const auto original_offset = next_offset_;
    const auto original_reserved = reserved_through_;
    const auto pending = arena.sealed_count();
    if (arena.submitted_ != 0) {
        throw std::logic_error(
            "synchronous WAL flush cannot mix with submitted writer work");
    }
    written.reserve(pending);
    for (std::size_t i = 0; i < pending; ++i) {
        const auto slot = arena.sealed_[i];
        const auto& block = arena.blocks_[slot];
        written.push_back({slot, block.generation, block.sequence,
                           block.highest_record_index, block.used});
    }
    if (written.empty()) return 0;
    try {
        for (const auto& descriptor : written) {
            write_block(descriptor, arena.block_bytes(descriptor));
        }
        sync();
    } catch (...) {
        const int rollback = ::ftruncate(fd_, static_cast<off_t>(original_offset));
        static_cast<void>(rollback);
        next_offset_ = original_offset;
        reserved_through_ = original_reserved;
        throw;
    }
    for (const auto& descriptor : written) {
        if (!arena.complete(descriptor)) {
            throw std::runtime_error("WAL completion order violation");
        }
    }
    return written.size();
}

namespace {

class SegmentRewriteWriter final {
public:
    SegmentRewriteWriter(const int fd, const VerifiedStaticIdentity& identity,
                         const StorageCheckpoints checkpoints)
        : fd_(fd), identity_(identity), checkpoints_(checkpoints) {
        payload_.reserve(kWalBlockPayloadBytes);
    }

    void append(const WalRecord& record) {
        auto framed = encode_bound_wal_record(record, identity_);
        if (framed.size() > kWalBlockPayloadBytes) {
            throw std::runtime_error("a retained record no longer fits a block");
        }
        if (payload_.size() + framed.size() > kWalBlockPayloadBytes) emit();
        payload_.insert(payload_.end(), framed.begin(), framed.end());
    }

    [[nodiscard]] std::uint64_t finish() {
        emit();
        if (::ftruncate(fd_, static_cast<off_t>(offset_)) != 0) {
            throw std::runtime_error("WAL rewrite truncate failed");
        }
#if defined(__APPLE__)
        if (::fsync(fd_) != 0) throw std::runtime_error("WAL rewrite fsync failed");
#else
        if (::fdatasync(fd_) != 0) throw std::runtime_error("WAL rewrite fsync failed");
#endif
        checkpoints_(StorageCheckpoint::file_synced);
        return offset_;
    }

private:
    void emit() {
        if (payload_.empty()) return;
        const auto framed = frame_block(sequence_, sequence_, payload_);
        std::size_t done = 0;
        while (done < framed.size()) {
            const auto result = ::pwrite(fd_, framed.data() + done, framed.size() - done,
                                         static_cast<off_t>(offset_ + done));
            if (result <= 0) throw std::runtime_error("WAL rewrite failed");
            done += static_cast<std::size_t>(result);
        }
        offset_ += kWalBlockBytes;
        checkpoints_(StorageCheckpoint::rewrite_block);
        ++sequence_;
        payload_.clear();
    }

    int fd_;
    const VerifiedStaticIdentity& identity_;
    StorageCheckpoints checkpoints_;
    std::vector<std::byte> payload_;
    std::uint64_t sequence_{1};
    std::uint64_t offset_{};
};

}

void WalSegment::rewrite_filtered(const std::uint64_t boundary,
                                  const bool keep_before_boundary) {
    bool changed = false;
    visit_segment_records(fd_, identity_, [&](const WalRecord& record) {
        const bool keep = keep_before_boundary ? record.index < boundary
                                               : record.index > boundary;
        changed = changed || !keep;
    });
    if (!changed) return;

    const auto temporary = path_.string() + ".rewrite";
    int replacement = ::open(temporary.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    if (replacement < 0) throw std::runtime_error("WAL replacement open failed");
    try {
        struct stat info{};
        if (::fstat(replacement, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1)
            throw std::runtime_error("WAL replacement is not a singly-linked regular file");
        if (::ftruncate(replacement, 0) != 0) throw std::runtime_error("WAL replacement truncate failed");
        SegmentRewriteWriter writer(replacement, identity_, checkpoints_);
        visit_segment_records(fd_, identity_, [&](const WalRecord& record) {
            const bool keep = keep_before_boundary ? record.index < boundary
                                                   : record.index > boundary;
            if (keep) writer.append(record);
        });
        const auto end = writer.finish();
        if (::rename(temporary.c_str(), path_.c_str()) != 0)
            throw std::runtime_error("WAL replacement rename failed");
        ::close(fd_);
        fd_ = replacement;
        replacement = -1;
        next_offset_ = end;
        reserved_through_ = end;
        checkpoints_(StorageCheckpoint::file_renamed);
        sync_directory(path_.parent_path());
        checkpoints_(StorageCheckpoint::directory_synced);
    } catch (...) {
        if (replacement >= 0) ::close(replacement);
        throw;
    }
}

void WalSegment::truncate_from(const std::uint64_t first_removed_index) {
    rewrite_filtered(first_removed_index, true);
}

void WalSegment::discard_through(const std::uint64_t last_removed_index) {
    if (last_removed_index == 0) return;
    rewrite_filtered(last_removed_index, false);
}

namespace {

void visit_segment_records(const int fd, const VerifiedStaticIdentity& identity,
                           const std::function<void(const WalRecord&)>& visitor) {
    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1) {
        throw std::runtime_error("cannot inspect WAL segment");
    }
    const auto size = static_cast<std::uint64_t>(info.st_size);

    std::array<std::byte, kWalBlockBytes> block{};
    bool padding = false;
    for (std::uint64_t offset = 0; offset < size; offset += kWalBlockBytes) {
        const auto expected = static_cast<std::size_t>(std::min<std::uint64_t>(kWalBlockBytes, size - offset));
        std::size_t done = 0;
        while (done < expected) {
            const auto result = ::pread(fd, block.data() + done, expected - done,
                                        static_cast<off_t>(offset + done));
            if (result < 0 && errno == EINTR) continue;
            if (result <= 0) throw std::runtime_error("WAL changed during recovery scan");
            done += static_cast<std::size_t>(result);
        }
        const auto bytes = std::span(block).first(expected);
        const bool zero = std::all_of(bytes.begin(), bytes.end(), [](auto byte) { return byte == std::byte{0}; });
        if (zero) { padding = true; continue; }
        if (expected != kWalBlockBytes)
            throw BoundWalError(BoundFrameError::truncated, "nonzero partial WAL block");
        if (padding)
            throw BoundWalError(BoundFrameError::invalid_payload, "nonzero WAL block after padding");
        const auto decoded = decode_block(bytes);
        if (!decoded)
            throw BoundWalError(BoundFrameError::invalid_payload, "corrupt complete WAL block");
        const auto records = decode_bound_wal_records(decoded->payload, identity);
        for (const auto& record : records) visitor(record);
    }
}

}

std::vector<WalRecord> WalSegment::read_records() const {
    std::vector<WalRecord> records;
    visit_segment_records(fd_, identity_, [&](const WalRecord& record) {
        records.push_back(record);
    });
    return records;
}

std::vector<WalRecord> WalSegment::read_existing_records(
    const std::filesystem::path& data_dir, const VerifiedStaticIdentity& identity) {
    std::vector<WalRecord> records;
    visit_existing_records(data_dir, identity, [&](const WalRecord& record) {
        records.push_back(record);
    });
    return records;
}

void WalSegment::visit_existing_records(
    const std::filesystem::path& data_dir, const VerifiedStaticIdentity& identity,
    const std::function<void(const WalRecord&)>& visitor) {
    if (!identity.valid()) throw BoundWalError(BoundFrameError::invalid_identity, "WAL identity missing");
    struct stat legacy{};
    if (::lstat((data_dir / kV2WalFileName).c_str(), &legacy) == 0)
        throw BoundWalError(BoundFrameError::unsupported_schema, "legacy flat V2 WAL is incompatible");
    if (errno != ENOENT) throw std::runtime_error("cannot inspect legacy WAL name");
    const auto path = data_dir / "v2-wal-blocks.bin";
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT) return;
        throw std::runtime_error("cannot open existing WAL segment " + path.string());
    }
    try {
        visit_segment_records(fd, identity, visitor);
        ::close(fd);
    } catch (...) {
        ::close(fd);
        throw;
    }
}

WalWriter::WalWriter(WalSegment& segment, const WalBlockArena& arena,
                     const std::uint64_t generation,
                     const StorageCheckpoints checkpoints)
    : segment_(&segment), arena_(&arena), target_generation_(generation),
      checkpoints_(checkpoints) {}

WalWriter::~WalWriter() { stop(); }

bool WalWriter::submit(const BlockDescriptor& descriptor) noexcept {
    if (failed() || maintenance_state_.load(std::memory_order_acquire) != 0 ||
        switch_state_.load(std::memory_order_acquire) != 0) return false;
    return submissions_.push(descriptor);
}

bool WalWriter::take_completion(BlockDescriptor& descriptor) noexcept {
    return completions_.pop(descriptor);
}

bool WalWriter::submit_maintenance(void (*run)(void*), void* context) noexcept {
    if (!run || !context || failed() || !running_.load(std::memory_order_acquire) ||
        maintenance_state_.load(std::memory_order_acquire) != 0 ||
        switch_state_.load(std::memory_order_acquire) != 0) return false;
    maintenance_run_ = run;
    maintenance_context_ = context;
    maintenance_state_.store(1, std::memory_order_release);
    return true;
}

bool WalWriter::submit_target_switch(
    const std::uint64_t expected_generation,
    const std::uint64_t next_generation, const std::uint64_t sequence,
    WalSegment& next_segment, const WalBlockArena& next_arena) noexcept {
    if (!next_generation || next_generation <= expected_generation || !sequence ||
        sequence <= target_switch_sequence_.load(std::memory_order_acquire) ||
        failed() || !running_.load(std::memory_order_acquire) ||
        maintenance_state_.load(std::memory_order_acquire) != 0 ||
        switch_state_.load(std::memory_order_acquire) != 0)
        return false;
    switch_segment_ = &next_segment;
    switch_arena_ = &next_arena;
    switch_expected_generation_ = expected_generation;
    switch_generation_ = next_generation;
    switch_sequence_ = sequence;
    switch_state_.store(1, std::memory_order_release);
    return true;
}

bool WalWriter::take_target_switch_completion(
    TargetSwitchCompletion& completion) noexcept {
    if (switch_state_.load(std::memory_order_acquire) != 2) return false;
    completion = switch_completion_;
    switch_state_.store(0, std::memory_order_release);
    return true;
}

bool WalWriter::take_maintenance_completion() noexcept {
    if (maintenance_state_.load(std::memory_order_acquire) != 2) return false;
    maintenance_state_.store(0, std::memory_order_release);
    return true;
}

std::size_t WalWriter::poll() {
    rethrow_failure();
    std::vector<BlockDescriptor> done;
    BlockDescriptor descriptor{};
    while (submissions_.pop(descriptor)) {
        segment_->write_block(descriptor, arena_->block_bytes(descriptor));
        done.push_back(descriptor);
        if (done.size() >= completions_.capacity() / 2) break;
    }
    if (done.empty()) {
        if (maintenance_state_.load(std::memory_order_acquire) == 1) {
            maintenance_run_(maintenance_context_);
            maintenance_state_.store(2, std::memory_order_release);
            return 1;
        }
        checkpoints_(StorageCheckpoint::writer_idle_before_control_acquire);
        if (switch_state_.load(std::memory_order_acquire) == 1) {
            if (!submissions_.empty()) return 0;
            if (!completions_.empty()) return 0;
            const bool matches = target_generation_ == switch_expected_generation_;
            switch_completion_ = {target_generation_, switch_generation_,
                                  switch_sequence_, matches};
            if (matches) {
                segment_ = switch_segment_;
                arena_ = switch_arena_;
                target_generation_ = switch_generation_;
                target_switch_sequence_.store(switch_sequence_,
                                              std::memory_order_release);
            }
            switch_state_.store(2, std::memory_order_release);
            return 1;
        }
        return 0;
    }

    segment_->sync();

    for (const auto& finished : done) {
        while (!completions_.push(finished)) {
            std::this_thread::yield();
        }
    }
    blocks_written_.fetch_add(done.size(), std::memory_order_release);
    return done.size();
}

void WalWriter::start() {
    rethrow_failure();
    if (running_.exchange(true, std::memory_order_acq_rel)) return;
    thread_ = std::thread([this] {
        try {
            std::uint32_t idle_polls = 0;
            while (running_.load(std::memory_order_acquire)) {
                if (poll() != 0) {
                    idle_polls = 0;
                } else if (idle_polls < kWriterIdleSpins) {
                    ++idle_polls;
                    std::this_thread::yield();
                } else {
                    std::this_thread::sleep_for(kWriterIdleSleep);
                }
            }
            while (poll() > 0) {
            }
        } catch (...) {
            failure_ = std::current_exception();
            failed_.store(true, std::memory_order_release);
            running_.store(false, std::memory_order_release);
        }
    });
}

void WalWriter::stop() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
}

void WalWriter::rethrow_failure() const {
    if (!failed()) return;
    std::rethrow_exception(failure_);
}

}
