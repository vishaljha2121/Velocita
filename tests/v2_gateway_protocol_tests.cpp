#include "velocita/v2/gateway_loop.hpp"
#include "velocita/v2/node_config.hpp"
#include "velocita/v2/node_protocol.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita::v2;
using Bytes = std::vector<std::byte>;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::array<std::byte, 16> incarnation(std::uint8_t seed) {
    std::array<std::byte, 16> value{};
    for (std::size_t i = 0; i < value.size(); ++i) value[i] = static_cast<std::byte>(seed + i);
    return value;
}

Digest256 digest(std::uint8_t seed) {
    Digest256 value;
    for (std::size_t i = 0; i < value.bytes.size(); ++i) value.bytes[i] = static_cast<std::byte>(seed ^ i);
    return value;
}

AuthorityBarrier barrier(std::uint8_t seed = 3) {
    return {incarnation(seed), 7, EffectId{11}, digest(0x21), KillEpoch{2}, ExecutorGeneration{3},
            digest(0x31), digest(0x41)};
}

std::uint64_t le64(std::span<const std::byte> bytes, std::size_t at) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) value |= std::to_integer<std::uint64_t>(bytes[at + i]) << (8 * i);
    return value;
}

void every_prefix_is_truncated(std::span<const std::byte> frame, const std::function<NodeProtocolError(std::span<const std::byte>)>& decode,
                               const std::string& what) {
    for (std::size_t n = 0; n < frame.size(); ++n) {
        const auto error = decode(frame.first(n));
        require(error == NodeProtocolError::truncated, what + ": prefix " + std::to_string(n) + " was not truncated");
    }
    Bytes longer(frame.begin(), frame.end());
    longer.push_back(std::byte{0});
    require(decode(longer) == NodeProtocolError::length_mismatch, what + ": a trailing byte was accepted");
}

void test_gateway_request_bytes_and_round_trip() {
    const GatewayRequest state{GatewayRequestKind::state_read, incarnation(1), 0x0102030405060708ULL, {}};
    std::array<std::byte, kGatewayAuthorityRequestBytes> out{};
    const auto encoded = encode_gateway_request(state, out);
    require(encoded.error == NodeProtocolError::none && encoded.bytes_written == 26, "state read is not 26 bytes");
    require(out[0] == std::byte{0x10} && out[1] == std::byte{1}, "state read kind/version bytes");
    for (std::size_t i = 0; i < 16; ++i) require(out[2 + i] == incarnation(1)[i], "incarnation not at offset 2");
    require(le64(out, 18) == 0x0102030405060708ULL, "request id not little-endian at offset 18");
    GatewayRequest decoded;
    require(decode_gateway_request(std::span(out).first(26), decoded) == NodeProtocolError::none && decoded == state,
            "state read did not round trip");

    auto authority = GatewayRequest{GatewayRequestKind::authority, incarnation(3), 9, barrier(3)};
    const auto framed = encode_gateway_request(authority, out);
    require(framed.error == NodeProtocolError::none && framed.bytes_written == 212, "authority request is not 212 bytes");
    std::array<std::byte, kAuthorityBarrierBytes> expected{};
    require(encode_authority_barrier(kAuthorityBarrierEntryType, barrier(3), expected).error ==
                AuthorityBarrierCodecError::none, "fixture barrier did not encode");
    require(std::equal(expected.begin(), expected.end(), out.begin() + 26), "barrier frame not verbatim at offset 26");
    require(decode_gateway_request(out, decoded) == NodeProtocolError::none && decoded == authority,
            "authority request did not round trip");
    every_prefix_is_truncated(std::span<const std::byte>(out), [](auto bytes) {
        GatewayRequest ignored;
        auto error = decode_gateway_request(bytes, ignored);
        return error;
    }, "authority request");

    authority.barrier.incarnation = incarnation(4);
    require(encode_gateway_request(authority, out).error == NodeProtocolError::invalid_value,
            "encoder accepted a barrier for a different incarnation");
    auto tampered = Bytes(out.begin(), out.end());
    require(encode_gateway_request({GatewayRequestKind::authority, incarnation(3), 9, barrier(3)}, tampered).error ==
                NodeProtocolError::none, "fixture re-encode failed");
    tampered[2] ^= std::byte{0xff};
    require(decode_gateway_request(tampered, decoded) == NodeProtocolError::invalid_value,
            "decoder accepted a header incarnation that differs from the barrier's");

    Bytes zero_id(out.begin(), out.begin() + 26);
    zero_id[0] = std::byte{0x10};
    for (std::size_t i = 18; i < 26; ++i) zero_id[i] = std::byte{0};
    require(decode_gateway_request(zero_id, decoded) == NodeProtocolError::invalid_value, "request id zero accepted");
    zero_id[18] = std::byte{1};
    zero_id[1] = std::byte{2};
    require(decode_gateway_request(zero_id, decoded) == NodeProtocolError::invalid_version, "version 2 accepted");
    zero_id[1] = std::byte{1};
    zero_id[0] = std::byte{0x12};
    require(decode_gateway_request(zero_id, decoded) == NodeProtocolError::invalid_kind, "kind 0x12 accepted");

    IngressView ingress;
    require(decode_ingress(std::span(out).first(26), ingress) == NodeProtocolError::invalid_lane,
            "decode_ingress accepted a gateway request as a lane");
}

