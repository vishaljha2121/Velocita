#include "velocita/v2/node_protocol.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace velocita::v2;
using Bytes = std::vector<std::byte>;
using Error = NodeProtocolError;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Bytes hex(std::string_view text) {
    Bytes out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out.push_back(static_cast<std::byte>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16)));
    return out;
}

ResponseCorrelation correlation() {
    ResponseCorrelation value;
    value.request_sequence = 0x0102030405060708ULL;
    for (std::size_t i = 0; i < 32; ++i) value.payload_digest.bytes[i] = static_cast<std::byte>(i);
    return value;
}

const Bytes kAdmitted = hex(
    "010807060504030201000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
    "05000000000000002a00000000000000");
const Bytes kRefusedNotLeader = hex(
    "020807060504030201000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
    "0202000000");
const Bytes kCommitted = hex(
    "030807060504030201000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
    "05000000000000002a0000000000000000070e151c232a31383f464d545b626970777e858c939aa1a8"
    "afb6bdc4cbd2d9e0e7eef5fc030a11181f262d343b424950575e656c737a81888f969da4abb2b9c0c7"
    "ced5dce3eaf1f8ff060d141b222930373e454c535a61686f767d848b9299a0a7aeb5bcc3cad1d8dfe6"
    "edf4fb020910171e252c333a41484f565d646b7279");

NodeResponse admitted() {
    NodeResponse r;
    r.kind = ResponseKind::admitted;
    r.correlation = correlation();
    r.term = 5;
    r.index = 42;
    return r;
}
NodeResponse refused_not_leader() {
    NodeResponse r;
    r.kind = ResponseKind::refused;
    r.correlation = correlation();
    r.reason = static_cast<std::uint8_t>(ProposeStatus::not_leader);
    r.leader_id = 2;
    return r;
}
NodeResponse committed() {
    NodeResponse r = admitted();
    r.kind = ResponseKind::committed;
    for (std::size_t i = 0; i < r.result.size(); ++i) r.result[i] = static_cast<std::byte>((i * 7) & 0xff);
    return r;
}

Bytes encode(const NodeResponse& response) {
    std::array<std::byte, kMaxResponseBodyBytes> out{};
    const auto result = encode_response(response, out);
    require(result.error == Error::none, "valid response refused");
    return {out.begin(), out.begin() + static_cast<std::ptrdiff_t>(result.bytes_written)};
}

void responses_match_goldens_and_round_trip() {
    const std::array<std::pair<NodeResponse, const Bytes*>, 3> cases{{
        {admitted(), &kAdmitted}, {refused_not_leader(), &kRefusedNotLeader}, {committed(), &kCommitted}}};
    for (const auto& [response, golden] : cases) {
        require(encode(response) == *golden, "response bytes differ from the Python oracle");
        NodeResponse decoded;
        require(decode_response(*golden, decoded) == Error::none && decoded == response,
                "golden response did not decode to its fields");
    }
    require(kAdmitted.size() == kResponseAdmittedBytes && kRefusedNotLeader.size() == kResponseRefusedBytes &&
                kCommitted.size() == kResponseCommittedBytes,
            "response sizes differ from the reviewed layout");
}

void response_decoding_refuses_malformed_input() {
    const auto expect = [](const Bytes& input, Error error, const std::string& what) {
        NodeResponse out = admitted();
        const auto before = out;
        require(decode_response(input, out) == error, what);
        require(out == before, what + ": refusal modified the destination");
    };
    for (const auto* golden : {&kAdmitted, &kRefusedNotLeader, &kCommitted}) {
        for (std::size_t length = 0; length < golden->size(); ++length)
            require(decode_response(std::span(*golden).first(length), *std::make_unique<NodeResponse>()) !=
                        Error::none,
                    "truncated response accepted at length " + std::to_string(length));
        auto extended = *golden;
        extended.push_back(std::byte{0});
        expect(extended, Error::length_mismatch, "trailing byte");
    }
    for (const int kind : {0, 4, 255}) {
        auto copy = kAdmitted;
        copy[0] = static_cast<std::byte>(kind);
        expect(copy, Error::invalid_kind, "kind " + std::to_string(kind));
    }
    for (const int reason : {0, 1, 10, 63, 68, 255}) {
        auto copy = kRefusedNotLeader;
        copy[41] = static_cast<std::byte>(reason);
        expect(copy, Error::invalid_reason, "reason " + std::to_string(reason));
    }
    for (const int reason : {2, 9, 64, 67}) {
        auto copy = kRefusedNotLeader;
        copy[41] = static_cast<std::byte>(reason);
        NodeResponse out;
        require(decode_response(copy, out) == Error::none && out.reason == reason,
                "valid reason " + std::to_string(reason) + " refused");
    }
}

void ingress_round_trips_and_refuses() {
    const Bytes command(100, std::byte{0x5c});
    std::array<std::byte, kMaxIngressBodyBytes + 1> out{};
    const auto encoded = encode_ingress(ProposeLane::control, command, out);
    require(encoded.error == Error::none && encoded.bytes_written == 101 && out[0] == std::byte{1},
            "control-lane ingress layout differs");
    IngressView view;
    require(decode_ingress(std::span(out).first(encoded.bytes_written), view) == Error::none &&
                view.lane == ProposeLane::control && view.command.size() == 100 &&
                view.command.data() == out.data() + 1,
            "ingress did not decode to a borrowed command");

    const auto refuses = [&](ProposeLane lane, std::size_t size, std::size_t output, Error error, const char* what) {
        out.fill(std::byte{0xEE});
        const Bytes body(size, std::byte{0x11});
        const auto result = encode_ingress(lane, body, std::span(out).first(output));
        require(result.error == error && result.bytes_written == 0, what);
        require(std::all_of(out.begin(), out.end(), [](std::byte b) { return b == std::byte{0xEE}; }),
                std::string(what) + " wrote output on refusal");
    };
    refuses(ProposeLane::normal, 0, out.size(), Error::empty_command, "empty command");
    refuses(ProposeLane::normal, kMaxKernelCommandBytes + 1, out.size(), Error::command_too_large, "oversized command");
    refuses(ProposeLane::normal, 10, 10, Error::output_too_small, "short output");
    refuses(static_cast<ProposeLane>(2), 10, out.size(), Error::invalid_lane, "unknown lane");

    const auto decode_error = [](Bytes input) {
        IngressView ignored;
        return decode_ingress(input, ignored);
    };
    require(decode_error({}) == Error::truncated, "empty ingress body accepted");
    require(decode_error({std::byte{0}}) == Error::empty_command, "lane without command accepted");
    require(decode_error({std::byte{2}, std::byte{1}}) == Error::invalid_lane, "unknown lane decoded");
    require(decode_error(Bytes(kMaxIngressBodyBytes + 1, std::byte{0})) == Error::command_too_large,
            "oversized ingress decoded");
}

}

int main() {
    try {
        responses_match_goldens_and_round_trip();
        response_decoding_refuses_malformed_input();
        ingress_round_trips_and_refuses();
        std::cout << "node protocol tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "node protocol tests failed: " << error.what() << '\n';
        return 1;
    }
}
