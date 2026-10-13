#include "velocita/v2/solana_payload_view.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace velocita::v2::truemarkets;
namespace {
const std::string signer = std::string(31, '1') + '2';

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::vector<std::uint8_t> fixture(bool lookup = false, bool legacy = false) {
    std::vector<std::uint8_t> b{1};
    b.resize(65, 0);
    if (!legacy) b.push_back(128);
    b.insert(b.end(), {1, 0, 1, 2});
    for (std::uint8_t last : {1, 2}) {
        b.insert(b.end(), 31, 0); b.push_back(last);
    }
    b.insert(b.end(), 32, 3);
    b.insert(b.end(), {1, 1, 1, static_cast<std::uint8_t>(lookup ? 2 : 0), 2, 42, 43});
    if (!legacy) b.push_back(lookup ? 1 : 0);
    if (lookup) {
        b.insert(b.end(), 31, 0); b.push_back(4);
        b.insert(b.end(), {1, 0, 0});
    }
    return b;
}

std::string envelope(const std::vector<std::uint8_t>& bytes) {
    constexpr auto digits = "0123456789abcdef";
    std::string hex;
    for (const auto b : bytes) { hex += digits[b >> 4]; hex += digits[b & 15]; }
    return "{ \"organizationId\":\"test-org\",\"parameters\":{\"signWith\":\"" + signer +
        "\",\"type\":\"TRANSACTION_TYPE_SOLANA\",\"unsignedTransaction\":\"" + hex +
        "\"},\"timestampMs\":\"1000\",\"type\":\"ACTIVITY_TYPE_SIGN_TRANSACTION_V2\" }";
}

SolanaParseError parse(const std::string& text, DecodedSolanaPayload& out) {
    return decode_solana_signing_envelope(text, "test-org", signer, out);
}

void worked_offsets_and_owned_bytes() {
    auto text = envelope(fixture(true));
    const auto original = text;
    DecodedSolanaPayload out;
    require(parse(text, out) == SolanaParseError::none, "worked v0 fixture refused");
    require(out.transaction_size == 209 && out.static_keys_offset == 70 &&
            out.static_key_count == 2 && out.blockhash_offset == 134,
            "worked v0 header/key offsets differ");
    require(out.instructions[0].accounts_offset == 169 && out.instructions[0].data_offset == 171 &&
            out.instructions[0].data_size == 2 && out.transaction[171] == 42,
            "worked instruction offset differs");
    require(out.lookup_count == 1 && out.lookups[0].key_offset == 174 &&
            out.lookups[0].writable_offset == 207 && out.lookups[0].writable_count == 1 &&
            out.loaded_key_count == 1 && out.needs_lookup_resolution(),
            "lookup requirement was lost");
    require(out.signer_index == 0 && out.signature_count == 1 && out.nonzero_signatures == 0,
            "worked signer identity differs");
    text.assign(text.size(), 'x');
    auto copied = out;
    out = {};
    require(copied.original_payload() == original && copied.transaction[171] == 42,
            "retained evidence borrowed caller or previous object memory");
    require(parse(envelope(fixture(false, true)), out) == SolanaParseError::none &&
            !out.version_zero && !out.needs_lookup_resolution() && out.transaction_size == 172,
            "legacy worked length differs");
}

void truncated_noncanonical_and_bad_indices_refused() {
    auto bytes = fixture(true);
    DecodedSolanaPayload out;
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        out.timestamp_ms = 123;
        require(parse(envelope({bytes.begin(), bytes.begin() + size}), out) != SolanaParseError::none,
                "truncated transaction accepted");
        require(out == DecodedSolanaPayload{}, "failed parse retained previous evidence");
    }
    auto bad = bytes; bad.push_back(0);
    require(parse(envelope(bad), out) == SolanaParseError::transaction, "trailing transaction byte accepted");
    bad = bytes; bad[0] = 129; bad.insert(bad.begin()+1, 0);
    require(parse(envelope(bad), out) == SolanaParseError::transaction, "overlong compact length accepted");
    bad = bytes; bad[65] = 129;
    require(parse(envelope(bad), out) == SolanaParseError::unsupported_version, "unknown transaction version accepted");
    bad = bytes; bad[169] = 3;
    require(parse(envelope(bad), out) == SolanaParseError::transaction, "out of range loaded-account index accepted");
    bad = bytes; bad[167] = 2;
    require(parse(envelope(bad), out) == SolanaParseError::transaction, "program loaded from lookup accepted");
    bad = bytes; bad[167] = 0;
    require(parse(envelope(bad), out) == SolanaParseError::transaction, "payer used as program accepted");
    bad = bytes; bad[169] = 0; bad[206] = 0; bad.erase(bad.begin()+207);
    require(parse(envelope(bad), out) == SolanaParseError::transaction, "empty lookup table accepted");
    bad = bytes; bad[66] = 2;
    require(parse(envelope(bad), out) == SolanaParseError::transaction, "signature/header mismatch accepted");
    bad = bytes; bad[166] = 65;
    require(parse(envelope(bad), out) == SolanaParseError::capacity, "instruction capacity not enforced");
}

void identity_and_envelope_are_explicit() {
    const auto original = envelope(fixture());
    DecodedSolanaPayload out;
    require(decode_solana_signing_envelope(original, "wrong", signer, out) == SolanaParseError::identity,
            "wrong organization accepted");
    auto bytes = fixture(); bytes[101] = 9;
    require(parse(envelope(bytes), out) == SolanaParseError::identity, "signWith not bound to transaction signer");
    for (const auto addition : {",\"unknown\":true", ",\"type\":\"ACTIVITY_TYPE_SIGN_TRANSACTION_V2\""}) {
        auto text = original; text.insert(text.size()-1, addition);
        require(parse(text, out) == SolanaParseError::envelope, "extra or duplicate envelope field accepted");
    }
    auto nested = original;
    nested.insert(nested.find("},\"timestampMs"), ",\"signWith\":\"" + signer + "\"");
    require(parse(nested, out) == SolanaParseError::envelope, "duplicate nested signing field accepted");
    auto wrong_type = original;
    wrong_type.replace(wrong_type.find("TRANSACTION_TYPE_SOLANA"), 23, "TRANSACTION_TYPE_ETHEREUM");
    require(parse(wrong_type, out) == SolanaParseError::envelope, "wrong chain transaction accepted");
    require(parse(std::string(kMaxSolanaEnvelope+1, 'x'), out) == SolanaParseError::capacity,
            "envelope capacity not enforced");
    require(out == DecodedSolanaPayload{}, "capacity failure retained state");
}
}

int main() {
    try {
        worked_offsets_and_owned_bytes();
        truncated_noncanonical_and_bad_indices_refused();
        identity_and_envelope_are_explicit();
        std::cout << "PASS worked_offsets_and_owned_bytes\n"
                     "PASS truncated_noncanonical_and_bad_indices_refused\n"
                     "PASS identity_and_envelope_are_explicit\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
