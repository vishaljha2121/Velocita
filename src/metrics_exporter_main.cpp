#include "velocita/shared_metrics.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::atomic_bool running{true};

void stop(int) {
    running.store(false);
}

struct Options {
    std::string listen_address{"127.0.0.1"};
    std::uint16_t port{9200};
    std::vector<std::pair<std::int32_t, std::filesystem::path>> metric_files;
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help") {
            std::cout << "raft-metrics-exporter [--listen-address IP] [--port PORT] "
                         "--metrics-file NODE_ID=PATH...\n";
            std::exit(0);
        }
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + key);
        const std::string value = argv[++i];
        if (key == "--listen-address") options.listen_address = value;
        else if (key == "--port") {
            const auto port = std::stoul(value);
            if (port == 0 || port > 65535) throw std::runtime_error("port must be between 1 and 65535");
            options.port = static_cast<std::uint16_t>(port);
        } else if (key == "--metrics-file") {
            const auto separator = value.find('=');
            if (separator == std::string::npos) throw std::runtime_error("metrics file must be NODE_ID=PATH");
            options.metric_files.emplace_back(std::stoi(value.substr(0, separator)),
                                              value.substr(separator + 1));
        } else {
            throw std::runtime_error("unknown option: " + key);
        }
    }
    if (options.metric_files.empty()) throw std::runtime_error("at least one --metrics-file is required");
    return options;
}

void help(std::ostringstream& output, const std::string_view name,
          const std::string_view description, const std::string_view type) {
    output << "# HELP " << name << ' ' << description << '\n'
           << "# TYPE " << name << ' ' << type << '\n';
}

template <typename Value>
void node_metric(std::ostringstream& output, const std::string_view name,
                 const std::int32_t node_id, const Value value) {
    output << name << "{node_id=\"" << node_id << "\"} " << value << '\n';
}

std::string_view role_name(const velocita::MetricRole role) {
    switch (role) {
        case velocita::MetricRole::follower: return "follower";
        case velocita::MetricRole::candidate: return "candidate";
        case velocita::MetricRole::leader: return "leader";
        default: return "none";
    }
}

