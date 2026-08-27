#include "velocita/benchmark.hpp"
#include "velocita/client_session.hpp"
#include "velocita/wire.hpp"

#include "Publication.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    double warmup_seconds{5};
    double duration_seconds{30};
    std::uint64_t rate_per_second{100};
    std::size_t max_in_flight{4096};
    std::uint64_t timeout_ms{2000};
    std::filesystem::path output_dir{"benchmark-results"};
    std::filesystem::path prometheus_file;
    std::string aeron_dir{"/dev/shm/aeron-raft-benchmark"};
    std::vector<velocita::RequestTarget> request_targets;
    std::vector<std::int32_t> members;
    std::string response_channel{"aeron:udp?endpoint=0.0.0.0:40101"};
    velocita::CommandType command{velocita::CommandType::increment};
    std::int64_t amount{};
    std::string idle_strategy{"yield"};
    std::string run_mode{"baseline"};
};

struct Counters {
    std::uint64_t scheduled{};
    std::uint64_t steady_scheduled{};
    std::uint64_t completed_ok{};
    std::uint64_t steady_completed_ok{};
    std::uint64_t timed_out{};
    std::uint64_t steady_timed_out{};
    std::uint64_t capacity_rejected{};
    std::uint64_t steady_capacity_rejected{};
    std::uint64_t logical_commands_started{};
    std::uint64_t steady_logical_commands_started{};
    std::uint64_t transport_attempts{};
    std::uint64_t steady_transport_attempts{};
    std::uint64_t not_leader_responses{};
    std::uint64_t steady_not_leader_responses{};
    std::uint64_t leader_hint_protocol_errors{};
    std::uint64_t steady_leader_hint_protocol_errors{};
    std::uint64_t duplicate_or_late_responses{};
    std::uint64_t invalid_responses{};
    std::uint64_t internal_error_responses{};
    std::uint64_t offer_success{};
    std::uint64_t offer_not_connected{};
    std::uint64_t offer_back_pressured{};
    std::uint64_t offer_admin_action{};
    std::uint64_t offer_closed{};
    std::uint64_t offer_max_position{};
    std::uint64_t offer_failed{};
    std::uint64_t steady_offer_not_connected{};
    std::uint64_t steady_offer_back_pressured{};
    std::uint64_t steady_offer_admin_action{};
    std::uint64_t steady_offer_closed{};
    std::uint64_t steady_offer_max_position{};
    std::uint64_t steady_offer_failed{};
    std::uint64_t internal_timing_samples{};
};

struct FailureRecord {
    std::uint64_t request_id{};
    std::uint64_t intended_send_ns{};
    velocita::BenchmarkPhase phase{};
    std::string_view outcome;
};

std::uint64_t now_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count());
}

double parse_positive_double(const std::string& value, const std::string& name) {
    const auto parsed = std::stod(value);
    if (!std::isfinite(parsed) || parsed <= 0) throw std::runtime_error(name + " must be positive");
    return parsed;
}

std::uint64_t parse_positive_u64(const std::string& value, const std::string& name) {
    const auto parsed = std::stoull(value);
    if (parsed == 0) throw std::runtime_error(name + " must be positive");
    return parsed;
}

