#include "velocita/v2/solana_resolved_accounts.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace velocita::v2;
using namespace velocita::v2::truemarkets;
void require(bool value, const char* name) { if (!value) throw std::runtime_error(name); }
SolanaAccountKey key(unsigned value) { SolanaAccountKey k{}; k.back() = static_cast<std::uint8_t>(value); return k; }
Digest256 digest(unsigned value) { Digest256 d; d.bytes[0] = static_cast<std::byte>(value); return d; }
const std::string signer = std::string(31, '1') + '2';
void append_key(std::vector<std::uint8_t>& b, unsigned value) {
    const auto k = key(value); b.insert(b.end(), k.begin(), k.end());
}
std::vector<std::uint8_t> fixture(bool legacy = false) {
    std::vector<std::uint8_t> b{2}; b.resize(129, 0);
    if (!legacy) b.push_back(128);
    b.insert(b.end(), {2, 1, 1, 4});
    for (unsigned k = 1; k <= 4; ++k) append_key(b, k);
    b.insert(b.end(), 32, 7);
    if (legacy) { b.insert(b.end(), {1, 3, 2, 0, 2, 1, 42}); return b; }
    b.insert(b.end(), {1, 3, 6, 0, 4, 5, 6, 7, 8, 1, 42, 2});
    append_key(b, 20); b.insert(b.end(), {2, 2, 0, 1, 1});
    append_key(b, 21); b.insert(b.end(), {1, 1, 1, 0});
    return b;
}
std::string envelope(const std::vector<std::uint8_t>& bytes) {
    constexpr auto digits = "0123456789abcdef";
    std::string hex;
    for (const auto b : bytes) { hex += digits[b >> 4]; hex += digits[b & 15]; }
    return "{\"organizationId\":\"test-org\",\"parameters\":{\"signWith\":\"" + signer +
           "\",\"type\":\"TRANSACTION_TYPE_SOLANA\",\"unsignedTransaction\":\"" + hex +
           "\"},\"timestampMs\":\"1000\",\"type\":\"ACTIVITY_TYPE_SIGN_TRANSACTION_V2\"}";
}
DecodedSolanaPayload decode(const std::vector<std::uint8_t>& bytes) {
    DecodedSolanaPayload p;
    require(decode_solana_signing_envelope(envelope(bytes), "test-org", signer, p) == SolanaParseError::none,
            "independent fixture decodes");
    return p;
}
struct Rig {
    DecodedSolanaPayload decoded{decode(fixture())};
    std::array<SolanaLookupEvidence, 2> evidence{};
    ResolvedSolanaAccounts out{};
    Rig() {
        evidence[0].table = key(20); evidence[0].observed_slot = 100;
        evidence[0].evidence_digest = digest(50); evidence[0].address_count = 3;
        evidence[0].addresses[0] = key(10); evidence[0].addresses[1] = key(11); evidence[0].addresses[2] = key(12);
        evidence[1].table = key(21); evidence[1].observed_slot = 101;
        evidence[1].evidence_digest = digest(51); evidence[1].address_count = 2;
        evidence[1].addresses[0] = key(13); evidence[1].addresses[1] = key(14);
    }
    SolanaResolutionError resolve() { return resolve_solana_accounts(decoded, "test-org", signer, evidence, out); }
};
void account_order_and_privileges() {
    Rig r;
    require(r.decoded.transaction_size == 379 && r.decoded.static_keys_offset == 134 &&
            r.decoded.lookups[0].key_offset == 306 && r.decoded.lookups[1].key_offset == 343,
            "hand counted transaction and lookup offsets");
    require(r.resolve() == SolanaResolutionError::none, "valid lookup fixture resolves");
    require(r.out.account_count == 9 && r.out.lookup_count == 2, "four static and five loaded accounts");
    const unsigned order[]{1, 2, 3, 4, 12, 10, 14, 11, 13};
    for (unsigned i = 0; i < 9; ++i) {
        require(r.out.accounts[i].key == key(order[i]), "all writable lookups precede every readonly lookup");
        require(r.out.accounts[i].signer == (i < 2), "only required static accounts are signers");
        require(r.out.accounts[i].writable == (i == 0 || i == 2 || (i >= 4 && i < 7)),
                "header and loaded privileges match independent oracle");
    }
    require(r.out.lookups[0].table == key(20) && r.out.lookups[1].table == key(21) &&
            r.out.lookups[0].observed_slot == 100 && r.out.lookups[1].observed_slot == 101 &&
            r.out.lookups[0].evidence_digest == digest(50) && r.out.lookups[1].evidence_digest == digest(51) &&
            r.out.lookups[0].address_count == 3 && r.out.lookups[1].address_count == 2,
            "table identities and evidence provenance copied in transaction order");
    const auto original = r.out;
    std::swap(r.evidence[0], r.evidence[1]);
    require(r.resolve() == SolanaResolutionError::none && r.out == original, "evidence input order does not alter account order");
}
void owned_digests_and_legacy() {
    Rig r;
    require(r.resolve() == SolanaResolutionError::none, "digest fixture resolves");
    require(to_hex(r.out.transaction_digest) == "9913a5ca50b7ae8c5b667b22e23f988076836f1be0ff3f827fc0a4a276a1e2dc", "transaction digest matches independent SHA256 oracle");
    require(to_hex(r.out.lookups[0].addresses_digest) == "f36c5ee45ae6605f3dfe3ecd1536101ad170ed2ef9259a27e33df0d09f950865", "address image matches independent SHA256 oracle");
    const auto retained = r.out;
    r.decoded = {}; r.evidence = {};
    require(retained.accounts[4].key == key(12) && retained.lookups[0].observed_slot == 100,
            "resolved value owns keys and evidence after input destruction");
    r.decoded = decode(fixture(true));
    require(resolve_solana_accounts(r.decoded, "test-org", signer, {}, r.out) == SolanaResolutionError::none &&
            r.out.account_count == 4 && r.out.lookup_count == 0 && r.out.accounts[1].signer && !r.out.accounts[1].writable,
            "legacy transaction retains readonly signer without lookup evidence");
}
void forged_decoded_values_are_refused() {
    const Rig original;
    for (unsigned which = 0; which < 13; ++which) {
        auto r = original;
        switch (which) {
        case 0: r.decoded.static_keys_offset = 65535; break;
        case 1: r.decoded.static_key_count = 256; break;
        case 2: r.decoded.lookups[0].writable_offset = 65535; break;
        case 3: r.decoded.lookup_count = 33; break;
        case 4: r.decoded.instructions[0].accounts_offset = 65535; break;
        case 5: r.decoded.transaction[132] = 0; break;
        case 6: r.decoded.signature_count = 12; break;
        case 7: r.decoded.required_signatures = 1; break;
        case 8: r.decoded.loaded_key_count = 0; break;
        case 9: r.decoded.version_zero = false; break;
        case 10: r.decoded.transaction_size = 65535; break;
        case 11: r.decoded.original_size = 65535; break;
        case 12: r.decoded.signer_index = 2; break;
        }
        r.out.account_count = 1;
        require(r.resolve() != SolanaResolutionError::none && r.out == ResolvedSolanaAccounts{},
                "forged decoded metadata cannot produce resolution");
    }
    auto r = original;
    require(resolve_solana_accounts(r.decoded, "wrong-org", signer, r.evidence, r.out) == SolanaResolutionError::invalid_payload,
            "independent expected envelope identity required");
}
void lookup_evidence_failures() {
    Rig r;
    require(resolve_solana_accounts(r.decoded, "test-org", signer, {r.evidence.data(), 1}, r.out) ==
            SolanaResolutionError::evidence_count, "missing lookup evidence refused");
    std::array<SolanaLookupEvidence, 3> extra{r.evidence[0], r.evidence[1], r.evidence[0]};
    require(resolve_solana_accounts(r.decoded, "test-org", signer, extra, r.out) == SolanaResolutionError::evidence_count,
            "extra lookup evidence refused");
    r.evidence[1].table = key(22);
    require(r.resolve() == SolanaResolutionError::table_mismatch, "wrong lookup table refused");
    r.evidence[1] = r.evidence[0];
    require(r.resolve() == SolanaResolutionError::duplicate_table, "duplicate evidence table refused");
    r = Rig{}; r.evidence[0].observed_slot = 0;
    require(r.resolve() == SolanaResolutionError::invalid_evidence, "missing evidence slot refused");
    r = Rig{}; r.evidence[0].evidence_digest = {};
    require(r.resolve() == SolanaResolutionError::invalid_evidence, "missing evidence hash refused");
    r = Rig{}; r.evidence[0].address_count = 257;
    require(r.resolve() == SolanaResolutionError::capacity, "oversized lookup address image refused");
    r = Rig{}; r.evidence[0].address_count = 2;
    require(r.resolve() == SolanaResolutionError::lookup_index, "out of range table address index refused");
    r = Rig{}; r.evidence[0].address_count = 0;
    require(r.resolve() == SolanaResolutionError::invalid_evidence, "empty table evidence refused");
}
void aliases_and_failure_atomicity() {
    Rig r;
    require(r.resolve() == SolanaResolutionError::none, "prior good resolution exists");
    r.evidence[1].addresses[1] = key(1);
    require(r.resolve() == SolanaResolutionError::account_alias && r.out == ResolvedSolanaAccounts{},
            "loaded signer alias refused and old success cleared");
    r = Rig{}; r.evidence[1].addresses[0] = key(12);
    require(r.resolve() == SolanaResolutionError::account_alias, "readonly writable alias across tables refused");
    auto bytes = fixture(); bytes[340] = 2;
    r = Rig{}; r.decoded = decode(bytes);
    require(r.resolve() == SolanaResolutionError::account_alias, "duplicate selected lookup index refused");
    bytes = fixture(); std::copy(bytes.begin() + 306, bytes.begin() + 338, bytes.begin() + 343);
    r = Rig{}; r.decoded = decode(bytes);
    require(r.resolve() == SolanaResolutionError::duplicate_table, "duplicate transaction lookup table refused");
}
void maximum_accounts_and_zero_system_key() {
    auto bytes = fixture();
    std::fill(bytes.begin() + 230, bytes.begin() + 262, 0);
    Rig r; r.decoded = decode(bytes);
    require(r.resolve() == SolanaResolutionError::none && r.out.accounts[3].key == SolanaAccountKey{},
            "all zero System Program key is valid account identity");
    bytes = fixture(); bytes.resize(294);
    bytes.insert(bytes.end(), {1, 3, 1, 255, 1, 42, 1});
    append_key(bytes, 20); bytes.insert(bytes.end(), {252, 1});
    for (unsigned i = 0; i < 252; ++i) bytes.push_back(static_cast<std::uint8_t>(i));
    bytes.push_back(0);
    r.decoded = decode(bytes);
    auto image = r.evidence[0]; image.address_count = 256;
    for (unsigned i = 0; i < 256; ++i) { image.addresses[i] = key(i); image.addresses[i][30] = 1; }
    require(resolve_solana_accounts(r.decoded, "test-org", signer, {&image, 1}, r.out) == SolanaResolutionError::none &&
            r.out.account_count == 256 && r.out.accounts[255].key == image.addresses[251] && r.out.accounts[255].writable,
            "full 256 account capacity and unsigned index 255 resolve exactly");
    std::vector<SolanaLookupEvidence> oversized(33);
    require(resolve_solana_accounts(r.decoded, "test-org", signer, oversized, r.out) == SolanaResolutionError::capacity &&
            r.out == ResolvedSolanaAccounts{}, "evidence count capacity checked before consuming entries");
}
}
int main(int argc, char** argv) {
    const std::pair<const char*, void (*)()> cases[] = {
        {"account_order_and_privileges", account_order_and_privileges},
        {"owned_digests_and_legacy", owned_digests_and_legacy},
        {"forged_decoded_values_are_refused", forged_decoded_values_are_refused},
        {"lookup_evidence_failures", lookup_evidence_failures},
        {"aliases_and_failure_atomicity", aliases_and_failure_atomicity},
        {"maximum_accounts_and_zero_system_key", maximum_accounts_and_zero_system_key}};
    unsigned ran = 0, failed = 0;
    for (const auto& [name, test] : cases) {
        if (argc == 2 && std::string_view(argv[1]) != name) continue;
        ++ran;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    return ran && !failed ? 0 : 1;
}