std::uint64_t monotonic_nanoseconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string render_metrics(const Options& options) {
    std::ostringstream output;
    help(output, "velocita_metrics_page_up", "Whether a valid shared-memory metrics page was readable.", "gauge");
    help(output, "velocita_metrics_writer_active", "Whether the node marked its shared-memory writer active.", "gauge");
    help(output, "velocita_metrics_last_tick_age_seconds", "Seconds since the node completed raft_periodic.", "gauge");
    help(output, "velocita_process_start_time_seconds", "Node process start time since Unix epoch.", "gauge");
    help(output, "velocita_raft_tick_total", "Completed raft_periodic calls.", "counter");
    help(output, "velocita_raft_role", "Current Raft role as a one-hot gauge.", "gauge");
    help(output, "velocita_raft_leader_id", "Current known Raft leader ID, or -1 when unknown.", "gauge");
    help(output, "velocita_raft_term", "Current Raft term.", "gauge");
    help(output, "velocita_raft_log_current_index", "Last local Raft log index.", "gauge");
    help(output, "velocita_raft_commit_index", "Highest Raft log index known committed.", "gauge");
    help(output, "velocita_raft_applied_index", "Highest Raft log index applied to the FSM.", "gauge");
    help(output, "velocita_raft_commit_lag_entries", "Local entries not yet committed.", "gauge");
    help(output, "velocita_raft_apply_lag_entries", "Committed entries not yet applied.", "gauge");
    help(output, "velocita_raft_counter_value", "Current replicated counter state.", "gauge");
    help(output, "velocita_raft_observed_role_changes_total", "Role changes observed by the 100 ms status sampler.", "counter");
    help(output, "velocita_raft_observed_leader_changes_total", "Leader changes observed by the 100 ms status sampler.", "counter");
    help(output, "velocita_raft_observed_term_changes_total", "Term changes observed by the 100 ms status sampler.", "counter");
    help(output, "velocita_aeron_fragments_received_total", "Complete Aeron fragments delivered to the node.", "counter");
    help(output, "velocita_aeron_bytes_received_total", "Aeron payload bytes delivered to the node.", "counter");
    help(output, "velocita_aeron_messages_rejected_total", "Aeron messages rejected during decode or handling.", "counter");
    help(output, "velocita_aeron_outbound_peer_messages_total", "Raft peer messages queued for Aeron publication.", "counter");
    help(output, "velocita_aeron_dropped_peer_messages_total", "Peer messages dropped at the bounded queue limit.", "counter");
    help(output, "velocita_raft_completed_responses_total", "Committed responses produced for pending client commands.", "counter");
    help(output, "velocita_aeron_queue_depth", "Messages currently waiting for Aeron publication.", "gauge");
    help(output, "velocita_aeron_queue_high_watermark", "Largest Aeron publication queue depth since process start.", "gauge");
    help(output, "velocita_aeron_publication_offers_total", "Aeron publication offers by channel and outcome.", "counter");

    const auto now = monotonic_nanoseconds();
    for (const auto& [expected_node_id, path] : options.metric_files) {
        const auto snapshot = velocita::read_shared_metrics(path);
        if (!snapshot || snapshot->header.node_id != expected_node_id) {
            node_metric(output, "velocita_metrics_page_up", expected_node_id, 0);
            continue;
        }
        const auto& value = *snapshot;
        node_metric(output, "velocita_metrics_page_up", expected_node_id, 1);
        node_metric(output, "velocita_metrics_writer_active", expected_node_id, value.active ? 1 : 0);
        const double tick_age = now >= value.last_tick_monotonic_nanoseconds
            ? static_cast<double>(now - value.last_tick_monotonic_nanoseconds) / 1'000'000'000.0
            : 0.0;
        node_metric(output, "velocita_metrics_last_tick_age_seconds", expected_node_id, tick_age);
        node_metric(output, "velocita_process_start_time_seconds", expected_node_id,
                    value.header.process_start_time_seconds);
        node_metric(output, "velocita_raft_tick_total", expected_node_id, value.tick_total);
        for (const auto role : {velocita::MetricRole::none, velocita::MetricRole::follower,
                                velocita::MetricRole::candidate, velocita::MetricRole::leader}) {
            output << "velocita_raft_role{node_id=\"" << expected_node_id << "\",role=\""
                   << role_name(role) << "\"} " << (value.role == role ? 1 : 0) << '\n';
        }
        node_metric(output, "velocita_raft_leader_id", expected_node_id, value.leader_id);
        node_metric(output, "velocita_raft_term", expected_node_id, value.term);
        node_metric(output, "velocita_raft_log_current_index", expected_node_id, value.current_index);
        node_metric(output, "velocita_raft_commit_index", expected_node_id, value.commit_index);
        node_metric(output, "velocita_raft_applied_index", expected_node_id, value.applied_index);
        node_metric(output, "velocita_raft_commit_lag_entries", expected_node_id,
                    value.current_index >= value.commit_index ? value.current_index - value.commit_index : 0);
        node_metric(output, "velocita_raft_apply_lag_entries", expected_node_id,
                    value.commit_index >= value.applied_index ? value.commit_index - value.applied_index : 0);
        node_metric(output, "velocita_raft_counter_value", expected_node_id, value.counter_value);
        node_metric(output, "velocita_raft_observed_role_changes_total", expected_node_id,
                    value.observed_role_changes_total);
        node_metric(output, "velocita_raft_observed_leader_changes_total", expected_node_id,
                    value.observed_leader_changes_total);
        node_metric(output, "velocita_raft_observed_term_changes_total", expected_node_id,
                    value.observed_term_changes_total);

        output << "velocita_aeron_fragments_received_total{node_id=\"" << expected_node_id
               << "\",channel=\"request\"} " << value.request_fragments_total << '\n'
               << "velocita_aeron_fragments_received_total{node_id=\"" << expected_node_id
               << "\",channel=\"peer\"} " << value.peer_fragments_total << '\n'
               << "velocita_aeron_bytes_received_total{node_id=\"" << expected_node_id
               << "\",channel=\"request\"} " << value.request_bytes_total << '\n'
               << "velocita_aeron_bytes_received_total{node_id=\"" << expected_node_id
               << "\",channel=\"peer\"} " << value.peer_bytes_total << '\n'
               << "velocita_aeron_messages_rejected_total{node_id=\"" << expected_node_id
               << "\",channel=\"request\"} " << value.rejected_requests_total << '\n'
               << "velocita_aeron_messages_rejected_total{node_id=\"" << expected_node_id
               << "\",channel=\"peer\"} " << value.rejected_peer_messages_total << '\n';
        node_metric(output, "velocita_aeron_outbound_peer_messages_total", expected_node_id,
                    value.outbound_peer_messages_total);
        node_metric(output, "velocita_aeron_dropped_peer_messages_total", expected_node_id,
                    value.dropped_peer_messages_total);
        node_metric(output, "velocita_raft_completed_responses_total", expected_node_id,
                    value.completed_responses_total);
        output << "velocita_aeron_queue_depth{node_id=\"" << expected_node_id
               << "\",channel=\"peer\"} " << value.peer_queue_depth << '\n'
               << "velocita_aeron_queue_depth{node_id=\"" << expected_node_id
               << "\",channel=\"response\"} " << value.response_queue_depth << '\n'
               << "velocita_aeron_queue_high_watermark{node_id=\"" << expected_node_id
               << "\",channel=\"peer\"} " << value.peer_queue_high_watermark << '\n'
               << "velocita_aeron_queue_high_watermark{node_id=\"" << expected_node_id
               << "\",channel=\"response\"} " << value.response_queue_high_watermark << '\n';
        const std::array peer_offers{
            std::pair{"success", value.peer_offer_success_total},
            std::pair{"not_connected", value.peer_offer_not_connected_total},
            std::pair{"back_pressured", value.peer_offer_back_pressured_total},
            std::pair{"admin_action", value.peer_offer_admin_action_total},
            std::pair{"failed", value.peer_offer_failed_total},
        };
        const std::array response_offers{
            std::pair{"success", value.response_offer_success_total},
            std::pair{"not_connected", value.response_offer_not_connected_total},
            std::pair{"back_pressured", value.response_offer_back_pressured_total},
            std::pair{"admin_action", value.response_offer_admin_action_total},
            std::pair{"failed", value.response_offer_failed_total},
        };
        for (const auto& [outcome, count] : peer_offers) {
            output << "velocita_aeron_publication_offers_total{node_id=\"" << expected_node_id
                   << "\",channel=\"peer\",outcome=\"" << outcome << "\"} " << count << '\n';
        }
        for (const auto& [outcome, count] : response_offers) {
            output << "velocita_aeron_publication_offers_total{node_id=\"" << expected_node_id
                   << "\",channel=\"response\",outcome=\"" << outcome << "\"} " << count << '\n';
        }
    }
    return output.str();
}

