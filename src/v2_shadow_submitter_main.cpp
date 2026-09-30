// velocita-v2-shadow-submitter: installs a rule program in the cluster (operator,
// one shot) or evaluates one DecisionFrame per tick read from stdin (strategy).
// Authority is split: --setup-only needs an operator caller (CallerId <= 15) and
// the tick loop refuses one, so the process that proposes cannot change controls.
//
//   velocita-v2-shadow-submitter [--setup-only] [--mode shadow|paper]
//       [--risk-version N --position-version N]
//       --program FILE --catalog FILE --caller-key-id N --caller-id N
//       --key-file FILE --incarnation HEX32 --session-file FILE --state-file FILE
//       --response-endpoint HOST:PORT --node ID=HOST:PORT [--node ...] --aeron-dir DIR
//       [--timeout-ms N]
//
// Each caller needs its OWN session and state files: a shared state file would
// skip the tick loop's source generation.
//
// --setup-only: Publish, Validate, Stage, ActivateExecutable, SetStrategyMode(normal);
//   stdout: SETUP generation=G first_sequence=S kill_epoch=K executor_generation=E strategy_mode=M
// Tick loop, shadow (default): EvaluationMode::shadow, frames bind risk 1 / position 1;
//   no decision can authorize. Paper: authoritative frames bound to the current
//   risk and position versions (start: --risk-version / --position-version, default
//   1/1); after an actionable decision, AuthorizeDecision. A stale_risk_state
//   refusal that reports the account's versions is adopted for the next frame.
// stdin:  TICK id=ID event_ns=N receive_ns=N age_ns=N features=F0,F1,..,PRICE evidence=HEX64 [exits_only=0|1]
//         exits_only=1 (paper): an enter_long proposal is recorded but not sent for authorization.
// stdout: READY generation=G first_sequence=S mode=shadow|paper caller_id=C
//         DECISION id=ID source_seq=Q decision=D code=C decision_code=DC rule=R proposed=P effective=E resolution=A
//                  from=V t_read_ns=T t_source_ns=T t_decision_ns=T
//           paper adds: risk_version=R position_version=P authorization=NAME|none|withheld [resync=1 position_lots=Q]
// Timestamps are wall-clock nanoseconds (comparable with the edge on the same host).
// Exit: 0 at end of input (or setup done); 2 setup refused; 3 a command could not be committed
// within the timeout (restart: the next run is a new source generation); 64 usage.

#include "velocita/aeron_runtime.hpp"
#include "velocita/v2/aeron_node_transport.hpp"
#include "velocita/v2/node_client.hpp"
#include "velocita/v2/node_files.hpp"
#include "velocita/v2/shadow_submitter.hpp"

#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace velocita::v2;
using Clock = std::chrono::steady_clock;

struct Usage final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

constexpr std::uint64_t kReserve = 10'000;
constexpr std::uint64_t kCommandsPerTick = 3;  // source event, evaluation, paper authorization

std::uint64_t number(const std::string& text, const char* what) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) throw Usage(std::string("invalid ") + what);
    return value;
}

Endpoint endpoint(const std::string& text) {
    const auto colon = text.rfind(':');
    if (colon == std::string::npos) throw Usage("endpoint needs HOST:PORT");
    return {text.substr(0, colon), static_cast<std::uint16_t>(number(text.substr(colon + 1), "port"))};
}

bool hex_bytes(std::string_view text, std::span<std::byte> out) {
    if (text.size() != 2 * out.size()) return false;
    for (std::size_t i = 0; i < out.size(); ++i) {
        unsigned v = 0;
        const auto r = std::from_chars(text.data() + 2 * i, text.data() + 2 * i + 2, v, 16);
        if (r.ec != std::errc{} || r.ptr != text.data() + 2 * i + 2) return false;
        out[i] = static_cast<std::byte>(v);
    }
    return true;
}

std::int64_t wall_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