void test_correlation_binds_the_exact_request_body() {
    std::array<std::byte, 26> a{}, b{};
    require(encode_gateway_request({GatewayRequestKind::state_read, incarnation(1), 5, {}}, a).error ==
                NodeProtocolError::none, "fixture a");
    require(encode_gateway_request({GatewayRequestKind::state_read, incarnation(2), 5, {}}, b).error ==
                NodeProtocolError::none, "fixture b");
    const auto ca = gateway_correlation(a), cb = gateway_correlation(b);
    require(ca.request_sequence == 5 && cb.request_sequence == 5, "correlation does not carry the request id");
    require(ca.payload_digest != cb.payload_digest, "two processes' identical request ids share a correlation");
    Sha256 hash;
    const std::string tag = "velocita.v2.gateway-request.1";
    hash.update(std::as_bytes(std::span(tag.data(), tag.size())));
    hash.update(a);
    require(ca.payload_digest == hash.finish(), "correlation digest is not SHA-256(tag || body)");
}

void test_refusal_bytes() {
    const GatewayRefusal refusal{{42, digest(9)}, 68, 3, 77};
    std::array<std::byte, 64> out{};
    const auto encoded = encode_gateway_refusal(refusal, out);
    require(encoded.error == NodeProtocolError::none && encoded.bytes_written == 54, "refusal is not 54 bytes");
    require(out[0] == std::byte{4} && out[41] == std::byte{68}, "refusal kind/reason offsets");
    require(le64(out, 46) == 77, "refusal term not at offset 46");
    GatewayRefusal decoded;
    require(decode_gateway_refusal(std::span(out).first(54), decoded) == NodeProtocolError::none && decoded == refusal,
            "refusal did not round trip");
    every_prefix_is_truncated(std::span<const std::byte>(out).first(54), [](auto bytes) {
        GatewayRefusal ignored;
        return decode_gateway_refusal(bytes, ignored);
    }, "refusal");
    for (const std::uint8_t reason : {0, 1, 10, 63, 70, 255}) {
        auto copy = out;
        copy[41] = static_cast<std::byte>(reason);
        require(decode_gateway_refusal(std::span(copy).first(54), decoded) == NodeProtocolError::invalid_reason,
                "reason " + std::to_string(reason) + " accepted");
        require(encode_gateway_refusal({{}, reason, 0, 0}, copy).error == NodeProtocolError::invalid_reason,
                "encoder accepted reason " + std::to_string(reason));
    }
    NodeResponse legacy;
    for (const std::uint8_t kind : {4, 5, 6}) {
        auto copy = out;
        copy[0] = static_cast<std::byte>(kind);
        require(decode_response(std::span(copy).first(54), legacy) == NodeProtocolError::invalid_kind,
                "legacy decode_response accepted gateway kind " + std::to_string(kind));
    }
}