Options parse_options(int argc, char** argv) {
    Options options;
    options.request_targets = {
        {1, "aeron:udp?endpoint=127.0.0.1:40111"},
        {2, "aeron:udp?endpoint=127.0.0.1:40112"},
        {3, "aeron:udp?endpoint=127.0.0.1:40113"},
    };
    options.members = {1, 2, 3};
    bool custom_request_targets = false;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help") {
            std::cout
                << "raft-bench [--warmup-seconds N] [--duration-seconds N] [--rate N] "
                   "[--max-in-flight N] [--timeout-ms N] [--output-dir PATH] "
                   "[--prometheus-file PATH] "
                   "[--aeron-dir PATH] [--members ID,ID,...] "
                   "[--request-target ID=URI]... [--response-channel URI] "
                   "[--operation get|increment] [--amount N] [--idle-strategy busy|yield|sleep] "
                   "[--run-mode baseline|stall]\n";
            std::exit(0);
        }
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + key);
        const std::string value = argv[++i];
        if (key == "--warmup-seconds") options.warmup_seconds = parse_positive_double(value, key);
        else if (key == "--duration-seconds") options.duration_seconds = parse_positive_double(value, key);
        else if (key == "--rate") options.rate_per_second = parse_positive_u64(value, key);
        else if (key == "--max-in-flight") {
            options.max_in_flight = static_cast<std::size_t>(parse_positive_u64(value, key));
        }
        else if (key == "--timeout-ms") options.timeout_ms = parse_positive_u64(value, key);
        else if (key == "--output-dir") options.output_dir = value;
        else if (key == "--prometheus-file") options.prometheus_file = value;
        else if (key == "--aeron-dir") options.aeron_dir = value;
        else if (key == "--members") options.members = velocita::parse_member_ids(value);
        else if (key == "--request-target") {
            if (!custom_request_targets) {
                options.request_targets.clear();
                custom_request_targets = true;
            }
            options.request_targets.push_back(velocita::parse_request_target(value));
        }
        else if (key == "--response-channel") options.response_channel = value;
        else if (key == "--operation") {
            if (value == "get") options.command = velocita::CommandType::get;
            else if (value == "increment") options.command = velocita::CommandType::increment;
            else throw std::runtime_error("operation must be get or increment");
        }
        else if (key == "--amount") options.amount = std::stoll(value);
        else if (key == "--idle-strategy") {
            if (value != "busy" && value != "yield" && value != "sleep") {
                throw std::runtime_error("idle strategy must be busy, yield, or sleep");
            }
            options.idle_strategy = value;
        }
        else if (key == "--run-mode") {
            if (value != "baseline" && value != "stall") {
                throw std::runtime_error("run mode must be baseline or stall");
            }
            options.run_mode = value;
        }
        else throw std::runtime_error("unknown option: " + key);
    }
    velocita::validate_request_targets(options.request_targets, options.members);
    const long double planned =
        (options.warmup_seconds + options.duration_seconds) * options.rate_per_second;
    if (planned > 5'000'000.0L) {
        throw std::runtime_error("a single run is limited to five million scheduled samples");
    }
    return options;
}

void write_distribution(std::ostream& output, const velocita::DistributionSummary& value) {
    output << "{\"count\":" << value.count
           << ",\"min_ns\":" << value.minimum_ns
           << ",\"mean_ns\":" << std::fixed << std::setprecision(3)
           << static_cast<double>(value.mean_ns)
           << ",\"p50_ns\":" << value.p50_ns
           << ",\"p90_ns\":" << value.p90_ns
           << ",\"p99_ns\":" << value.p99_ns
           << ",\"p999_ns\":" << value.p999_ns
           << ",\"p9999_ns\":" << value.p9999_ns
           << ",\"max_ns\":" << value.maximum_ns << '}';
}

std::vector<std::uint64_t> field_values(
    const std::span<const velocita::LatencySample> samples,
    const std::function<std::uint64_t(const velocita::LatencySample&)>& field,
    const bool internal_only = false) {
    std::vector<std::uint64_t> result;
    result.reserve(samples.size());
    for (const auto& sample : samples) {
        if (internal_only && !sample.internal_timing_valid) continue;
        result.push_back(field(sample));
    }
    return result;
}

void write_samples(const std::filesystem::path& path,
                   const std::span<const velocita::LatencySample> samples) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("open samples file failed: " + path.string());
    output << "request_id,scheduled_to_offer_ns,offer_to_response_ns,scheduled_to_response_ns,"
              "ingress_to_admission_ns,local_wal_append_ns,quorum_append_ns,"
              "admission_to_commit_ns,commit_to_apply_ns,retries,internal_timing_valid\n";
    for (const auto& sample : samples) {
        output << sample.request_id << ',' << sample.scheduled_to_offer_ns << ','
               << sample.offer_to_response_ns << ',' << sample.scheduled_to_response_ns << ','
               << sample.ingress_to_admission_ns << ',' << sample.local_wal_append_ns << ','
               << sample.quorum_append_ns << ',' << sample.admission_to_commit_ns << ','
               << sample.commit_to_apply_ns << ',' << sample.retries << ','
               << (sample.internal_timing_valid ? 1 : 0) << '\n';
    }
}

void write_histogram(const std::filesystem::path& path,
                     const std::span<const velocita::LatencySample> samples) {
    const auto bounds = velocita::default_latency_bucket_bounds_ns();
    const auto values = field_values(samples, [](const auto& sample) {
        return sample.scheduled_to_response_ns;
    });
    const auto counts = velocita::cumulative_bucket_counts(values, bounds);
    std::ofstream output(path);
    if (!output) throw std::runtime_error("open histogram file failed: " + path.string());
    output << "le_ns,cumulative_count\n";
    for (std::size_t i = 0; i < bounds.size(); ++i) output << bounds[i] << ',' << counts[i] << '\n';
    output << "+Inf," << values.size() << '\n';
}

void write_failures(const std::filesystem::path& path,
                    const std::span<const FailureRecord> failures) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("open failures file failed: " + path.string());
    output << "request_id,intended_send_ns,phase,outcome\n";
    for (const auto& failure : failures) {
        output << failure.request_id << ',' << failure.intended_send_ns << ','
               << (failure.phase == velocita::BenchmarkPhase::steady ? "steady" : "warmup")
               << ',' << failure.outcome << '\n';
    }
}

