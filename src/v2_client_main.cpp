// velocita-v2-client: operator commands for the cluster.
#include "velocita/aeron_runtime.hpp"
#include "velocita/v2/aeron_node_transport.hpp"
#include "velocita/v2/node_files.hpp"
#include "velocita/v2/node_client.hpp"
#include "velocita/v2/node_protocol.hpp"
#include "velocita/v2/session_fence.hpp"
#include "velocita/v2/shadow_submitter.hpp"
#include "velocita/v2/swap_kernel.hpp"
#include <fstream>

#include "FragmentAssembler.h"

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <chrono>
#include <deque>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

using namespace velocita::v2;
using Clock = std::chrono::steady_clock;

struct Usage final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::uint64_t number(const std::string& text, const char* what) {
    try {
        std::size_t used = 0;
        const auto value = std::stoull(text, &used);
        if (used != text.size()) throw Usage(std::string("invalid ") + what);
        return value;
    } catch (const Usage&) {
        throw;
    } catch (...) {
        throw Usage(std::string("invalid ") + what);
    }
}

Endpoint endpoint(const std::string& text) {
    const auto colon = text.rfind(':');
    if (colon == std::string::npos) throw Usage("endpoint needs HOST:PORT: " + text);
    const auto port = number(text.substr(colon + 1), "port");
    if (port == 0 || port > 65535) throw Usage("invalid port: " + text);
    return {text.substr(0, colon), static_cast<std::uint16_t>(port)};
}

struct Arguments {
    std::int32_t caller_key_id{};
    std::uint64_t caller_id{};
    std::filesystem::path key_file, session_file, aeron_dir;
    bool key_stdin{};
    bool dry_run{};
    std::string summary;
    std::array<std::byte, 16> incarnation{};
    Endpoint response_endpoint;
    std::map<std::int32_t, Endpoint> nodes;
    std::vector<std::int32_t> order;
    std::uint64_t sequence{};
    std::uint64_t timeout_ms{10000};
    std::uint64_t kill_epoch{};
    CommandBody body{};
    ProposeLane lane{ProposeLane::control};
    Digest256 digest{};
};

Digest256 payload_digest(std::uint64_t kill_epoch) {
    std::string domain = "velocita.v2-client.advance-kill-epoch";
    std::vector<std::byte> bytes;
    for (const char c : domain) bytes.push_back(static_cast<std::byte>(c));
    for (int b = 0; b < 8; ++b) bytes.push_back(static_cast<std::byte>((kill_epoch >> (8 * b)) & 0xff));
    return sha256_digest(bytes);
}

Digest256 verb_digest(const std::string& verb, std::initializer_list<std::string> args) {
    std::string text = "velocita.v2-client." + verb;
    for (const auto& arg : args) text += " " + arg;
    std::vector<std::byte> bytes;
    for (const char c : text) bytes.push_back(static_cast<std::byte>(c));
    return sha256_digest(bytes);
}

swap::Record read_swap_record(const char* path) {
    std::ifstream input(path, std::ios::binary);
    std::array<std::byte, swap::max_record_bytes + 1> bytes{};
    if (!input) throw Usage("cannot read swap record");
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    const auto count = input.gcount();
    if (input.bad() || count <= 0 || static_cast<std::size_t>(count) > swap::max_record_bytes)
        throw Usage("swap record must be one bounded canonical record");
    swap::Record record;
    const auto original = std::span(bytes).first(static_cast<std::size_t>(count));
    if (swap::decode(original, record) != swap::CodecError::none)
        throw Usage("swap record must be one bounded canonical record");
    std::array<std::byte, swap::max_record_bytes> canonical{};
    const auto encoded = swap::encode(record, canonical);
    if (encoded.error != swap::CodecError::none || encoded.size != original.size() ||
        !std::equal(original.begin(), original.end(), canonical.begin()))
        throw Usage("swap record must be one bounded canonical record");
    return record;
}