void test_state_chunk_geometry() {
    StateChunkHeader header{{8, digest(1)}, 2, 2, 5, 99, digest(2), 30'000, digest(3), 1, 2};
    const Bytes tail(30'000 - kStateChunkPayloadBytes, std::byte{0x5a});
    Bytes out(kMaxStateChunkResponseBytes);
    const auto encoded = encode_state_chunk(header, tail, out);
    require(encoded.error == NodeProtocolError::none && encoded.bytes_written == 141 + tail.size(),
            "final chunk size");
    require(out[0] == std::byte{5} && le64(out, 57) == 99, "applied index not at offset 57");
    StateChunkView view;
    require(decode_state_chunk(std::span(out).first(encoded.bytes_written), view) == NodeProtocolError::none &&
                view.header == header && view.chunk.size() == tail.size() && view.chunk[0] == std::byte{0x5a},
            "chunk did not round trip");
    for (std::size_t n = 0; n < 141; ++n)
        require(decode_state_chunk(std::span(out).first(n), view) != NodeProtocolError::none,
                "chunk header prefix " + std::to_string(n) + " accepted");
    require(decode_state_chunk(std::span(out).first(encoded.bytes_written - 1), view) ==
                NodeProtocolError::length_mismatch, "short chunk body accepted");

    const Bytes full(kStateChunkPayloadBytes, std::byte{1});
    auto bad = header;
    bad.chunk_index = 0;
    require(encode_state_chunk(bad, tail, out).error == NodeProtocolError::invalid_geometry, "short first chunk");
    bad = header;
    bad.chunk_count = 3;
    require(encode_state_chunk(bad, tail, out).error == NodeProtocolError::invalid_geometry, "wrong chunk count");
    bad = header;
    bad.chunk_index = 2;
    require(encode_state_chunk(bad, tail, out).error == NodeProtocolError::invalid_geometry, "index past count");
    bad = header;
    bad.snapshot_bytes = static_cast<std::uint32_t>(kMaxStateSnapshotBytes + 1);
    bad.chunk_count = 3;
    bad.chunk_index = 0;
    require(encode_state_chunk(bad, full, out).error == NodeProtocolError::invalid_geometry, "oversize snapshot");
    auto forged = Bytes(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written));
    require(encode_state_chunk(header, tail, forged).error == NodeProtocolError::none, "re-encode");
    forged[135] = std::byte{9};
    require(decode_state_chunk(forged, view) == NodeProtocolError::invalid_geometry, "forged count accepted");
}

AuthorityProofView proof_view() {
    AuthorityProofView view;
    view.request = barrier(3);
    auto& effect = view.binding.effect;
    effect.effect_id = EffectId{11};
    effect.cluster_order_id = ClusterOrderId{12};
    effect.committed_directive_index = 13;
    effect.executor_generation = ExecutorGeneration{3};
    effect.kill_epoch = KillEpoch{2};
    effect.venue_id = VenueId{9};
    effect.account_id = AccountId{5};
    effect.instrument_id = InstrumentId{4};
    effect.operation = EffectOperation::submit;
    effect.side = Side::buy;
    effect.price = PriceTicks::from_atoms(11);
    effect.quantity = QuantityLots::from_atoms(2);
    effect.time_in_force = TimeInForce::good_til_cancel;
    effect.post_only = PostOnlyMode::required;
    effect.policy_id = PolicyId{10};
    effect.policy_version = PolicyVersion{1};
    effect.decision_id = DecisionId{20};
    effect.risk_version = RiskVersion{1};
    effect.source_generation = SourceGeneration{1};
    effect.source_sequence = SourceSequence{1};
    effect.payload_digest = digest(0x21);
    view.binding.catalog_root = digest(0x31);
    view.binding.instrument_spec_version = InstrumentSpecVersion{7};
    view.binding.venue_capabilities_version = VenueCapabilitiesVersion{8};
    view.issuer_id = 2;
    view.term = 4;
    view.barrier_index = 30;
    view.applied_index = 30;
    view.state_version = 17;
    view.strategy_mode = StrategyMode::normal;
    return view;
}

bool same(const AuthorityProofView& a, const AuthorityProofView& b) {
    return a.request == b.request && a.binding == b.binding && a.issuer_id == b.issuer_id && a.term == b.term &&
           a.barrier_index == b.barrier_index && a.applied_index == b.applied_index &&
           a.state_version == b.state_version && a.strategy_mode == b.strategy_mode;
}

void test_authority_granted_bytes() {
    const auto view = proof_view();
    std::array<std::byte, kResponseAuthorityGrantedBytes> out{};
    const ResponseCorrelation correlation{9, digest(0x55)};
    const auto encoded = encode_authority_granted(correlation, view, out);
    require(encoded.error == NodeProtocolError::none && encoded.bytes_written == 478, "grant is not 478 bytes");
    require(out[0] == std::byte{6} && out[477] == std::byte{1}, "grant kind / strategy mode offsets");
    require(le64(out, 445) == 4 && le64(out, 453) == 30 && le64(out, 469) == 17, "grant coordinate offsets");
    const auto effect = encode_payload(view.binding.effect);
    require(effect.error == PayloadError::none && std::equal(effect.bytes.begin(), effect.bytes.end(), out.begin() + 227),
            "OrderEffect payload not verbatim at offset 227");
    ResponseCorrelation decoded_correlation;
    AuthorityProofView decoded;
    require(decode_authority_granted(out, decoded_correlation, decoded) == NodeProtocolError::none &&
                decoded_correlation == correlation && same(decoded, view), "grant did not round trip");
    every_prefix_is_truncated(std::span<const std::byte>(out), [](auto bytes) {
        ResponseCorrelation c;
        AuthorityProofView v;
        return decode_authority_granted(bytes, c, v);
    }, "grant");
    auto bad = out;
    for (const std::uint8_t mode : {std::uint8_t{0}, std::uint8_t{7}}) {
        bad[477] = std::byte{mode};
        require(decode_authority_granted(bad, decoded_correlation, decoded) == NodeProtocolError::invalid_value,
                "strategy mode " + std::to_string(mode) + " accepted");
    }
    bad[477] = std::byte{6};
    require(decode_authority_granted(bad, decoded_correlation, decoded) == NodeProtocolError::none &&
                decoded.strategy_mode == StrategyMode::flatten,
            "the flatten mode was refused in a grant");
    bad = out;
    bad[41] ^= std::byte{0xff};
    require(decode_authority_granted(bad, decoded_correlation, decoded) == NodeProtocolError::invalid_value,
            "corrupt barrier frame accepted");
    bad = out;
    bad[300] ^= std::byte{0x01};
    const auto changed = decode_authority_granted(bad, decoded_correlation, decoded);
    require(changed != NodeProtocolError::none || !same(decoded, view), "a flipped effect byte decoded unchanged");
}

void test_metrics_start_at_zero_with_every_label() {
    GatewayMetrics metrics;
    const auto text = render_gateway_metrics(metrics);
    for (const std::string line :
         {"velocita_gateway_up 1", "velocita_gateway_new_risk_stopped 1",
          "velocita_gateway_orders_sent_total{side=\"buy\"} 0", "velocita_gateway_orders_sent_total{side=\"sell\"} 0",
          "velocita_gateway_orders_sent_total{side=\"cancel\"} 0", "velocita_gateway_fills_total{side=\"buy\"} 0",
          "velocita_gateway_fills_total{side=\"sell\"} 0", "velocita_gateway_rejects_total{reason=\"business_rejected\"} 0",
          "velocita_gateway_rejects_total{reason=\"pre_send_failure\"} 0",
          "velocita_gateway_rejects_total{reason=\"rate_limited\"} 0",
          "velocita_gateway_rejects_total{reason=\"dispatch_refused\"} 0",
          "velocita_gateway_rejects_total{reason=\"venue_rejected\"} 0",
          "velocita_gateway_rejects_total{reason=\"kernel_refused\"} 0",
          "velocita_gateway_rejects_total{reason=\"reconciliation_conflict\"} 0",
          "velocita_gateway_rejects_total{reason=\"reconciliation_exhausted\"} 0",
          "velocita_gateway_unknown_outcomes_total 0",
          "velocita_gateway_authority_requests_total{result=\"granted\"} 0",
          "velocita_gateway_authority_requests_total{result=\"refused\"} 0",
          "velocita_gateway_authority_requests_total{result=\"timed_out\"} 0",
          "velocita_gateway_authority_requests_total{result=\"rejected\"} 0",
          "velocita_gateway_stage_ms_bucket{stage=\"authorized_to_proof\",le=\"+Inf\"} 0",
          "velocita_gateway_stage_ms_count{stage=\"fill_to_committed\"} 0",
          "velocita_gateway_fees_usd_total 0.000000", "velocita_gateway_last_fill_timestamp_seconds 0.000000",
          "velocita_gateway_volume_usd_total{side=\"buy\"} 0.000000",
          "velocita_gateway_volume_usd_total{side=\"sell\"} 0.000000",
          "velocita_gateway_heartbeat_timestamp_seconds 0.000000"})
        require(text.find(line + "\n") != std::string::npos, "metrics page lacks: " + line);
    require(text.find("decision_to_authorized") == std::string::npos, "an unmeasured stage was exported");

    metrics.stages[1].observe(3.0);
    metrics.fee_micros_total = 1'234'567;
    const auto after = render_gateway_metrics(metrics);
    require(after.find("velocita_gateway_stage_ms_bucket{stage=\"proof_to_sent\",le=\"2\"} 0\n") != std::string::npos &&
                after.find("velocita_gateway_stage_ms_bucket{stage=\"proof_to_sent\",le=\"5\"} 1\n") != std::string::npos &&
                after.find("velocita_gateway_stage_ms_sum{stage=\"proof_to_sent\"} 3.000000\n") != std::string::npos,
            "histogram buckets are not cumulative at the right boundaries");
    metrics.volume_micros_sell = 21'000'001;
    const auto sold = render_gateway_metrics(metrics);
    require(after.find("velocita_gateway_fees_usd_total 1.234567\n") != std::string::npos, "fee micros to USD");
    require(sold.find("velocita_gateway_volume_usd_total{side=\"sell\"} 21.000001\n") != std::string::npos &&
                sold.find("velocita_gateway_volume_usd_total{side=\"buy\"} 0.000000\n") != std::string::npos,
            "volume micros to USD by side");
}

const std::string kConfig =
    "node_id = 1\n"
    "data_root = /var/lib/velocita/node-1\n"
    "startup = recover\n"
    "cluster.incarnation = ab5a0000000000000000000000000001\n"
    "catalog.root = 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n"
    "catalog.file = /etc/velocita/catalog.bin\n"
    "deployment_mode = laboratory\n"
    "voter.1.endpoint = 10.0.0.1:40200\n"
    "ingress.endpoint = 10.0.0.1:40300\n"
    "caller.11.caller_id = 9\n"
    "caller.11.key_file = /etc/velocita/keys/caller-11.key\n"
    "caller.11.response_endpoint = 10.0.0.50:40400\n"
    "caller.13.caller_id = 22\n"
    "caller.13.key_file = /etc/velocita/keys/caller-13.key\n"
    "caller.13.response_endpoint = 10.0.0.50:40402\n"
    "history.max_wal_bytes = 1073741824\n";

void test_gateway_caller_config() {
    const auto absent = parse_node_config(kConfig);
    require(absent.config && !absent.config->gateway_caller, "gateway.caller should default to absent");
    const auto present = parse_node_config(kConfig + "gateway.caller = 22\n");
    require(present.config && present.config->gateway_caller == std::uint64_t{22}, "gateway.caller = 22 not parsed");
    const auto operator_band = parse_node_config(kConfig + "gateway.caller = 9\n");
    require(!operator_band.config && operator_band.error.code == NodeConfigErrorCode::inconsistent &&
                operator_band.error.key == "gateway.caller", "an operator-band gateway caller was accepted");
    const auto unconfigured = parse_node_config(kConfig + "gateway.caller = 23\n");
    require(!unconfigured.config && unconfigured.error.code == NodeConfigErrorCode::inconsistent,
            "a gateway caller with no caller route was accepted");
    const auto zero = parse_node_config(kConfig + "gateway.caller = 0\n");
    require(!zero.config && zero.error.code == NodeConfigErrorCode::invalid_value, "gateway.caller = 0 accepted");
}

}

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"gateway_request_bytes_and_round_trip", test_gateway_request_bytes_and_round_trip},
        {"correlation_binds_the_exact_request_body", test_correlation_binds_the_exact_request_body},
        {"refusal_bytes", test_refusal_bytes},
        {"state_chunk_geometry", test_state_chunk_geometry},
        {"authority_granted_bytes", test_authority_granted_bytes},
        {"metrics_start_at_zero_with_every_label", test_metrics_start_at_zero_with_every_label},
        {"gateway_caller_config", test_gateway_caller_config},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