velocita::DistributionSummary summarize_field(
    const std::span<const velocita::LatencySample> samples,
    const std::function<std::uint64_t(const velocita::LatencySample&)>& field,
    const bool internal_only = false) {
    return velocita::summarize_values(field_values(samples, field, internal_only));
}

void idle(const std::string& strategy) {
    if (strategy == "yield") std::this_thread::yield();
    else if (strategy == "sleep") std::this_thread::sleep_for(std::chrono::microseconds(50));
}

std::uint64_t initial_request_id() {
    auto value = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    return value == 0 ? 1 : value;
}

std::string_view operation_name(const velocita::CommandType command) {
    return command == velocita::CommandType::get ? "get" : "increment";
}

void prometheus_distribution(std::ostream& output, const std::string_view stage,
                             const velocita::DistributionSummary& summary) {
    const std::array quantiles{
        std::pair{"0.50", summary.p50_ns}, std::pair{"0.90", summary.p90_ns},
        std::pair{"0.99", summary.p99_ns}, std::pair{"0.999", summary.p999_ns},
        std::pair{"0.9999", summary.p9999_ns}, std::pair{"1.0", summary.maximum_ns},
    };
    for (const auto& [quantile, value] : quantiles) {
        output << "velocita_benchmark_latency_seconds{stage=\"" << stage
               << "\",quantile=\"" << quantile << "\"} "
               << std::setprecision(12) << static_cast<long double>(value) / 1e9L << '\n';
    }
}

void prometheus_histogram(std::ostream& output, const std::string_view stage,
                          const std::span<const std::uint64_t> values) {
    const auto bounds = velocita::default_latency_bucket_bounds_ns();
    const auto counts = velocita::cumulative_bucket_counts(values, bounds);
    long double sum_ns{};
    for (const auto value : values) sum_ns += value;
    for (std::size_t i = 0; i < bounds.size(); ++i) {
        output << "velocita_benchmark_latency_distribution_seconds_bucket{stage=\"" << stage
               << "\",le=\"" << std::setprecision(12)
               << static_cast<long double>(bounds[i]) / 1e9L << "\"} " << counts[i] << '\n';
    }
    output << "velocita_benchmark_latency_distribution_seconds_bucket{stage=\"" << stage
           << "\",le=\"+Inf\"} " << values.size() << '\n'
           << "velocita_benchmark_latency_distribution_seconds_sum{stage=\"" << stage
           << "\"} " << std::setprecision(12) << sum_ns / 1e9L << '\n'
           << "velocita_benchmark_latency_distribution_seconds_count{stage=\"" << stage
           << "\"} " << values.size() << '\n';
}

