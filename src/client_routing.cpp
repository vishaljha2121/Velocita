#include "velocita/client_routing.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace velocita {
namespace {

std::int32_t parse_positive_node_id(const std::string_view value) {
    std::int32_t node_id{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), node_id);
    if (error != std::errc{} || end != value.data() + value.size() || node_id <= 0) {
        throw std::invalid_argument("Raft node ID must be a positive 32-bit integer");
    }
    return node_id;
}

}  // namespace

RequestTarget parse_request_target(const std::string_view value) {
    const auto separator = value.find('=');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 == value.size()) {
        throw std::invalid_argument("request target must be NODE_ID=URI");
    }
    return {parse_positive_node_id(value.substr(0, separator)),
            std::string(value.substr(separator + 1))};
}

std::vector<std::int32_t> parse_member_ids(const std::string_view value) {
    if (value.empty()) throw std::invalid_argument("member list must not be empty");
    std::vector<std::int32_t> members;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto separator = value.find(',', begin);
        const auto end = separator == std::string_view::npos ? value.size() : separator;
        if (end == begin) throw std::invalid_argument("member list contains an empty node ID");
        const auto node_id = parse_positive_node_id(value.substr(begin, end - begin));
        if (std::find(members.begin(), members.end(), node_id) != members.end()) {
            throw std::invalid_argument("member list contains a duplicate node ID");
        }
        members.push_back(node_id);
        if (separator == std::string_view::npos) break;
        if (separator + 1 == value.size()) {
            throw std::invalid_argument("member list contains an empty node ID");
        }
        begin = separator + 1;
    }
    return members;
}

void validate_request_targets(const std::span<const RequestTarget> targets,
                              const std::span<const std::int32_t> required_members) {
    if (targets.empty()) throw std::invalid_argument("at least one request target is required");
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const auto& target = targets[index];
        if (target.node_id <= 0) throw std::invalid_argument("request target node ID must be positive");
        if (target.channel.empty()) throw std::invalid_argument("request target channel must not be empty");
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (targets[prior].node_id == target.node_id) {
                throw std::invalid_argument("duplicate request target node ID");
            }
            if (targets[prior].channel == target.channel) {
                throw std::invalid_argument("duplicate request target channel");
            }
        }
    }

    for (std::size_t index = 0; index < required_members.size(); ++index) {
        const auto member = required_members[index];
        if (member <= 0) throw std::invalid_argument("required member node ID must be positive");
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (required_members[prior] == member) {
                throw std::invalid_argument("required member list contains a duplicate node ID");
            }
        }
        const auto found = std::find_if(targets.begin(), targets.end(), [member](const auto& target) {
            return target.node_id == member;
        });
        if (found == targets.end()) throw std::invalid_argument("request target missing for Raft member");
    }

    if (!required_members.empty()) {
        for (const auto& target : targets) {
            if (std::find(required_members.begin(), required_members.end(), target.node_id) ==
                required_members.end()) {
                throw std::invalid_argument("request target configured for a non-member node");
            }
        }
    }
}

LeaderRouter::LeaderRouter(std::vector<RequestTarget> targets,
                           std::vector<std::int32_t> required_members)
    : targets_(std::move(targets)) {
    validate_request_targets(targets_, required_members);
}

std::size_t LeaderRouter::preferred_target() const noexcept {
    return leader_index_.value_or(discovery_index_);
}

std::size_t LeaderRouter::next_discovery_target(const std::size_t previous_target) {
    if (previous_target >= targets_.size()) throw std::out_of_range("request target index");
    discovery_index_ = (previous_target + 1) % targets_.size();
    return leader_index_.value_or(discovery_index_);
}

void LeaderRouter::mark_target_unavailable(const std::size_t unavailable_target) {
    if (unavailable_target >= targets_.size()) throw std::out_of_range("request target index");
    if (leader_index_ == unavailable_target) {
        leader_index_.reset();
        state_ = LeaderKnowledgeState::unknown;
    }
    discovery_index_ = (unavailable_target + 1) % targets_.size();
}

LeaderHintResult LeaderRouter::observe(const Response& response) {
    if (response.term < 0) return LeaderHintResult::invalid;
    if (response.term < observed_term_) return LeaderHintResult::stale;

    if (response.term > observed_term_) {
        observed_term_ = response.term;
        leader_index_.reset();
        state_ = LeaderKnowledgeState::unknown;
    }

    if (response.leader_id < -1 || response.leader_id == 0) {
        return LeaderHintResult::invalid;
    }

    if (response.leader_id == -1) {
        leader_index_.reset();
        state_ = LeaderKnowledgeState::unknown;
        const bool leader_required = response.status == ResponseStatus::ok &&
                                     response.wire_version >= kResponseWireVersion;
        return leader_required
            ? LeaderHintResult::invalid : LeaderHintResult::unknown;
    }

    const auto hinted_index = target_index(response.leader_id);
    if (!hinted_index) return LeaderHintResult::invalid;
    leader_index_ = *hinted_index;
    discovery_index_ = *hinted_index;
    if (response.status == ResponseStatus::ok) {
        state_ = LeaderKnowledgeState::known;
        return LeaderHintResult::confirmed;
    }
    state_ = LeaderKnowledgeState::suspect;
    return LeaderHintResult::updated;
}

std::optional<std::int32_t> LeaderRouter::leader_id() const noexcept {
    if (!leader_index_) return std::nullopt;
    return targets_[*leader_index_].node_id;
}

const RequestTarget& LeaderRouter::target(const std::size_t target_index_value) const {
    if (target_index_value >= targets_.size()) throw std::out_of_range("request target index");
    return targets_[target_index_value];
}

std::optional<std::size_t> LeaderRouter::target_index(const std::int32_t node_id) const noexcept {
    for (std::size_t index = 0; index < targets_.size(); ++index) {
        if (targets_[index].node_id == node_id) return index;
    }
    return std::nullopt;
}

std::string_view to_string(const LeaderKnowledgeState state) noexcept {
    switch (state) {
        case LeaderKnowledgeState::unknown: return "unknown";
        case LeaderKnowledgeState::suspect: return "suspect";
        case LeaderKnowledgeState::known: return "known";
    }
    return "unknown";
}

}  // namespace velocita