struct Arguments {
    std::filesystem::path program, catalog, key_file, session_file, state_file, aeron_dir;
    std::int32_t caller_key_id{};
    std::uint64_t caller_id{};
    std::array<std::byte, 16> incarnation{};
    bool have_incarnation{};
    Endpoint response_endpoint;
    std::map<std::int32_t, Endpoint> nodes;
    std::vector<std::int32_t> order;
    std::uint64_t timeout_ms{30'000};
    bool setup_only{};
    bool paper{};
    std::uint64_t risk_version{1}, position_version{1};
};

Arguments parse(int argc, char** argv) {
    Arguments a;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--setup-only") {
            a.setup_only = true;
            continue;
        }
        if (i + 1 >= argc) throw Usage("missing value for " + key);
        const std::string value = argv[++i];
        if (key == "--program") a.program = value;
        else if (key == "--catalog") a.catalog = value;
        else if (key == "--key-file") a.key_file = value;
        else if (key == "--session-file") a.session_file = value;
        else if (key == "--state-file") a.state_file = value;
        else if (key == "--aeron-dir") a.aeron_dir = value;
        else if (key == "--caller-key-id") a.caller_key_id = static_cast<std::int32_t>(number(value, "caller key id"));
        else if (key == "--caller-id") a.caller_id = number(value, "caller id");
        else if (key == "--incarnation") a.have_incarnation = hex_bytes(value, a.incarnation);
        else if (key == "--response-endpoint") a.response_endpoint = endpoint(value);
        else if (key == "--timeout-ms") a.timeout_ms = number(value, "timeout");
        else if (key == "--mode") {
            if (value != "shadow" && value != "paper") throw Usage("--mode must be shadow or paper");
            a.paper = value == "paper";
        } else if (key == "--risk-version") a.risk_version = number(value, "risk version");
        else if (key == "--position-version") a.position_version = number(value, "position version");
        else if (key == "--node") {
            const auto eq = value.find('=');
            if (eq == std::string::npos) throw Usage("--node needs ID=HOST:PORT");
            const auto id = static_cast<std::int32_t>(number(value.substr(0, eq), "node id"));
            if (!a.nodes.emplace(id, endpoint(value.substr(eq + 1))).second) throw Usage("duplicate --node id");
            a.order.push_back(id);
        } else {
            throw Usage("unknown argument " + key);
        }
    }
    if (a.program.empty() || a.catalog.empty() || a.key_file.empty() || a.session_file.empty() ||
        a.state_file.empty() || a.aeron_dir.empty() || !a.have_incarnation || a.nodes.empty() ||
        a.response_endpoint.port == 0 || a.caller_key_id <= 0 || a.caller_id == 0 || a.risk_version == 0 ||
        a.position_version == 0)
        throw Usage("usage: see the header of v2_shadow_submitter_main.cpp");
    if (a.setup_only && a.paper) throw Usage("--setup-only installs the program; --mode applies to the tick loop");
    const auto role = submitter_role_error(a.setup_only ? SubmitterRole::setup : SubmitterRole::ticks,
                                           CallerId{a.caller_id});
    if (!role.empty()) throw Usage(role);
    return a;
}