void publish_prometheus(const std::filesystem::path& run_file,
                        const std::filesystem::path& latest_file,
                        const Options& options, const std::size_t effective_max_in_flight,
                        const Counters& counters,
                        const std::span<const velocita::LatencySample> samples,
                        const std::array<std::pair<std::string_view,
                            velocita::DistributionSummary>, 8>& summaries) {
    std::ofstream output(run_file);
    if (!output) throw std::runtime_error("open Prometheus output failed: " + run_file.string());
    output << "# HELP velocita_benchmark_last_run_timestamp_seconds Unix time of the latest completed benchmark.\n"
           << "# TYPE velocita_benchmark_last_run_timestamp_seconds gauge\n"
           << "velocita_benchmark_last_run_timestamp_seconds "
           << std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count() << '\n'
           << "# HELP velocita_benchmark_operation_info Bounded operation used by the latest run.\n"
           << "# TYPE velocita_benchmark_operation_info gauge\n"
           << "velocita_benchmark_operation_info{operation=\"" << operation_name(options.command)
           << "\"} 1\n"
           << "# HELP velocita_benchmark_run_mode_info Bounded mode of the latest run.\n"
           << "# TYPE velocita_benchmark_run_mode_info gauge\n"
           << "velocita_benchmark_run_mode_info{mode=\"" << options.run_mode << "\"} 1\n"
           << "# HELP velocita_benchmark_instrumentation Whether benchmark-only node timing was compiled in.\n"
           << "# TYPE velocita_benchmark_instrumentation gauge\n"
           << "velocita_benchmark_instrumentation ";
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    output << "1\n";
#else
    output << "0\n";
#endif
    output << "# HELP velocita_benchmark_config Latest bounded benchmark configuration.\n"
           << "# TYPE velocita_benchmark_config gauge\n"
           << "velocita_benchmark_config{parameter=\"rate_per_second\"} " << options.rate_per_second << '\n'
           << "velocita_benchmark_config{parameter=\"duration_seconds\"} " << options.duration_seconds << '\n'
           << "velocita_benchmark_config{parameter=\"max_in_flight\"} " << effective_max_in_flight << '\n'
           << "# HELP velocita_benchmark_outcome Latest measured-phase request and transport outcomes.\n"
           << "# TYPE velocita_benchmark_outcome gauge\n";
    const std::array outcomes{
        std::pair{"scheduled", counters.steady_scheduled},
        std::pair{"completed", counters.steady_completed_ok},
        std::pair{"timeout", counters.steady_timed_out},
        std::pair{"capacity_rejected", counters.steady_capacity_rejected},
        std::pair{"not_leader", counters.steady_not_leader_responses},
        std::pair{"leader_hint_protocol_error", counters.steady_leader_hint_protocol_errors},
        std::pair{"back_pressured", counters.steady_offer_back_pressured},
        std::pair{"not_connected", counters.steady_offer_not_connected},
        std::pair{"invalid_response", counters.invalid_responses},
        std::pair{"internal_error", counters.internal_error_responses},
    };
    for (const auto& [outcome, value] : outcomes) {
        output << "velocita_benchmark_outcome{outcome=\"" << outcome << "\"} " << value << '\n';
    }
    output << "# HELP velocita_benchmark_throughput_per_second Latest offered and completed measured throughput.\n"
           << "# TYPE velocita_benchmark_throughput_per_second gauge\n"
           << "velocita_benchmark_throughput_per_second{kind=\"offered\"} "
           << static_cast<double>(counters.steady_scheduled) / options.duration_seconds << '\n'
           << "velocita_benchmark_throughput_per_second{kind=\"completed\"} "
           << static_cast<double>(counters.steady_completed_ok) / options.duration_seconds << '\n'
           << "# HELP velocita_benchmark_retry_routing Latest measured logical-command and transport-attempt counts.\n"
           << "# TYPE velocita_benchmark_retry_routing gauge\n"
           << "velocita_benchmark_retry_routing{kind=\"logical_commands_started\"} "
           << counters.steady_logical_commands_started << '\n'
           << "velocita_benchmark_retry_routing{kind=\"transport_attempts\"} "
           << counters.steady_transport_attempts << '\n'
           << "# HELP velocita_benchmark_retry_amplification Transport attempts per admitted logical command.\n"
           << "# TYPE velocita_benchmark_retry_amplification gauge\n"
           << "velocita_benchmark_retry_amplification "
           << (counters.steady_logical_commands_started == 0 ? 0.0 :
               static_cast<double>(counters.steady_transport_attempts) /
               static_cast<double>(counters.steady_logical_commands_started)) << '\n'
           << "# HELP velocita_benchmark_latency_seconds Latest latency quantiles by bounded stage.\n"
           << "# TYPE velocita_benchmark_latency_seconds gauge\n";
    for (const auto& [stage, summary] : summaries) prometheus_distribution(output, stage, summary);
    output << "# HELP velocita_benchmark_latency_distribution_seconds Latest-run cumulative fixed-bucket latency distribution by bounded stage.\n"
           << "# TYPE velocita_benchmark_latency_distribution_seconds histogram\n";
    const auto total_values = field_values(samples, [](const auto& sample) {
        return sample.scheduled_to_response_ns;
    });
    const auto response_values = field_values(samples, [](const auto& sample) {
        return sample.offer_to_response_ns;
    });
    prometheus_histogram(output, "scheduled_to_response", total_values);
    prometheus_histogram(output, "offer_to_response", response_values);
    output.close();
    if (!latest_file.empty()) {
        if (!latest_file.parent_path().empty()) {
            std::filesystem::create_directories(latest_file.parent_path());
        }
        auto temporary = latest_file;
        temporary += ".tmp";
        std::filesystem::copy_file(run_file, temporary,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::rename(temporary, latest_file);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        std::filesystem::create_directories(options.output_dir);
        const auto planned_samples = static_cast<std::size_t>(std::ceil(
            (options.warmup_seconds + options.duration_seconds) *
            static_cast<double>(options.rate_per_second))) + 1;
        if (std::filesystem::exists(options.output_dir / "summary.json")) {
            throw std::runtime_error("output directory already contains summary.json: " +
                                     options.output_dir.string());
        }
        velocita::ExactLatencyRecorder warmup(planned_samples);
        velocita::ExactLatencyRecorder steady(planned_samples);
        velocita::PendingRequestTable pending(options.max_in_flight);
        std::vector<std::uint64_t> deadline_order;
        deadline_order.reserve(planned_samples);
        std::vector<FailureRecord> failures;
        failures.reserve(planned_samples);
        std::size_t deadline_cursor = 0;
        Counters counters;
        velocita::LeaderRouter router(options.request_targets, options.members);
        bool have_unsent_requests = false;
        std::function<void(velocita::PendingRequest&)> retry_request;

        velocita::ClientSessionOptions session_options;
        session_options.aeron_dir = options.aeron_dir;
        session_options.request_targets = options.request_targets;
        session_options.required_members = options.members;
        session_options.response_channel = options.response_channel;

        velocita::AeronClientSession session(
            std::move(session_options), [&](const velocita::Response& response) {
                auto* request = pending.find(response.request_id);
                if (request == nullptr) {
                    ++counters.duplicate_or_late_responses;
                    return;
                }
                if (response.status == velocita::ResponseStatus::not_leader) {
                    ++counters.not_leader_responses;
                    if (request->phase == velocita::BenchmarkPhase::steady) {
                        ++counters.steady_not_leader_responses;
                    }
                    const auto hint = router.observe(response);
                    if (hint == velocita::LeaderHintResult::invalid) {
                        ++counters.leader_hint_protocol_errors;
                        if (request->phase == velocita::BenchmarkPhase::steady) {
                            ++counters.steady_leader_hint_protocol_errors;
                        }
                    }
                    request->channel_index = router.leader_id()
                        ? router.preferred_target()
                        : router.next_discovery_target(request->channel_index);
                    retry_request(*request);
                    return;
                }
                if (response.status != velocita::ResponseStatus::ok) {
                    const auto outcome = response.status == velocita::ResponseStatus::invalid_request
                        ? std::string_view{"invalid_request"} : std::string_view{"internal_error"};
                    if (response.status == velocita::ResponseStatus::invalid_request) {
                        ++counters.invalid_responses;
                    } else {
                        ++counters.internal_error_responses;
                    }
                    failures.push_back({request->request_id, request->intended_send_ns,
                                        request->phase, outcome});
                    (void)pending.erase(response.request_id);
                    return;
                }
                if (router.observe(response) == velocita::LeaderHintResult::invalid) {
                    ++counters.leader_hint_protocol_errors;
                    if (request->phase == velocita::BenchmarkPhase::steady) {
                        ++counters.steady_leader_hint_protocol_errors;
                    }
                    failures.push_back({request->request_id, request->intended_send_ns,
                                        request->phase, "invalid_leader_hint"});
                    (void)pending.erase(response.request_id);
                    return;
                }
                const auto completed_ns = now_ns();
                velocita::LatencySample sample;
                sample.request_id = request->request_id;
                sample.scheduled_to_offer_ns = request->actual_send_ns >= request->intended_send_ns
                    ? request->actual_send_ns - request->intended_send_ns : 0;
                sample.offer_to_response_ns = completed_ns >= request->actual_send_ns
                    ? completed_ns - request->actual_send_ns : 0;
                sample.scheduled_to_response_ns = completed_ns >= request->intended_send_ns
                    ? completed_ns - request->intended_send_ns : 0;
                sample.retries = request->retry_count;
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
                sample.internal_timing_valid =
                    (response.benchmark.flags & velocita::kBenchmarkTimingValid) != 0;
                if (sample.internal_timing_valid) {
                    sample.ingress_to_admission_ns = response.benchmark.ingress_to_admission_ns;
                    sample.local_wal_append_ns = response.benchmark.local_wal_append_ns;
                    sample.quorum_append_ns = response.benchmark.quorum_append_ns;
                    sample.admission_to_commit_ns = response.benchmark.admission_to_commit_ns;
                    sample.commit_to_apply_ns = response.benchmark.commit_to_apply_ns;
                    ++counters.internal_timing_samples;
                }
#endif
                if (request->phase == velocita::BenchmarkPhase::warmup) (void)warmup.record(sample);
                else {
                    (void)steady.record(sample);
                    ++counters.steady_completed_ok;
                }
                ++counters.completed_ok;
                (void)pending.erase(response.request_id);
            });

        auto count_offer = [&](const std::int64_t result, const velocita::BenchmarkPhase phase) {
            if (result >= 0) ++counters.offer_success;
            else if (result == aeron::NOT_CONNECTED) ++counters.offer_not_connected;
            else if (result == aeron::BACK_PRESSURED) ++counters.offer_back_pressured;
            else if (result == aeron::ADMIN_ACTION) ++counters.offer_admin_action;
            else if (result == aeron::PUBLICATION_CLOSED) ++counters.offer_closed;
            else if (result == aeron::MAX_POSITION_EXCEEDED) ++counters.offer_max_position;
            else ++counters.offer_failed;
            if (phase != velocita::BenchmarkPhase::steady || result >= 0) return;
            if (result == aeron::NOT_CONNECTED) ++counters.steady_offer_not_connected;
            else if (result == aeron::BACK_PRESSURED) ++counters.steady_offer_back_pressured;
            else if (result == aeron::ADMIN_ACTION) ++counters.steady_offer_admin_action;
            else if (result == aeron::PUBLICATION_CLOSED) ++counters.steady_offer_closed;
            else if (result == aeron::MAX_POSITION_EXCEEDED) ++counters.steady_offer_max_position;
            else ++counters.steady_offer_failed;
        };

        retry_request = [&](velocita::PendingRequest& request) {
            const velocita::Command command{options.command, request.request_id, options.amount};
            const auto offer_started_ns = now_ns();
            if (request.transport_attempts != 0) ++request.retry_count;
            ++request.transport_attempts;
            ++counters.transport_attempts;
            if (request.phase == velocita::BenchmarkPhase::steady) {
                ++counters.steady_transport_attempts;
            }
            const auto result = session.offer(request.channel_index, command);
            count_offer(result, request.phase);
            if (result >= 0) {
                if (request.actual_send_ns == 0) request.actual_send_ns = offer_started_ns;
                request.needs_offer_retry = false;
            } else if (result == aeron::NOT_CONNECTED || result == aeron::PUBLICATION_CLOSED) {
                router.mark_target_unavailable(request.channel_index);
                request.channel_index = router.preferred_target();
                request.needs_offer_retry = true;
                have_unsent_requests = true;
            } else {
                request.needs_offer_retry = true;
                have_unsent_requests = true;
            }
        };

        const auto connection_deadline = Clock::now() + std::chrono::seconds(10);
        bool any_connected = false;
        while (Clock::now() < connection_deadline) {
            for (std::size_t channel = 0; channel < session.channel_count(); ++channel) {
                any_connected = any_connected || session.is_connected(channel);
            }
            if (any_connected) break;
            session.poll(10);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!any_connected) throw std::runtime_error("no Raft request publication connected in ten seconds");

        std::vector<std::uint64_t> clock_costs;
        clock_costs.reserve(10'000);
        for (int sample = 0; sample < 10'000; ++sample) {
            const auto begin = now_ns();
            const auto end = now_ns();
            clock_costs.push_back(end - begin);
        }
        const auto clock_summary = velocita::summarize_values(clock_costs);

        const auto run_start_ns = now_ns() + 100'000'000ULL;
        const auto warmup_duration_ns = static_cast<std::uint64_t>(options.warmup_seconds * 1e9);
        const auto measured_duration_ns = static_cast<std::uint64_t>(options.duration_seconds * 1e9);
        const auto warmup_end_ns = run_start_ns + warmup_duration_ns;
        const auto run_end_ns = warmup_end_ns + measured_duration_ns;
        const auto timeout_ns = options.timeout_ms * 1'000'000ULL;
        auto request_id = initial_request_id();
        std::uint64_t sequence = 0;

        std::cout << "benchmark starting rate=" << options.rate_per_second
                  << "/s warmup=" << options.warmup_seconds
                  << "s measured=" << options.duration_seconds
                  << "s max_in_flight=" << pending.capacity() << '\n';

        while (now_ns() < run_start_ns) {
            session.poll(100);
            idle(options.idle_strategy);
        }

        while (now_ns() < run_end_ns || !pending.empty()) {
            const int fragments = session.poll(100);
            auto current_ns = now_ns();

            while (deadline_cursor < deadline_order.size()) {
                const auto expiring_id = deadline_order[deadline_cursor];
                auto* request = pending.find(expiring_id);
                if (request == nullptr) {
                    ++deadline_cursor;
                    continue;
                }
                if (request->deadline_ns > current_ns) break;
                ++counters.timed_out;
                if (request->phase == velocita::BenchmarkPhase::steady) ++counters.steady_timed_out;
                failures.push_back({request->request_id, request->intended_send_ns,
                                    request->phase, "timeout"});
                (void)pending.erase(expiring_id);
                ++deadline_cursor;
            }

            if (current_ns < run_end_ns) {
                while (true) {
                    const auto intended_ns = run_start_ns +
                        (sequence * 1'000'000'000ULL) / options.rate_per_second;
                    if (intended_ns > current_ns || intended_ns >= run_end_ns) break;
                    ++sequence;
                    ++counters.scheduled;
                    const bool steady_phase = intended_ns >= warmup_end_ns;
                    if (steady_phase) ++counters.steady_scheduled;
                    ++request_id;
                    if (request_id == 0) ++request_id;
                    if (pending.size() == pending.capacity()) {
                        ++counters.capacity_rejected;
                        if (steady_phase) ++counters.steady_capacity_rejected;
                        failures.push_back({request_id, intended_ns,
                                            steady_phase ? velocita::BenchmarkPhase::steady
                                                         : velocita::BenchmarkPhase::warmup,
                                            "capacity_rejected"});
                        continue;
                    }
                    velocita::PendingRequest request;
                    request.request_id = request_id;
                    request.intended_send_ns = intended_ns;
                    request.deadline_ns = intended_ns + timeout_ns;
                    request.channel_index = router.preferred_target();
                    request.phase = intended_ns < warmup_end_ns
                        ? velocita::BenchmarkPhase::warmup : velocita::BenchmarkPhase::steady;
                    if (!pending.insert(request)) {
                        ++counters.capacity_rejected;
                        if (steady_phase) ++counters.steady_capacity_rejected;
                        continue;
                    }
                    deadline_order.push_back(request_id);
                    ++counters.logical_commands_started;
                    if (steady_phase) ++counters.steady_logical_commands_started;
                    auto* inserted = pending.find(request_id);
                    retry_request(*inserted);
                }
            }

            if (have_unsent_requests) {
                have_unsent_requests = false;
                for (auto& request : pending.slots()) {
                    if (request.request_id != 0 && request.needs_offer_retry &&
                        request.deadline_ns > current_ns) {
                        retry_request(request);
                    }
                }
            }

            if (current_ns >= run_end_ns && pending.empty()) break;
            if (fragments == 0) idle(options.idle_strategy);
        }

        write_samples(options.output_dir / "warmup-samples.csv", warmup.samples());
        write_samples(options.output_dir / "steady-samples.csv", steady.samples());
        write_failures(options.output_dir / "failures.csv", failures);
        write_histogram(options.output_dir / "warmup-histogram.csv", warmup.samples());
        write_histogram(options.output_dir / "steady-histogram.csv", steady.samples());

        const auto total = steady.summarize_total_latency();
        const auto scheduling = summarize_field(steady.samples(), [](const auto& sample) {
            return sample.scheduled_to_offer_ns;
        });
        const auto response = summarize_field(steady.samples(), [](const auto& sample) {
            return sample.offer_to_response_ns;
        });
        const auto ingress = summarize_field(steady.samples(), [](const auto& sample) {
            return sample.ingress_to_admission_ns;
        }, true);
        const auto wal = summarize_field(steady.samples(), [](const auto& sample) {
            return sample.local_wal_append_ns;
        }, true);
        const auto quorum = summarize_field(steady.samples(), [](const auto& sample) {
            return sample.quorum_append_ns;
        }, true);
        const auto commit = summarize_field(steady.samples(), [](const auto& sample) {
            return sample.admission_to_commit_ns;
        }, true);
        const auto apply = summarize_field(steady.samples(), [](const auto& sample) {
            return sample.commit_to_apply_ns;
        }, true);

        std::ofstream summary(options.output_dir / "summary.json");
        if (!summary) throw std::runtime_error("open summary file failed");
        summary << "{\n  \"schema_version\":2,\n"
                << "  \"benchmark_instrumentation\":"
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
                << "true,\n"
#else
                << "false,\n"
#endif
                << "  \"config\":{\"warmup_seconds\":" << options.warmup_seconds
                << ",\"duration_seconds\":" << options.duration_seconds
                << ",\"rate_per_second\":" << options.rate_per_second
                << ",\"max_in_flight\":" << pending.capacity()
                << ",\"timeout_ms\":" << options.timeout_ms
                << ",\"operation\":\"" << operation_name(options.command) << "\""
                << ",\"amount\":" << options.amount
                << ",\"idle_strategy\":\"" << options.idle_strategy << "\""
                << ",\"run_mode\":\"" << options.run_mode << "\"},\n"
                << "  \"counters\":{\"scheduled\":" << counters.scheduled
                << ",\"steady_scheduled\":" << counters.steady_scheduled
                << ",\"completed_ok\":" << counters.completed_ok
                << ",\"steady_completed_ok\":" << counters.steady_completed_ok
                << ",\"timed_out\":" << counters.timed_out
                << ",\"steady_timed_out\":" << counters.steady_timed_out
                << ",\"capacity_rejected\":" << counters.capacity_rejected
                << ",\"steady_capacity_rejected\":" << counters.steady_capacity_rejected
                << ",\"logical_commands_started\":" << counters.logical_commands_started
                << ",\"steady_logical_commands_started\":" << counters.steady_logical_commands_started
                << ",\"transport_attempts\":" << counters.transport_attempts
                << ",\"steady_transport_attempts\":" << counters.steady_transport_attempts
                << ",\"not_leader_responses\":" << counters.not_leader_responses
                << ",\"steady_not_leader_responses\":" << counters.steady_not_leader_responses
                << ",\"leader_hint_protocol_errors\":" << counters.leader_hint_protocol_errors
                << ",\"steady_leader_hint_protocol_errors\":"
                << counters.steady_leader_hint_protocol_errors
                << ",\"duplicate_or_late_responses\":" << counters.duplicate_or_late_responses
                << ",\"invalid_responses\":" << counters.invalid_responses
                << ",\"internal_error_responses\":" << counters.internal_error_responses
                << ",\"offer_success\":" << counters.offer_success
                << ",\"offer_not_connected\":" << counters.offer_not_connected
                << ",\"offer_back_pressured\":" << counters.offer_back_pressured
                << ",\"offer_admin_action\":" << counters.offer_admin_action
                << ",\"offer_closed\":" << counters.offer_closed
                << ",\"offer_max_position\":" << counters.offer_max_position
                << ",\"offer_failed\":" << counters.offer_failed
                << ",\"steady_offer_not_connected\":" << counters.steady_offer_not_connected
                << ",\"steady_offer_back_pressured\":" << counters.steady_offer_back_pressured
                << ",\"steady_offer_admin_action\":" << counters.steady_offer_admin_action
                << ",\"steady_offer_closed\":" << counters.steady_offer_closed
                << ",\"steady_offer_max_position\":" << counters.steady_offer_max_position
                << ",\"steady_offer_failed\":" << counters.steady_offer_failed
                << ",\"internal_timing_samples\":" << counters.internal_timing_samples
                << ",\"warmup_recorder_dropped\":" << warmup.dropped_samples()
                << ",\"steady_recorder_dropped\":" << steady.dropped_samples() << "},\n"
                << "  \"rates\":{\"offered_per_second\":"
                << static_cast<double>(counters.steady_scheduled) / options.duration_seconds
                << ",\"completed_per_second\":"
                << static_cast<double>(counters.steady_completed_ok) / options.duration_seconds
                << ",\"retry_amplification\":"
                << (counters.steady_logical_commands_started == 0 ? 0.0 :
                    static_cast<double>(counters.steady_transport_attempts) /
                    static_cast<double>(counters.steady_logical_commands_started))
                << "},\n"
                << "  \"clock_call_pair\":";
        write_distribution(summary, clock_summary);
        summary << ",\n  \"scheduled_to_offer\":";
        write_distribution(summary, scheduling);
        summary << ",\n  \"offer_to_response\":";
        write_distribution(summary, response);
        summary << ",\n  \"scheduled_to_response\":";
        write_distribution(summary, total);
        summary << ",\n  \"ingress_to_admission\":";
        write_distribution(summary, ingress);
        summary << ",\n  \"local_wal_append\":";
        write_distribution(summary, wal);
        summary << ",\n  \"quorum_append\":";
        write_distribution(summary, quorum);
        summary << ",\n  \"admission_to_commit\":";
        write_distribution(summary, commit);
        summary << ",\n  \"commit_to_apply\":";
        write_distribution(summary, apply);
        summary << "\n}\n";
        summary.close();

        const std::array prometheus_summaries{
            std::pair<std::string_view, velocita::DistributionSummary>{"scheduled_to_response", total},
            std::pair<std::string_view, velocita::DistributionSummary>{"scheduled_to_offer", scheduling},
            std::pair<std::string_view, velocita::DistributionSummary>{"offer_to_response", response},
            std::pair<std::string_view, velocita::DistributionSummary>{"ingress_to_admission", ingress},
            std::pair<std::string_view, velocita::DistributionSummary>{"local_wal_append", wal},
            std::pair<std::string_view, velocita::DistributionSummary>{"quorum_append", quorum},
            std::pair<std::string_view, velocita::DistributionSummary>{"admission_to_commit", commit},
            std::pair<std::string_view, velocita::DistributionSummary>{"commit_to_apply", apply},
        };
        publish_prometheus(options.output_dir / "metrics.prom", options.prometheus_file,
                           options, pending.capacity(), counters, steady.samples(),
                           prometheus_summaries);

        std::cout << "benchmark complete samples=" << total.count
                  << " p50_ns=" << total.p50_ns
                  << " p99_ns=" << total.p99_ns
                  << " p999_ns=" << total.p999_ns
                  << " p9999_ns=" << total.p9999_ns
                  << " max_ns=" << total.maximum_ns
                  << " timeouts=" << counters.timed_out
                  << " capacity_rejected=" << counters.capacity_rejected
                  << " output=" << options.output_dir << '\n';
        return counters.timed_out == 0 && counters.capacity_rejected == 0 &&
                       counters.invalid_responses == 0 && counters.internal_error_responses == 0 &&
                       steady.dropped_samples() == 0
            ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "raft-bench failed: " << error.what() << '\n';
        return 1;
    }
}
