// Published tree head files: reading them back and counting the signatures that verify.
#pragma once

#include "velocita/tape/signing.hpp"
#include "velocita/v2/digest.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

namespace velocita::tape {

struct HeadFile {
    std::filesystem::path file;
    std::uint64_t incarnation{};
    std::uint64_t tick{};
    std::uint64_t tree_size{};
    v2::Digest256 root;
    std::map<int, std::vector<std::byte>> signatures;
};

[[nodiscard]] HeadFile read_head_file(const std::filesystem::path& file);

[[nodiscard]] std::size_t valid_head_signatures(const HeadFile& head, const std::array<std::optional<VerifyKey>, 3>& keys);

}