Arguments parse(int argc, char** argv) {
    Arguments a;
    bool have_incarnation = false;
    int i = 1;
    for (; i < argc; ++i) {
        const std::string key = argv[i];
        if (key.rfind("--", 0) != 0) break;
        if (key == "--key-stdin") {
            a.key_stdin = true;
            continue;
        }
        if (key == "--dry-run") {
            a.dry_run = true;
            continue;
        }
        if (i + 1 >= argc) throw Usage("missing value for " + key);
        const std::string value = argv[++i];
        if (key == "--caller-key-id") a.caller_key_id = static_cast<std::int32_t>(number(value, "caller key id"));
        else if (key == "--caller-id") a.caller_id = number(value, "caller id");
        else if (key == "--key-file") a.key_file = value;
        else if (key == "--session-file") a.session_file = value;
        else if (key == "--aeron-dir") a.aeron_dir = value;
        else if (key == "--response-endpoint") a.response_endpoint = endpoint(value);
        else if (key == "--sequence") a.sequence = number(value, "sequence");
        else if (key == "--timeout-ms") a.timeout_ms = number(value, "timeout");
        else if (key == "--node") {
            const auto eq = value.find('=');
            if (eq == std::string::npos) throw Usage("--node needs ID=HOST:PORT");
            const auto id = static_cast<std::int32_t>(number(value.substr(0, eq), "node id"));
            if (!a.nodes.emplace(id, endpoint(value.substr(eq + 1))).second) throw Usage("duplicate --node id");
            a.order.push_back(id);
        } else if (key == "--incarnation") {
            if (value.size() != 32) throw Usage("incarnation must be 32 lowercase hex digits");
            for (std::size_t b = 0; b < 16; ++b)
                a.incarnation[b] = static_cast<std::byte>(std::stoi(value.substr(2 * b, 2), nullptr, 16));
            have_incarnation = true;
        } else {
            throw Usage("unknown argument " + key);
        }
    }
    if (i >= argc) throw Usage("missing command");
    const std::string verb = argv[i];
    if (verb == "spot-swap-record" && (i + 2 == argc || i + 3 == argc)) {
        const auto record = read_swap_record(argv[i+1]);
        if ((!std::holds_alternative<swap::Initialize>(record.command) && !std::holds_alternative<swap::Intent>(record.command) &&
             !std::holds_alternative<swap::AmendBuyPrice>(record.command) && !std::holds_alternative<swap::GrantStrategy>(record.command)) || record.role != swap::required_role(record.command))
            throw Usage("owner swap record must initialize funding, admit an explicit intent, amend the buy price or grant strategy authority with its required role");
        auto binding = std::visit([](const auto& value) -> swap::Binding {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T,swap::Initialize> || std::is_same_v<T,swap::Intent>) return value.binding;
            else return {};
        }, record.command);
        if (const auto* amendment = std::get_if<swap::AmendBuyPrice>(&record.command)) {
            if (i + 3 != argc) throw Usage("price amendment requires its original opening record");
            const auto original = read_swap_record(argv[i+2]);
            const auto* opening = std::get_if<swap::Initialize>(&original.command);
            if (!opening || original.role != swap::Role::operator_role)
                throw Usage("price amendment opening must be an operator Initialize record");
            if (amendment->opening_digest != swap::opening_digest(*opening) ||
                amendment->expected_price != opening->limits.max_buy_price_per_sol)
                throw Usage("price amendment differs from supplied original opening");
            binding = opening->binding;
            a.summary = "SWAP BUY PRICE AMENDMENT expected_price=" + std::to_string(amendment->expected_price.value()) +
                " replacement_price=" + std::to_string(amendment->replacement_price.value()) +
                " opening_digest=" + to_hex(amendment->opening_digest) + " review_evidence=" + to_hex(amendment->review_evidence) +
                " expected_revision=" + std::to_string(amendment->expected_revision) + "\n";
        } else if (const auto* grant = std::get_if<swap::GrantStrategy>(&record.command)) {
            if (i + 3 != argc) throw Usage("strategy grant requires its original opening record");
            const auto original=read_swap_record(argv[i+2]);
            const auto* opening=std::get_if<swap::Initialize>(&original.command);
            if (!opening || original.role!=swap::Role::operator_role || grant->opening_digest!=swap::opening_digest(*opening))
                throw Usage("strategy grant differs from supplied original opening");
            if (grant->caller.value()<=kMaxOperatorCallerId || !grant->first_sequence.value() || grant->review_evidence.is_zero() ||
                !grant->kill_epoch || !grant->executor_generation || !record.event_time)
                throw Usage("strategy grant needs a strategy caller, sequence floor, evidence, epochs and event time");
            binding=opening->binding;
            a.summary="SWAP STRATEGY GRANT caller="+std::to_string(grant->caller.value())+
                " first_sequence="+std::to_string(grant->first_sequence.value())+
                " kill_epoch="+std::to_string(grant->kill_epoch)+" executor_generation="+std::to_string(grant->executor_generation)+
                " opening_digest="+to_hex(grant->opening_digest)+" review_evidence="+to_hex(grant->review_evidence)+"\n";
        } else if (i + 2 != argc) {
            throw Usage("Initialize and Intent records do not take an opening argument");
        }
        if (!have_incarnation || binding.incarnation != sha256_digest(a.incarnation))
            throw Usage("swap record is bound to another incarnation");
        if (const auto* o = std::get_if<swap::Initialize>(&record.command)) {
            a.summary = "SWAP FUNDING base_atoms=" + std::to_string(o->base.value()) +
                " quote_atoms=" + std::to_string(o->quote.value()) + " purchase_debit_cap=" +
                std::to_string(o->limits.max_purchase_total.value()) + " realized_loss_cap=" +
                std::to_string(o->limits.max_realized_loss.value()) + "\n";
        } else if (const auto* value = std::get_if<swap::Intent>(&record.command)) {
            const auto& intent = *value;
            a.summary = "SWAP INTENT side=" + std::to_string(static_cast<unsigned>(intent.side)) +
                " effect=" + std::to_string(intent.effect.value()) + " max_quote_debit=" +
                std::to_string(intent.max_quote_debit.value()) + " base_input=" + std::to_string(intent.base_input.value()) +
                " min_base_receive=" + std::to_string(intent.min_base_receive.value()) +
                " min_quote_receive=" + std::to_string(intent.min_quote_receive.value()) + "\n";
        }
        a.body = ApplySpotSwap{record}; a.digest = swap_record_digest(record); a.lane = ProposeLane::normal;
    } else if (verb == "spot-swap-intent" && i + 11 == argc) {
        const auto original = read_swap_record(argv[i+1]);
        const auto* opening = std::get_if<swap::Initialize>(&original.command);
        if (!opening || original.role != swap::Role::operator_role)
            throw Usage("strategy intent needs the operator opening record");
        if (!have_incarnation || opening->binding.incarnation != sha256_digest(a.incarnation))
            throw Usage("opening record is bound to another incarnation");
        const std::string_view side = argv[i+2];
        const EffectId effect{number(argv[i+3], "effect")};
        const auto amount = number(argv[i+4], "amount"), receive = number(argv[i+5], "minimum receive"), fee = number(argv[i+6], "fee cap");
        const auto source = number(argv[i+7], "source time"), sign_by = number(argv[i+8], "sign by");
        const auto kill = number(argv[i+9], "kill epoch"), generation = number(argv[i+10], "executor generation");
        std::optional<swap::Intent> intent;
        if (side == "buy") {
            intent = swap::strategy_buy(opening->binding, effect, swap::QuoteAtoms{amount}, swap::BaseAtoms{receive}, source, sign_by, kill, generation);
            if (intent && intent->fee_cap.value() != fee) throw Usage("buy fee cap must equal 0.2% of the whole-cent debit");
        } else if (side == "sell") {
            intent = swap::strategy_sell(opening->binding, effect, swap::BaseAtoms{amount}, swap::QuoteAtoms{receive}, swap::QuoteAtoms{fee}, source, sign_by, kill, generation);
        } else throw Usage("strategy intent side must be buy or sell");
        if (!intent) throw Usage("strategy intent amounts or times refused");
        const swap::Record record{swap::Role::strategy, source, *intent};
        a.summary = "STRATEGY SWAP INTENT side=" + std::string(side) + " effect=" + std::to_string(effect.value()) +
            " amount=" + std::to_string(amount) + " min_receive=" + std::to_string(receive) + " fee_cap=" + std::to_string(intent->fee_cap.value()) + "\n";
        a.body = ApplySpotSwap{record}; a.digest = swap_record_digest(record); a.lane = ProposeLane::normal;
    } else if (verb == "kill-epoch" && i + 2 == argc) {
        a.kill_epoch = number(argv[i + 1], "kill epoch");
        a.body = AdvanceKillEpoch{KillEpoch{a.kill_epoch}};
        a.digest = payload_digest(a.kill_epoch);
    } else if (verb == "executor-generation" && i + 2 == argc) {
        const auto generation = number(argv[i + 1], "executor generation");
        if (generation == 0) throw Usage("executor generation must be positive");
        a.body = SetExecutorGeneration{ExecutorGeneration{generation}};
        a.digest = verb_digest(verb, {argv[i + 1]});
    } else if (verb == "install-catalog" && i + 4 == argc) {
        InstallExecutionCatalog install;
        const std::string root = argv[i + 1];
        if (root.size() != 64) throw Usage("catalog root must be 64 hex digits");
        for (std::size_t b = 0; b < 32; ++b) {
            unsigned v = 0;
            const auto r = std::from_chars(root.data() + 2 * b, root.data() + 2 * b + 2, v, 16);
            if (r.ec != std::errc{} || r.ptr != root.data() + 2 * b + 2) throw Usage("catalog root must be hex");
            install.catalog_root.bytes[b] = static_cast<std::byte>(v);
        }
        install.expected_kill_epoch = KillEpoch{number(argv[i + 2], "expected kill epoch")};
        install.expected_executor_generation = ExecutorGeneration{number(argv[i + 3], "expected executor generation")};
        a.body = install;
        a.lane = ProposeLane::normal;
        a.digest = verb_digest(verb, {argv[i + 1], argv[i + 2], argv[i + 3]});
    } else if (verb == "resolve-conflict" && i + 8 == argc) {
        ResolveOrderConflict fix;
        fix.cluster_order_id = ClusterOrderId{number(argv[i + 1], "order id")};
        fix.expected_kill_epoch = KillEpoch{number(argv[i + 2], "expected kill epoch")};
        fix.expected_executor_generation = ExecutorGeneration{number(argv[i + 3], "expected executor generation")};
        const auto lots = number(argv[i + 4], "filled lots");
        const auto ticks = number(argv[i + 5], "price ticks");
        const auto fee = number(argv[i + 6], "fee micros");
        constexpr std::uint64_t limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (lots > limit || ticks > limit || fee > limit) throw Usage("a correction value is too large");
        if (lots == 0 ? ticks != 0 || fee != 0 : ticks == 0)
            throw Usage("give a price with a fill, and no price or fee without one");
        fix.unbooked_quantity = QuantityLots::from_atoms(static_cast<std::int64_t>(lots));
        fix.unbooked_price = PriceTicks::from_atoms(static_cast<std::int64_t>(ticks));
        fix.unbooked_fee = FeeMicros::from_atoms(static_cast<std::int64_t>(fee));
        const std::string evidence = argv[i + 7];
        if (evidence.size() != 64) throw Usage("evidence digest must be 64 hex digits");
        for (std::size_t b = 0; b < 32; ++b) {
            unsigned v = 0;
            const auto r = std::from_chars(evidence.data() + 2 * b, evidence.data() + 2 * b + 2, v, 16);
            if (r.ec != std::errc{} || r.ptr != evidence.data() + 2 * b + 2) throw Usage("evidence digest must be hex");
            fix.evidence_digest.bytes[b] = static_cast<std::byte>(v);
        }
        if (fix.cluster_order_id.value() == 0 || fix.evidence_digest.is_zero())
            throw Usage("the order id and the evidence digest must not be zero");
        a.body = fix;
        a.lane = ProposeLane::normal;
        a.digest = verb_digest(verb, {argv[i + 1], argv[i + 2], argv[i + 3], argv[i + 4], argv[i + 5], argv[i + 6],
                                      argv[i + 7]});
        a.summary = std::string("RESOLVE-CONFLICT order ") + argv[i + 1] + ": " +
                    (lots == 0 ? std::string("book nothing")
                               : std::string("book ") + argv[i + 4] + " lot(s) at " + argv[i + 5] + " ticks, fee " +
                                     argv[i + 6] + " micros, with no limit or fee check") +
                    ", release the rest of its reservation and close it\n"
                    "The order must be finished at the venue; evidence sha256 " + evidence + "\n";
    } else if (verb == "assign-role" && i + 3 == argc) {
        const std::string role = argv[i + 2];
        if (role != "strategy" && role != "gateway") throw Usage("role must be strategy or gateway");
        const auto caller = number(argv[i + 1], "caller id");
        if (caller <= kMaxOperatorCallerId) throw Usage("an operator caller cannot be given a role");
        a.body = AssignCallerRole{CallerId{caller}, role == "strategy" ? CallerRole::strategy : CallerRole::gateway};
        a.lane = ProposeLane::normal;
        a.digest = verb_digest(verb, {argv[i + 1], argv[i + 2]});
        a.summary = std::string("ASSIGN-ROLE caller ") + argv[i + 1] + " may act as " + role + " from now on; " +
                    (role == "strategy" ? "it may record market data, evaluate and authorize decisions"
                                        : "it may report venue evidence and request cancels") + "\n";
    } else if (verb == "strategy-mode" && i + 4 == argc) {
        const auto mode = parse_strategy_mode(argv[i + 1]);
        if (!mode) throw Usage("strategy mode must be normal, no_new_risk, cancel_only, reduce_only, flatten or halted");
        a.body = SetStrategyMode{*mode, KillEpoch{number(argv[i + 2], "expected kill epoch")},
                                 ExecutorGeneration{number(argv[i + 3], "expected executor generation")}};
        a.digest = verb_digest(verb, {argv[i + 1], argv[i + 2], argv[i + 3]});
        a.summary = std::string("STRATEGY-MODE ") + argv[i + 1] + " expected kill epoch " + argv[i + 2] +
                    ", executor generation " + argv[i + 3] + "\n";
    } else if (verb == "risk-limits") {
        const std::vector<std::string> words(argv + i + 1, argv + argc);
        const auto parsed = parse_risk_limits_verb(words);
        if (!parsed.limits) throw Usage("risk-limits: " + parsed.error);
        a.body = *parsed.limits;
        a.lane = ProposeLane::normal;
        a.digest = verb_digest(verb, {risk_limits_canonical(*parsed.limits)});
        a.summary = describe_risk_limits(*parsed.limits);
    } else {
        throw Usage("command must be: kill-epoch VALUE | executor-generation VALUE | "
                    "spot-swap-record INITIALIZE_OR_INTENT.record | spot-swap-record AMENDMENT.record ORIGINAL_OPENING.record | "
                    "install-catalog ROOT EXPECTED_KILL_EPOCH EXPECTED_EXECUTOR_GENERATION | "
                    "strategy-mode MODE EXPECTED_KILL_EPOCH EXPECTED_EXECUTOR_GENERATION | risk-limits KEY=VALUE... | "
                    "assign-role CALLER_ID strategy|gateway | "
                    "resolve-conflict ORDER_ID EXPECTED_KILL_EPOCH EXPECTED_EXECUTOR_GENERATION FILLED_LOTS "
                    "PRICE_TICKS FEE_MICROS EVIDENCE_SHA256");
    }
    if (a.dry_run) return a;
    if (a.key_stdin == !a.key_file.empty()) throw Usage("give exactly one of --key-file or --key-stdin");
    if (a.caller_key_id <= 0 || a.caller_id == 0 || a.session_file.empty() ||
        a.aeron_dir.empty() || !have_incarnation || a.response_endpoint.port == 0 || a.nodes.empty() ||
        a.sequence == 0 || (verb == "kill-epoch" && a.kill_epoch == 0))
        throw Usage("missing required argument");
    return a;
}

