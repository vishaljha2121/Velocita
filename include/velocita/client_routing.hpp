// V1: client-side leader discovery and routing.
#pragma once

#include "velocita/wire.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace velocita {

struct RequestTarget {
    std::int32_t node_id{};
    std::string channel;
};

RequestTarget parse_request_target(std::string_view value);
std::vector<std::int32_t> parse_member_ids(std::string_view value);
void validate_request_targets(std::span<const RequestTarget> targets,
                              std::span<const std::int32_t> required_members = {});

enum class LeaderHintResult : std::uint8_t {
    confirmed,
    updated,
    unknown,
    stale,
    invalid,
};

enum class LeaderKnowledgeState : std::uint8_t {
    unknown,
    suspect,
    known,
};

class LeaderRouter {
public:
    explicit LeaderRouter(std::vector<RequestTarget> targets,
                          std::vector<std::int32_t> required_members = {});

    [[nodiscard]] std::size_t preferred_target() const noexcept;
    [[nodiscard]] std::size_t next_discovery_target(std::size_t previous_target);
    void mark_target_unavailable(std::size_t target_index);
    [[nodiscard]] LeaderHintResult observe(const Response& response);

    [[nodiscard]] std::optional<std::int32_t> leader_id() const noexcept;
    [[nodiscard]] LeaderKnowledgeState state() const noexcept { return state_; }
    [[nodiscard]] std::int64_t observed_term() const noexcept { return observed_term_; }
    [[nodiscard]] std::size_t target_count() const noexcept { return targets_.size(); }
    [[nodiscard]] const RequestTarget& target(std::size_t target_index) const;
    [[nodiscard]] std::optional<std::size_t> target_index(std::int32_t node_id) const noexcept;

private:
    std::vector<RequestTarget> targets_;
    std::size_t discovery_index_{};
    std::optional<std::size_t> leader_index_;
    std::int64_t observed_term_{-1};
    LeaderKnowledgeState state_{LeaderKnowledgeState::unknown};
};

[[nodiscard]] std::string_view to_string(LeaderKnowledgeState state) noexcept;

}
