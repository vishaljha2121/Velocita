#include "velocita/tape/config.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <sys/random.h>
#include <unistd.h>
#include <vector>

namespace velocita::tape {
namespace {

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}

KvConfig KvConfig::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read config " + path.string());
    KvConfig c;
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) throw std::runtime_error("config line " + std::to_string(n) + " has no '='");
        const auto key = trim(line.substr(0, eq));
        if (c.values_.contains(key)) throw std::runtime_error("config key repeated: " + key);
        c.values_[key] = trim(line.substr(eq + 1));
    }
    return c;
}

std::string KvConfig::get(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) throw std::runtime_error("config is missing " + key);
    return it->second;
}

std::optional<std::string> KvConfig::find(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

std::uint64_t KvConfig::number(const std::string& key, std::optional<std::uint64_t> fallback) const {
    const auto v = find(key);
    if (!v) {
        if (fallback) return *fallback;
        throw std::runtime_error("config is missing " + key);
    }
    std::size_t used = 0;
    const auto n = std::stoull(*v, &used);
    if (used != v->size()) throw std::runtime_error("config value is not a number: " + key);
    return n;
}

Key load_master_key(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read master key " + path.string());
    const std::vector<char> raw((std::istreambuf_iterator<char>(in)), {});
    Key k{};
    if (raw.size() == k.size()) {
        for (std::size_t i = 0; i < k.size(); ++i) k[i] = std::byte(static_cast<unsigned char>(raw[i]));
        return k;
    }
    std::string text(raw.begin(), raw.end());
    text = trim(text);
    if (text.size() != 64) throw std::runtime_error("master key must be 32 bytes or 64 hex characters");
    for (std::size_t i = 0; i < k.size(); ++i) {
        const int hi = hex(text[2 * i]), lo = hex(text[2 * i + 1]);
        if (hi < 0 || lo < 0) throw std::runtime_error("master key is not hex");
        k[i] = std::byte(static_cast<unsigned char>(hi * 16 + lo));
    }
    return k;
}

std::uint64_t random_u64() {
    std::uint64_t v = 0;
    if (::getentropy(&v, sizeof(v)) != 0) throw std::runtime_error("entropy unavailable");
    return v;
}

}