std::string slurp(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw Usage("cannot read " + file.string());
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The program names its catalog root; the catalog file must verify against it.
Digest256 declared_root(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = line.substr(0, eq);
        key.erase(key.find_last_not_of(" \t") + 1);
        if (key != "catalog.root") continue;
        auto value = line.substr(eq + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r") + 1);
        Digest256 root;
        if (!hex_bytes(value, root.bytes)) throw Usage("program: invalid catalog.root");
        return root;
    }
    throw Usage("program: catalog.root missing");
}

// Submits COMMAND until it settles. A timeout is retried with the SAME command:
// it may already have committed, and an exact retry returns the retained result.
std::optional<SubmitOutcome> commit(NodeClientSession& session, const Command& command, ProposeLane lane,
                                    std::chrono::milliseconds total) {
    const auto deadline = Clock::now() + total;
    while (Clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (!session.submit(command, lane, Clock::now(), std::min(remaining, std::chrono::milliseconds(5000))))
            return std::nullopt;
        while (true) {
            const auto outcome = session.pump(Clock::now());
            if (outcome.state == SubmitState::committed || outcome.state == SubmitState::refused) return outcome;
            if (outcome.state == SubmitState::timed_out) break;
            std::this_thread::yield();
        }
    }
    return std::nullopt;
}

bool setup_step_ok(const ApplyResult& r, std::size_t step) {
    if (r.code == ApplyCode::applied || r.code == ApplyCode::already_applied) return true;
    // Re-running setup on a restart: the policy is already active, so validation
    // is no longer a legal transition. Accept exactly that case.
    return step == 1 && r.code == ApplyCode::invalid_policy_transition && r.policy_lifecycle == PolicyLifecycle::active;
}

int run(const Arguments& a) {
    const auto program_text = slurp(a.program);
    const auto catalog = load_catalog_file(a.catalog, declared_root(program_text));
    if (catalog.error != NodeFileError::none) throw Usage("catalog file refused");
    const auto parsed = parse_shadow_program(program_text, catalog.catalog);
    if (!parsed.program) throw Usage("program refused: " + parsed.error);
    const auto& program = *parsed.program;

    auto secrets = load_secret_set(a.key_file);
    if (!secrets.secrets) throw Usage("key file refused: " + secrets.failed_file.string());
    std::vector<AuthSecret> keys;
    keys.push_back(std::move(secrets.secrets->current));
    if (secrets.secrets->next) keys.push_back(std::move(*secrets.secrets->next));
    if (secrets.secrets->previous) keys.push_back(std::move(*secrets.secrets->previous));

    auto epoch = advance_session_epoch_file(a.session_file);
    if (epoch.error == SessionEpochError::missing) epoch = create_session_epoch_file(a.session_file);
    if (epoch.error != SessionEpochError::none) throw Usage("session epoch refused");
    const auto start = begin_submitter_state(a.state_file, a.incarnation, kReserve);
    if (!start.error.empty()) throw Usage("state refused: " + start.error);
    std::uint64_t sequence = start.first_sequence, reserved = start.first_sequence + kReserve;

    velocita::AeronRuntime aeron(a.aeron_dir.string());
    AeronClientTransport transport(aeron, a.nodes, a.response_endpoint);
    std::vector<ClientRoute> routes;
    for (const auto id : a.order) {
        auto ingress = AuthKey::derive(keys.front(), {AuthRole::ingress, a.caller_key_id, id}, a.incarnation);
        if (!ingress) throw Usage("invalid caller or node id");
        std::vector<AuthKey> responses;
        for (const auto& key : keys) {
            auto response = AuthKey::derive(key, {AuthRole::response, id, a.caller_key_id}, a.incarnation);
            if (!response) throw Usage("invalid caller or node id");
            responses.push_back(std::move(*response));
        }
        routes.push_back(ClientRoute{id, std::move(*ingress), std::move(responses)});
    }
    NodeClientSession session(epoch.epoch, std::move(routes), transport);
    const auto total = std::chrono::milliseconds(a.timeout_ms);
    const CallerId caller{a.caller_id};

    if (a.setup_only) {
        // The four steps that need no control values, then the mode change built
        // from the kill epoch and executor generation the cluster just reported.
        const auto provisional = shadow_setup(program, caller, sequence, KillEpoch{}, ExecutorGeneration{});
        ApplyResult last;
        for (std::size_t step = 0; step < 4; ++step) {
            // Policy commands are ordinary commands: only the mode change is a control command.
            const auto outcome = commit(session, provisional.commands[step], ProposeLane::normal, total);
            if (outcome && outcome->state == SubmitState::committed &&
                outcome->result.code == ApplyCode::catalog_not_installed) {
                std::cout << "ERROR setup reason=catalog_not_installed hint=\"operator: velocita-v2-client "
                             "executor-generation, then install-catalog ROOT KILL_EPOCH EXECUTOR_GENERATION\""
                          << std::endl;
                return 2;
            }
            if (!outcome || outcome->state != SubmitState::committed || !setup_step_ok(outcome->result, step)) {
                std::cout << "ERROR setup step=" << step << " code="
                          << (outcome && outcome->state == SubmitState::committed
                                  ? std::string(apply_code_name(outcome->result.code)) : std::string("none"))
                          << " refusal=" << (outcome ? static_cast<int>(outcome->refusal_reason) : -1) << std::endl;
                return 2;
            }
            last = outcome->result;
        }
        const auto final_setup = shadow_setup(program, caller, sequence, last.kill_epoch, last.executor_generation);
        const auto mode = commit(session, final_setup.commands[4], ProposeLane::control, total);
        if (!mode || mode->state != SubmitState::committed || !setup_step_ok(mode->result, 4)) {
            std::cout << "ERROR setup step=4 code="
                      << (mode ? std::string(apply_code_name(mode->result.code)) : std::string("none")) << std::endl;
            return 2;
        }
        std::cout << "SETUP generation=" << start.generation << " first_sequence=" << start.first_sequence
                  << " kill_epoch=" << mode->result.kill_epoch.value()
                  << " executor_generation=" << mode->result.executor_generation.value()
                  << " strategy_mode=" << static_cast<int>(mode->result.strategy_mode) << std::endl;
        return 0;
    }

    std::cout << "READY generation=" << start.generation << " first_sequence=" << start.first_sequence
              << " mode=" << (a.paper ? "paper" : "shadow") << " caller_id=" << a.caller_id << std::endl;
    RiskView view{RiskVersion{a.risk_version}, PositionVersion{a.position_version}, std::nullopt};

    std::uint64_t source_sequence = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto t_read = wall_ns();
        std::istringstream in(line);
        std::string verb;
        in >> verb;
        if (verb != "TICK") continue;
        std::map<std::string, std::string> f;
        for (std::string token; in >> token;) {
            const auto eq = token.find('=');
            if (eq != std::string::npos) f[token.substr(0, eq)] = token.substr(eq + 1);
        }
        ShadowTick tick;
        bool exits_only = false;
        tick.source = parsed.source;
        tick.generation = SourceGeneration{start.generation};
        tick.sequence = SourceSequence{source_sequence + 1};
        try {
            tick.event_time_ns = static_cast<std::int64_t>(number(f.at("event_ns"), "event_ns"));
            tick.receive_time_ns = static_cast<std::int64_t>(number(f.at("receive_ns"), "receive_ns"));
            tick.market_data_age_ns = static_cast<std::int64_t>(number(f.at("age_ns"), "age_ns"));
            std::istringstream values(f.at("features"));
            for (std::string v; std::getline(values, v, ',');) {
                std::int64_t x = 0;
                const auto r = std::from_chars(v.data(), v.data() + v.size(), x);
                if (r.ec != std::errc{} || r.ptr != v.data() + v.size()) throw Usage("feature");
                tick.features.push_back(x);
            }
            if (!hex_bytes(f.at("evidence"), tick.evidence_digest.bytes) || tick.evidence_digest.is_zero())
                throw Usage("evidence");
            if (tick.features.size() != program.generic_features + 1u) throw Usage("feature count");
            const auto gate = parse_exits_only(f.contains("exits_only") ? std::optional<std::string_view>(f["exits_only"])
                                                                        : std::nullopt);
            if (!gate) throw Usage("exits_only");
            exits_only = *gate;
        } catch (const std::exception& error) {
            std::cout << "ERROR id=" << f["id"] << " reason=bad_tick" << std::endl;
            continue;  // nothing was sent; the source sequence is unused
        }
        ++source_sequence;
        if (sequence + kCommandsPerTick >= reserved) {
            reserved = sequence + kReserve;
            if (const auto e = extend_reservation(a.state_file, a.incarnation, start.generation, reserved); !e.empty())
                throw std::runtime_error("reservation: " + e);
        }
        const DecisionId decision{(start.generation << 32) | source_sequence};
        const FrameBinding binding = a.paper
            ? FrameBinding{EvaluationMode::authoritative, view.risk_version, view.position_version}
            : FrameBinding{};
        const auto commands = shadow_tick(program, tick, caller, sequence, decision, binding);
        sequence += 2;
        const auto source = commit(session, commands[0], ProposeLane::normal, total);
        const auto t_source = wall_ns();
        if (!source || source->state != SubmitState::committed || source->result.code != ApplyCode::applied) {
            std::cout << "ERROR id=" << f["id"] << " reason=source code="
                      << (source ? static_cast<int>(source->result.code) : -1) << std::endl;
            return 3;
        }
        const auto evaluated = commit(session, commands[1], ProposeLane::normal, total);
        const auto t_decision = wall_ns();
        if (!evaluated || evaluated->state != SubmitState::committed) {
            std::cout << "ERROR id=" << f["id"] << " reason=decision" << std::endl;
            return 3;
        }
        const auto& r = evaluated->result;
        std::string_view authorization = "none";
        bool resynced = false;
        const auto request = a.paper ? paper_request(program, r, exits_only) : PaperRequest::none;
        if (request == PaperRequest::withheld) authorization = "withheld";
        if (request == PaperRequest::authorize) {
            const auto outcome = commit(session, authorize_decision(caller, sequence++, decision), ProposeLane::normal,
                                        total);
            if (!outcome || outcome->state != SubmitState::committed) {
                std::cout << "ERROR id=" << f["id"] << " reason=authorization" << std::endl;
                return 3;
            }
            authorization = apply_code_name(outcome->result.code);
            const auto next = resync_risk_view(view, outcome->result);
            resynced = next != view;
            view = next;
        }
        std::cout << "DECISION id=" << f["id"] << " source_seq=" << source_sequence << " decision=" << decision.value()
                  << " code=" << static_cast<int>(r.code) << " decision_code=" << static_cast<int>(r.decision_code)
                  << " rule=" << r.matched_rule_index << " proposed=" << static_cast<int>(r.proposed_directive)
                  << " effective=" << static_cast<int>(r.effective_directive)
                  << " resolution=" << static_cast<int>(r.action_resolution_code) << " from=" << evaluated->from
                  << " t_read_ns=" << t_read << " t_source_ns=" << t_source << " t_decision_ns=" << t_decision;
        if (a.paper) {
            std::cout << " risk_version=" << binding.risk_version.value()
                      << " position_version=" << binding.position_version.value() << " authorization=" << authorization;
            if (resynced)
                std::cout << " resync=1 position_lots=" << (view.position ? view.position->atoms() : 0);
        }
        std::cout << std::endl;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse(argc, argv));
    } catch (const Usage& usage) {
        std::cerr << usage.what() << '\n';
        return 64;
    } catch (const std::exception& error) {
        std::cerr << "shadow submitter failed: " << error.what() << '\n';
        return 1;
    }
}
