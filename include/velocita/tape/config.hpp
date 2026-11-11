// key=value configuration files and key loading for tape processes.
#pragma once

#include "velocita/tape/auth.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace velocita::tape {

class KvConfig {
public:
    static KvConfig load(const std::filesystem::path& path);
    [[nodiscard]] std::string get(const std::string& key) const;
    [[nodiscard]] std::optional<std::string> find(const std::string& key) const;
    [[nodiscard]] std::uint64_t number(const std::string& key, std::optional<std::uint64_t> fallback = {}) const;

private:
    std::map<std::string, std::string> values_;
};

[[nodiscard]] Key load_master_key(const std::filesystem::path& path);
[[nodiscard]] std::uint64_t random_u64();

}