std::string http_response(const std::string_view status, const std::string_view content_type,
                          const std::string_view body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    return response.str();
}

void send_all(const int socket, const std::string& response) {
    std::size_t sent{};
    while (sent < response.size()) {
#ifdef MSG_NOSIGNAL
        const auto result = ::send(socket, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
#else
        const auto result = ::send(socket, response.data() + sent, response.size() - sent, 0);
#endif
        if (result > 0) sent += static_cast<std::size_t>(result);
        else if (result < 0 && errno == EINTR) continue;
        else return;
    }
}

int listen_socket(const Options& options) {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) throw std::runtime_error("creating exporter socket failed: " + std::string(std::strerror(errno)));
    const int reuse = 1;
    if (::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ::close(listener);
        throw std::runtime_error("configuring exporter socket failed");
    }
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(options.port);
    if (::inet_pton(AF_INET, options.listen_address.c_str(), &endpoint.sin_addr) != 1) {
        ::close(listener);
        throw std::runtime_error("listen address must be an IPv4 address");
    }
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) < 0 ||
        ::listen(listener, 16) < 0) {
        const auto message = std::string(std::strerror(errno));
        ::close(listener);
        throw std::runtime_error("binding exporter endpoint failed: " + message);
    }
    return listener;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        const int listener = listen_socket(options);
        std::cout << "Raft shared-memory metrics exporter listening on "
                  << options.listen_address << ':' << options.port << std::endl;
        while (running.load()) {
            pollfd descriptor{listener, POLLIN, 0};
            const int ready = ::poll(&descriptor, 1, 500);
            if (ready < 0 && errno == EINTR) continue;
            if (ready <= 0 || (descriptor.revents & POLLIN) == 0) continue;
            const int client = ::accept(listener, nullptr, nullptr);
            if (client < 0) continue;
            pollfd request_ready{client, POLLIN, 0};
            if (::poll(&request_ready, 1, 1000) > 0) {
                std::array<char, 2048> request{};
                const auto length = ::recv(client, request.data(), request.size(), 0);
                const std::string_view line(request.data(), length > 0 ? static_cast<std::size_t>(length) : 0);
                if (line.starts_with("GET /metrics ")) {
                    const auto body = render_metrics(options);
                    send_all(client, http_response("200 OK", "text/plain; version=0.0.4; charset=utf-8", body));
                } else {
                    constexpr std::string_view body = "not found\n";
                    send_all(client, http_response("404 Not Found", "text/plain; charset=utf-8", body));
                }
            }
            ::close(client);
        }
        ::close(listener);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "raft-metrics-exporter failed: " << error.what() << '\n';
        return 1;
    }
}
