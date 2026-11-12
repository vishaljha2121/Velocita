#include "velocita/tape/head_file.hpp"

#include "velocita/tape/reducer.hpp"

#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>

namespace velocita::tape {
namespace {

std::vector<std::byte> hex_bytes(const std::string& hex) {
    std::vector<std::byte> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(std::byte(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

}

HeadFile read_head_file(const std::filesystem::path& file) {
    std::ifstream in(file);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    std::smatch m;
    HeadFile h;
    h.file = file;
    if (!std::regex_search(text, m, std::regex(R"re("tick":(\d+),"tree_size":(\d+),"root":"([0-9a-f]{64})")re")))
        throw std::runtime_error("unreadable head " + file.string());
    h.tick = std::stoull(m[1]);
    h.tree_size = std::stoull(m[2]);
    h.root = *v2::parse_digest256(m[3].str());
    if (std::regex_search(text, m, std::regex(R"re("incarnation":(\d+))re"))) h.incarnation = std::stoull(m[1]);
    const std::regex sig(R"re("(\d)":"([0-9a-f]{128})")re");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), sig); it != std::sregex_iterator(); ++it)
        h.signatures[std::stoi((*it)[1])] = hex_bytes((*it)[2]);
    return h;
}

std::size_t valid_head_signatures(const HeadFile& head, const std::array<std::optional<VerifyKey>, 3>& keys) {
    const auto message = head_message(head.incarnation, head.tick, head.tree_size, head.root);
    std::size_t valid = 0;
    for (const auto& [voter, signature] : head.signatures)
        if (voter >= 1 && voter <= 3 && keys[voter - 1] && keys[voter - 1]->verify(message, signature)) ++valid;
    return valid;
}

}