int run(const Arguments& a) {
    std::cout << a.summary << std::flush;
    if (a.dry_run) {
        std::cout << "DRY-RUN not sent" << std::endl;
        return 0;
    }
    std::vector<AuthSecret> secrets;
    if (a.key_stdin) {
        std::array<std::byte, 2 * kAuthSecretBytes + 1> raw{};
        std::size_t length = 0;
        while (length < raw.size()) {
            const auto got = ::read(STDIN_FILENO, raw.data() + length, raw.size() - length);
            if (got <= 0) break;
            length += static_cast<std::size_t>(got);
        }
        if (length != kAuthSecretBytes && length != 2 * kAuthSecretBytes)
            throw Usage("--key-stdin needs exactly 32 or 64 bytes");
        for (std::size_t at = 0; at < length; at += kAuthSecretBytes)
            secrets.push_back(*AuthSecret::from_bytes(std::span(raw).subspan(at, kAuthSecretBytes)));
        volatile std::byte* cursor = raw.data();
        for (std::size_t i = 0; i < raw.size(); ++i) cursor[i] = std::byte{0};
    } else {
        auto loaded = load_secret_set(a.key_file);
        if (!loaded.secrets) throw Usage("key file refused: " + loaded.failed_file.string());
        secrets.push_back(std::move(loaded.secrets->current));
        if (loaded.secrets->next) secrets.push_back(std::move(*loaded.secrets->next));
        if (loaded.secrets->previous) secrets.push_back(std::move(*loaded.secrets->previous));
    }
    auto epoch = advance_session_epoch_file(a.session_file);
    if (epoch.error == SessionEpochError::missing) epoch = create_session_epoch_file(a.session_file);
    if (epoch.error != SessionEpochError::none) throw Usage("client session epoch refused");

    const Command command{{CallerId{a.caller_id}, RequestSequence{a.sequence}, a.digest}, a.body};

    velocita::AeronRuntime aeron(a.aeron_dir.string());
    AeronClientTransport transport(aeron, a.nodes, a.response_endpoint);
    std::vector<ClientRoute> routes;
    for (const auto id : a.order) {
        auto ingress = AuthKey::derive(secrets.front(), {AuthRole::ingress, a.caller_key_id, id}, a.incarnation);
        if (!ingress) throw Usage("invalid caller or node id");
        std::vector<AuthKey> responses;
        for (const auto& secret : secrets) {
            auto response = AuthKey::derive(secret, {AuthRole::response, id, a.caller_key_id}, a.incarnation);
            if (!response) throw Usage("invalid caller or node id");
            responses.push_back(std::move(*response));
        }
        routes.push_back(ClientRoute{id, std::move(*ingress), std::move(responses)});
    }
    NodeClientSession session(epoch.epoch, std::move(routes), transport);
    if (!session.submit(command, a.lane, Clock::now(), std::chrono::milliseconds(a.timeout_ms)))
        throw Usage("command did not encode");
    while (true) {
        const auto outcome = session.pump(Clock::now());
        switch (outcome.state) {
        case SubmitState::committed:
            std::cout << "COMMITTED sequence=" << a.sequence << " index=" << outcome.index
                      << " term=" << outcome.term << " code=" << static_cast<int>(outcome.result.code)
                      << " code_name=" << apply_code_name(outcome.result.code)
                      << " state_version=" << outcome.result.state_version
                      << " kill_epoch=" << outcome.result.kill_epoch.value()
                      << " executor_generation=" << outcome.result.executor_generation.value()
                      << " strategy_mode=" << static_cast<int>(outcome.result.strategy_mode);
            if (outcome.result.detail_kind == ApplyResultDetailKind::risk_configuration)
                std::cout << " risk_version=" << outcome.result.detail.words[2];
            std::cout << " from=" << outcome.from
                      << " latency_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(
                             *outcome.committed - *outcome.first_send).count() << std::endl;
            return outcome.result.code == ApplyCode::applied ? 0 : 1;
        case SubmitState::refused:
            std::cout << "REFUSED reason=" << static_cast<int>(outcome.refusal_reason)
                      << " leader=" << outcome.leader_hint << std::endl;
            return 4;
        case SubmitState::timed_out:
            std::cout << "TIMEOUT sequence=" << a.sequence << std::endl;
            return 3;
        case SubmitState::pending:
        case SubmitState::idle:
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}

int main(int argc, char** argv) {
    try {
        return run(parse(argc, argv));
    } catch (const Usage& error) {
        std::cerr << "velocita-v2-client: " << error.what() << std::endl;
        return 64;
    } catch (const std::exception& error) {
        std::cerr << "velocita-v2-client FAILED: " << error.what() << std::endl;
        return 2;
    }
}
