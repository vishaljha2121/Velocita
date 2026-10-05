// V1: raft-bench-merge.
#include "velocita/benchmark.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path output;
    std::vector<std::filesystem::path> inputs;
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help") {
            std::cout << "raft-bench-merge --output FILE --samples FILE...\n";
            std::exit(0);
        }
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + key);
        const std::string value = argv[++i];
        if (key == "--output") options.output = value;
        else if (key == "--samples") options.inputs.emplace_back(value);
        else throw std::runtime_error("unknown option: " + key);
    }
    if (options.output.empty() || options.inputs.empty()) {
        throw std::runtime_error("--output and at least one --samples are required");
    }
    return options;
}

std::vector<std::string_view> columns(const std::string& line) {
    std::vector<std::string_view> result;
    std::size_t begin{};
    for (std::size_t position = 0; position <= line.size(); ++position) {
        if (position == line.size() || line[position] == ',') {
            result.emplace_back(line.data() + begin, position - begin);
            begin = position + 1;
        }
    }
    return result;
}

std::uint64_t number(const std::string_view value) {
    std::size_t consumed{};
    const auto parsed = std::stoull(std::string(value), &consumed);
    if (consumed != value.size()) throw std::runtime_error("invalid numeric CSV field");
    return parsed;
}

std::vector<velocita::LatencySample> read_samples(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("open samples failed: " + path.string());
    std::string line;
    if (!std::getline(input, line)) throw std::runtime_error("empty samples file: " + path.string());
    std::vector<velocita::LatencySample> result;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto value = columns(line);
        if (value.size() != 11) throw std::runtime_error("unexpected sample schema: " + path.string());
        velocita::LatencySample sample;
        sample.request_id = number(value[0]);
        sample.scheduled_to_offer_ns = number(value[1]);
        sample.offer_to_response_ns = number(value[2]);
        sample.scheduled_to_response_ns = number(value[3]);
        sample.ingress_to_admission_ns = number(value[4]);
        sample.local_wal_append_ns = number(value[5]);
        sample.quorum_append_ns = number(value[6]);
        sample.admission_to_commit_ns = number(value[7]);
        sample.commit_to_apply_ns = number(value[8]);
        sample.retries = static_cast<std::uint32_t>(number(value[9]));
        sample.internal_timing_valid = number(value[10]) != 0;
        result.push_back(sample);
    }
    return result;
}

velocita::DistributionSummary summarize(
    const std::span<const velocita::LatencySample> samples,
    const std::function<std::uint64_t(const velocita::LatencySample&)>& field,
    const bool internal_only = false) {
    std::vector<std::uint64_t> values;
    values.reserve(samples.size());
    for (const auto& sample : samples) {
        if (!internal_only || sample.internal_timing_valid) values.push_back(field(sample));
    }
    return velocita::summarize_values(values);
}

void write_distribution(std::ostream& output, const velocita::DistributionSummary& value) {
    output << "{\"count\":" << value.count << ",\"min_ns\":" << value.minimum_ns
           << ",\"mean_ns\":" << std::fixed << std::setprecision(3)
           << static_cast<double>(value.mean_ns) << ",\"p50_ns\":" << value.p50_ns
           << ",\"p90_ns\":" << value.p90_ns << ",\"p99_ns\":" << value.p99_ns
           << ",\"p999_ns\":" << value.p999_ns << ",\"p9999_ns\":" << value.p9999_ns
           << ",\"max_ns\":" << value.maximum_ns << '}';
}

}

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        std::vector<velocita::LatencySample> merged;
        for (const auto& input : options.inputs) {
            auto samples = read_samples(input);
            merged.insert(merged.end(), samples.begin(), samples.end());
        }
        std::ofstream output(options.output);
        if (!output) throw std::runtime_error("open merged summary failed: " + options.output.string());
        output << "{\n  \"schema_version\":1,\n  \"source_files\":" << options.inputs.size()
               << ",\n  \"sample_count\":" << merged.size();
        struct Field {
            std::string_view name;
            std::function<std::uint64_t(const velocita::LatencySample&)> value;
            bool internal_only{};
        };
        const std::array<Field, 8> fields{{
            {"scheduled_to_response", [](const auto& sample) { return sample.scheduled_to_response_ns; }, false},
            {"scheduled_to_offer", [](const auto& sample) { return sample.scheduled_to_offer_ns; }, false},
            {"offer_to_response", [](const auto& sample) { return sample.offer_to_response_ns; }, false},
            {"ingress_to_admission", [](const auto& sample) { return sample.ingress_to_admission_ns; }, true},
            {"local_wal_append", [](const auto& sample) { return sample.local_wal_append_ns; }, true},
            {"quorum_append", [](const auto& sample) { return sample.quorum_append_ns; }, true},
            {"admission_to_commit", [](const auto& sample) { return sample.admission_to_commit_ns; }, true},
            {"commit_to_apply", [](const auto& sample) { return sample.commit_to_apply_ns; }, true},
        }};
        for (const auto& field : fields) {
            const auto& [name, value, internal_only] = field;
            output << ",\n  \"" << name << "\":";
            write_distribution(output, summarize(merged, value, internal_only));
        }
        output << "\n}\n";
        std::cout << "merged " << merged.size() << " exact samples from " << options.inputs.size()
                  << " files into " << options.output << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "raft-bench-merge failed: " << error.what() << '\n';
        return 1;
    }
}
