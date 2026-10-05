#include "velocita/shared_metrics.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>

namespace velocita {
namespace {

std::runtime_error system_error(const std::string& operation) {
    return std::runtime_error(operation + ": " + std::strerror(errno));
}

MetricRole metric_role(const std::string_view role) noexcept {
    if (role == "follower") return MetricRole::follower;
    if (role == "candidate") return MetricRole::candidate;
    if (role == "leader") return MetricRole::leader;
    return MetricRole::none;
}

std::uint64_t bits(const std::int64_t value) noexcept {
    return static_cast<std::uint64_t>(value);
}

std::int64_t signed_bits(const std::uint64_t value) noexcept {
    return static_cast<std::int64_t>(value);
}

std::uint64_t load(const MetricCell& cell) noexcept {
    return cell.value.load(std::memory_order_relaxed);
}

}

SharedMetrics::SharedMetrics(const std::int32_t node_id, const std::filesystem::path& path) {
    if (path.empty()) return;
    std::filesystem::create_directories(path.parent_path());
    const auto suffix = std::to_string(::getpid()) + "." +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto temporary_path = path.string() + ".tmp." + suffix;
    file_descriptor_ = ::open(temporary_path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0644);
    if (file_descriptor_ < 0) throw system_error("creating shared metrics page failed");
    mapped_size_ = sizeof(SharedMetricsPage);
    try {
        if (::fchmod(file_descriptor_, 0644) < 0 ||
            ::ftruncate(file_descriptor_, static_cast<off_t>(mapped_size_)) < 0) {
            throw system_error("sizing shared metrics page failed");
        }
        void* mapping = ::mmap(nullptr, mapped_size_, PROT_READ | PROT_WRITE, MAP_SHARED,
                               file_descriptor_, 0);
        if (mapping == MAP_FAILED) throw system_error("mapping shared metrics page failed");
        page_ = ::new (mapping) SharedMetricsPage{};
        page_->header.magic = kMetricsMagic;
        page_->header.version = kMetricsVersion;
        page_->header.page_size = sizeof(SharedMetricsPage);
        page_->header.node_id = node_id;
        page_->header.process_start_time_seconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        page_->header.writer_pid = static_cast<std::uint64_t>(::getpid());
        if (::rename(temporary_path.c_str(), path.c_str()) < 0) {
            throw system_error("publishing shared metrics page failed");
        }
        page_->active.value.store(1, std::memory_order_release);
    } catch (...) {
        if (page_ != nullptr) {
            ::munmap(page_, mapped_size_);
            page_ = nullptr;
        }
        ::close(file_descriptor_);
        file_descriptor_ = -1;
        ::unlink(temporary_path.c_str());
        throw;
    }
}

SharedMetrics::~SharedMetrics() {
    if (page_ != nullptr) {
        page_->active.value.store(0, std::memory_order_release);
        ::munmap(page_, mapped_size_);
    }
    if (file_descriptor_ >= 0) ::close(file_descriptor_);
}

void SharedMetrics::increment(MetricCell& cell) noexcept {
    cell.value.fetch_add(1, std::memory_order_relaxed);
}

void SharedMetrics::add(MetricCell& cell, const std::uint64_t value) noexcept {
    cell.value.fetch_add(value, std::memory_order_relaxed);
}

void SharedMetrics::set_depth(MetricCell& depth, MetricCell& high_watermark,
                              const std::uint64_t value) noexcept {
    depth.value.store(value, std::memory_order_relaxed);
    auto observed = high_watermark.value.load(std::memory_order_relaxed);
    while (observed < value &&
           !high_watermark.value.compare_exchange_weak(
               observed, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

void SharedMetrics::record_tick(const std::uint64_t monotonic_nanoseconds) noexcept {
    if (page_ == nullptr) return;
    page_->last_tick_monotonic_nanoseconds.value.store(monotonic_nanoseconds,
                                                        std::memory_order_relaxed);
    increment(page_->tick_total);
}

void SharedMetrics::record_owner_loop(
    const std::uint64_t iterations,
    const std::uint64_t maximum_gap_nanoseconds,
    const std::uint64_t client_budget_exhausted,
    const std::uint64_t peer_budget_exhausted,
    const std::uint64_t client_skipped_tick_slack,
    const std::uint64_t client_skipped_response_backlog) noexcept {
    if (page_ == nullptr) return;
    page_->owner_loop_iterations_total.value.store(iterations, std::memory_order_relaxed);
    page_->owner_loop_max_gap_nanoseconds.value.store(
        maximum_gap_nanoseconds, std::memory_order_relaxed);
    page_->client_budget_exhausted_total.value.store(
        client_budget_exhausted, std::memory_order_relaxed);
    page_->peer_budget_exhausted_total.value.store(
        peer_budget_exhausted, std::memory_order_relaxed);
    page_->client_poll_skipped_tick_slack_total.value.store(
        client_skipped_tick_slack, std::memory_order_relaxed);
    page_->client_poll_skipped_response_backlog_total.value.store(
        client_skipped_response_backlog, std::memory_order_relaxed);
}

void SharedMetrics::record_status(const NodeStatus& status) noexcept {
    if (page_ == nullptr) return;
    const auto role = metric_role(status.role);
    if (have_status_) {
        if (role != last_role_) increment(page_->observed_role_changes_total);
        if (status.leader_id != last_leader_id_) increment(page_->observed_leader_changes_total);
        if (static_cast<std::uint64_t>(status.term) != last_term_) increment(page_->observed_term_changes_total);
    }
    last_role_ = role;
    last_leader_id_ = status.leader_id;
    last_term_ = static_cast<std::uint64_t>(status.term);
    have_status_ = true;

    page_->status_sequence.value.fetch_add(1, std::memory_order_relaxed);
    page_->role.value.store(static_cast<std::uint64_t>(role), std::memory_order_relaxed);
    page_->leader_id.value.store(bits(status.leader_id), std::memory_order_relaxed);
    page_->term.value.store(static_cast<std::uint64_t>(status.term), std::memory_order_relaxed);
    page_->current_index.value.store(static_cast<std::uint64_t>(status.current_index), std::memory_order_relaxed);
    page_->commit_index.value.store(static_cast<std::uint64_t>(status.commit_index), std::memory_order_relaxed);
    page_->applied_index.value.store(static_cast<std::uint64_t>(status.applied_index), std::memory_order_relaxed);
    page_->counter_value.value.store(bits(status.counter_value), std::memory_order_relaxed);
    page_->status_sequence.value.fetch_add(1, std::memory_order_release);
}

void SharedMetrics::record_request(const std::size_t bytes) noexcept {
    if (page_ == nullptr) return;
    increment(page_->request_fragments_total);
    add(page_->request_bytes_total, bytes);
}

void SharedMetrics::record_rejected_request() noexcept {
    if (page_ != nullptr) increment(page_->rejected_requests_total);
}

void SharedMetrics::record_peer_fragment(const std::size_t bytes) noexcept {
    if (page_ == nullptr) return;
    increment(page_->peer_fragments_total);
    add(page_->peer_bytes_total, bytes);
}

void SharedMetrics::record_rejected_peer_message() noexcept {
    if (page_ != nullptr) increment(page_->rejected_peer_messages_total);
}

void SharedMetrics::record_outbound_peer_message() noexcept {
    if (page_ != nullptr) increment(page_->outbound_peer_messages_total);
}

void SharedMetrics::record_dropped_peer_message() noexcept {
    if (page_ != nullptr) increment(page_->dropped_peer_messages_total);
}

void SharedMetrics::record_completed_response() noexcept {
    if (page_ != nullptr) increment(page_->completed_responses_total);
}

void SharedMetrics::set_peer_queue_depth(const std::uint64_t depth) noexcept {
    if (page_ != nullptr) set_depth(page_->peer_queue_depth, page_->peer_queue_high_watermark, depth);
}

void SharedMetrics::set_response_queue_depth(const std::uint64_t depth) noexcept {
    if (page_ != nullptr) set_depth(page_->response_queue_depth, page_->response_queue_high_watermark, depth);
}

void SharedMetrics::record_offer(const OfferChannel channel, const OfferOutcome outcome) noexcept {
    if (page_ == nullptr) return;
    MetricCell* cell = nullptr;
    if (channel == OfferChannel::peer) {
        switch (outcome) {
            case OfferOutcome::success: cell = &page_->peer_offer_success_total; break;
            case OfferOutcome::not_connected: cell = &page_->peer_offer_not_connected_total; break;
            case OfferOutcome::back_pressured: cell = &page_->peer_offer_back_pressured_total; break;
            case OfferOutcome::admin_action: cell = &page_->peer_offer_admin_action_total; break;
            case OfferOutcome::failed: cell = &page_->peer_offer_failed_total; break;
        }
    } else {
        switch (outcome) {
            case OfferOutcome::success: cell = &page_->response_offer_success_total; break;
            case OfferOutcome::not_connected: cell = &page_->response_offer_not_connected_total; break;
            case OfferOutcome::back_pressured: cell = &page_->response_offer_back_pressured_total; break;
            case OfferOutcome::admin_action: cell = &page_->response_offer_admin_action_total; break;
            case OfferOutcome::failed: cell = &page_->response_offer_failed_total; break;
        }
    }
    increment(*cell);
}

std::optional<MetricsSnapshot> read_shared_metrics(const std::filesystem::path& path) {
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return std::nullopt;
    struct stat status{};
    if (::fstat(descriptor, &status) < 0 ||
        status.st_size != static_cast<off_t>(sizeof(SharedMetricsPage))) {
        ::close(descriptor);
        return std::nullopt;
    }
    void* mapping = ::mmap(nullptr, sizeof(SharedMetricsPage), PROT_READ, MAP_SHARED, descriptor, 0);
    ::close(descriptor);
    if (mapping == MAP_FAILED) return std::nullopt;
    const auto* page = static_cast<const SharedMetricsPage*>(mapping);
    MetricsSnapshot result;
    if (page->header.magic != kMetricsMagic || page->header.version != kMetricsVersion ||
        page->header.page_size != sizeof(SharedMetricsPage)) {
        ::munmap(mapping, sizeof(SharedMetricsPage));
        return std::nullopt;
    }
    result.header = page->header;
    result.active = page->active.value.load(std::memory_order_acquire) != 0;
    result.last_tick_monotonic_nanoseconds = load(page->last_tick_monotonic_nanoseconds);
    result.tick_total = load(page->tick_total);
    result.owner_loop_iterations_total = load(page->owner_loop_iterations_total);
    result.owner_loop_max_gap_nanoseconds = load(page->owner_loop_max_gap_nanoseconds);
    result.client_budget_exhausted_total = load(page->client_budget_exhausted_total);
    result.peer_budget_exhausted_total = load(page->peer_budget_exhausted_total);
    result.client_poll_skipped_tick_slack_total =
        load(page->client_poll_skipped_tick_slack_total);
    result.client_poll_skipped_response_backlog_total =
        load(page->client_poll_skipped_response_backlog_total);
    bool status_read{};
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto before = page->status_sequence.value.load(std::memory_order_acquire);
        if ((before & 1U) != 0) continue;
        result.role = static_cast<MetricRole>(load(page->role));
        result.leader_id = signed_bits(load(page->leader_id));
        result.term = load(page->term);
        result.current_index = load(page->current_index);
        result.commit_index = load(page->commit_index);
        result.applied_index = load(page->applied_index);
        result.counter_value = signed_bits(load(page->counter_value));
        const auto after = page->status_sequence.value.load(std::memory_order_acquire);
        if (before == after) {
            status_read = true;
            break;
        }
    }
    if (!status_read) {
        ::munmap(mapping, sizeof(SharedMetricsPage));
        return std::nullopt;
    }
    result.observed_role_changes_total = load(page->observed_role_changes_total);
    result.observed_leader_changes_total = load(page->observed_leader_changes_total);
    result.observed_term_changes_total = load(page->observed_term_changes_total);
    result.request_fragments_total = load(page->request_fragments_total);
    result.request_bytes_total = load(page->request_bytes_total);
    result.rejected_requests_total = load(page->rejected_requests_total);
    result.peer_fragments_total = load(page->peer_fragments_total);
    result.peer_bytes_total = load(page->peer_bytes_total);
    result.rejected_peer_messages_total = load(page->rejected_peer_messages_total);
    result.outbound_peer_messages_total = load(page->outbound_peer_messages_total);
    result.dropped_peer_messages_total = load(page->dropped_peer_messages_total);
    result.completed_responses_total = load(page->completed_responses_total);
    result.peer_queue_depth = load(page->peer_queue_depth);
    result.peer_queue_high_watermark = load(page->peer_queue_high_watermark);
    result.response_queue_depth = load(page->response_queue_depth);
    result.response_queue_high_watermark = load(page->response_queue_high_watermark);
    result.peer_offer_success_total = load(page->peer_offer_success_total);
    result.peer_offer_not_connected_total = load(page->peer_offer_not_connected_total);
    result.peer_offer_back_pressured_total = load(page->peer_offer_back_pressured_total);
    result.peer_offer_admin_action_total = load(page->peer_offer_admin_action_total);
    result.peer_offer_failed_total = load(page->peer_offer_failed_total);
    result.response_offer_success_total = load(page->response_offer_success_total);
    result.response_offer_not_connected_total = load(page->response_offer_not_connected_total);
    result.response_offer_back_pressured_total = load(page->response_offer_back_pressured_total);
    result.response_offer_admin_action_total = load(page->response_offer_admin_action_total);
    result.response_offer_failed_total = load(page->response_offer_failed_total);
    ::munmap(mapping, sizeof(SharedMetricsPage));
    return result;
}

}
