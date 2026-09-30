// M2 exit: two real velocita-v2-node processes on loopback start, commit,
// crash, restart, partition, heal, replay from disk and stop, driven by the
// real client binary. Peer traffic runs through an in-test UDP relay that can
// drop everything (a partition). Expected values come from the reducer.
//
//   v2-node-process-tests NODE_BINARY CLIENT_BINARY STATUS_BINARY

#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/node_health.hpp"
#include "v2_catalog_test_support.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <optional>
#include <poll.h>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {

using namespace velocita::v2;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// Written the way the Mac tool writes: private temporary, then atomic rename.
void write_secret(const fs::path& path, std::uint8_t seed, std::size_t length = 32) {
    std::string bytes(length, '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<char>(seed + i);
    auto temporary = path;
    temporary += ".tmp";
    { std::ofstream(temporary, std::ios::binary) << bytes; }
    require(::chmod(temporary.c_str(), 0600) == 0, "secret chmod failed");
    fs::rename(temporary, path);
}

// Bidirectional UDP relay: publisher -> LISTEN -> relay -> TARGET, with the
// subscriber's replies (status messages, NAKs) forwarded back. Aeron flow
// control needs both directions. `partitioned` drops everything.
class Relay {
public:
    std::atomic<bool> partitioned{false};

    Relay(std::uint16_t listen_port, std::uint16_t target_port) {
        listen_ = bound_socket(listen_port);
        upstream_ = bound_socket(0);
        target_ = address(target_port);
        worker_ = std::thread([this] { loop(); });
    }
    ~Relay() {
        stop_ = true;
        worker_.join();
        ::close(listen_);
        ::close(upstream_);
    }

private:
    static sockaddr_in address(std::uint16_t port) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return a;
    }
    static int bound_socket(std::uint16_t port) {
        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        require(fd >= 0, "relay socket failed");
        auto a = address(port);
        require(::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0, "relay bind failed on " + std::to_string(port));
        return fd;
    }
    void loop() {
        std::vector<char> buffer(65536);
        std::optional<sockaddr_in> publisher;
        while (!stop_) {
            pollfd fds[2] = {{listen_, POLLIN, 0}, {upstream_, POLLIN, 0}};
            if (::poll(fds, 2, 20) <= 0) continue;
            if (fds[0].revents & POLLIN) {
                sockaddr_in from{};
                socklen_t length = sizeof(from);
                const auto n = ::recvfrom(listen_, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&from), &length);
                if (n > 0) {
                    publisher = from;
                    if (!partitioned) ::sendto(upstream_, buffer.data(), n, 0, reinterpret_cast<sockaddr*>(&target_), sizeof(target_));
                }
            }
            if (fds[1].revents & POLLIN) {
                const auto n = ::recv(upstream_, buffer.data(), buffer.size(), 0);
                if (n > 0 && publisher && !partitioned)
                    ::sendto(listen_, buffer.data(), n, 0, reinterpret_cast<sockaddr*>(&*publisher), sizeof(*publisher));
            }
        }
    }
    int listen_{-1}, upstream_{-1};
    sockaddr_in target_{};
    std::atomic<bool> stop_{false};
    std::thread worker_;
};

std::string link_key(int a, int b) {
    return "link-" + std::to_string(std::min(a, b)) + "-" + std::to_string(std::max(a, b)) + ".key";
}

struct Harness {
    std::string node_binary, client_binary, status_binary;
    fs::path root;
    std::uint16_t base{};
    std::vector<int> voters{1, 2};
    bool relayed{true};  // two voters only: each relay forwards replies to ONE publisher
    std::uint32_t compact_at_entries{};  // 0: the node's default
    std::map<int, pid_t> pids;
    std::map<int, int> launches;
    std::uint64_t client_runs{};

    std::uint16_t peer_port(int node) const { return static_cast<std::uint16_t>(base + node); }        // own bind
    std::uint16_t relay_port(int to) const { return static_cast<std::uint16_t>(base + 10 + to); }      // relay -> to
    std::uint16_t ingress_port(int node) const { return static_cast<std::uint16_t>(base + 20 + node); }
    std::uint16_t response_port() const { return static_cast<std::uint16_t>(base + 30); }
    fs::path data(int node) const { return root / ("node-" + std::to_string(node)); }
    fs::path keys(int node) const { return root / ("keys-" + std::to_string(node)); }  // 0 = the client

    void write_config(int node, const char* startup) {
        std::ostringstream c;
        c << "node_id = " << node << "\n"
          << "data_root = " << data(node).string() << "\n"
          << "startup = " << startup << "\n"
          << "cluster.incarnation = ab5a0000000000000000000000000077\n"
          << "catalog.root = " << to_hex(catalog_test::standard_catalog().root()) << "\n"
          << "catalog.file = " << (root / "catalog.bin").string() << "\n"
          << "deployment_mode = laboratory\n"
          << "voter." << node << ".endpoint = 127.0.0.1:" << peer_port(node) << "\n";
        if (compact_at_entries != 0) c << "history.compact_at_entries = " << compact_at_entries << "\n";
        for (const int other : voters) {
            if (other == node) continue;
            c << "voter." << other << ".endpoint = 127.0.0.1:" << (relayed ? relay_port(other) : peer_port(other)) << "\n"
              << "voter." << other << ".key_file = " << (keys(node) / link_key(node, other)).string() << "\n";
        }
        c << "ingress.endpoint = 127.0.0.1:" << ingress_port(node) << "\n"
          << "caller.9.caller_id = 7\n"
          << "caller.9.key_file = " << (keys(node) / "caller.key").string() << "\n"
          << "caller.9.response_endpoint = 127.0.0.1:" << response_port() << "\n"
          // The shadow setup's own route (operator band): a route is one
          // session, so it never shares caller 9's replay fence.
          << "caller.11.caller_id = 9\n"
          << "caller.11.key_file = " << (keys(node) / "shadow.key").string() << "\n"
          << "caller.11.response_endpoint = 127.0.0.1:" << response_port() + 1 << "\n"
          // The tick loop's strategy route, outside the operator band.
          << "caller.12.caller_id = 21\n"
          << "caller.12.key_file = " << (keys(node) / "strategy.key").string() << "\n"
          << "caller.12.response_endpoint = 127.0.0.1:" << response_port() + 2 << "\n"
          << "history.max_wal_bytes = 1073741824\n";
        { std::ofstream(root / ("node-" + std::to_string(node) + ".conf")) << c.str(); }
    }

    pid_t spawn(const std::vector<std::string>& args, const fs::path& log) {
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
        std::vector<char*> argv;
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        pid_t pid{};
        const int spawned = ::posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        require(spawned == 0, "posix_spawn failed for " + args[0]);
        return pid;
    }

    fs::path log_of(int node) const {
        return root / ("node-" + std::to_string(node) + "-" + std::to_string(launches.at(node)) + ".log");
    }

    void start(int node, const char* startup) {
        write_config(node, startup);
        ++launches[node];
        pids[node] = spawn({node_binary, "--config", (root / ("node-" + std::to_string(node) + ".conf")).string()},
                           log_of(node));
        const auto deadline = Clock::now() + std::chrono::seconds(20);
        while (Clock::now() < deadline) {
            if (read_file(log_of(node)).find("READY node=" + std::to_string(node)) != std::string::npos) return;
            int status = 0;
            require(::waitpid(pids[node], &status, WNOHANG) == 0, "node " + std::to_string(node) +
                    " exited during startup: " + read_file(log_of(node)));
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        throw std::runtime_error("node " + std::to_string(node) + " never became READY: " + read_file(log_of(node)));
    }

    int stop(int node, int signal_number) {
        require(::kill(pids.at(node), signal_number) == 0, "kill failed");
        int status = 0;
        require(::waitpid(pids.at(node), &status, 0) == pids.at(node), "waitpid failed");
        pids.erase(node);
        return status;
    }

    std::optional<NodeHealth> health(int node) const { return read_node_health(data(node) / "health.page"); }

    std::string all_logs() const {
        std::string logs;
        for (const int node : voters)
            if (launches.contains(node)) logs += read_file(log_of(node));
        return logs;
    }

    int leader() const {
        for (const int node : voters) {
            if (!pids.contains(node)) continue;
            const auto h = health(node);
            if (h && h->role == 3 && h->ready && !h->failed) return node;
        }
        return 0;
    }

    int await_leader() {
        const auto deadline = Clock::now() + std::chrono::seconds(30);
        while (Clock::now() < deadline) {
            if (const int id = leader()) return id;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        throw std::runtime_error("no ready leader: node logs:\n" + all_logs());
    }

    struct ClientResult { int exit_code; std::string output; };

    // FIRST_NODE is asked first; the other voters follow in ascending order.
    ClientResult client(std::uint64_t sequence, std::uint64_t kill_epoch, int first_node, int timeout_ms) {
        return operator_command(sequence, {"kill-epoch", std::to_string(kill_epoch)}, first_node, timeout_ms);
    }

    ClientResult operator_command(std::uint64_t sequence, const std::vector<std::string>& verb, int first_node,
                                  int timeout_ms) {
        const auto run = ++client_runs;
        const auto log = root / ("client-" + std::to_string(run) + ".log");
        std::vector<std::string> args{client_binary, "--caller-key-id", "9", "--caller-id", "7",
                                      "--key-file", (keys(0) / "caller.key").string(),
                                      "--incarnation", "ab5a0000000000000000000000000077",
                                      "--session-file", (root / "client-session.bin").string(),
                                      "--response-endpoint", "127.0.0.1:" + std::to_string(response_port())};
        std::vector<int> order{first_node};
        for (const int node : voters)
            if (node != first_node) order.push_back(node);
        for (const int node : order)
            args.insert(args.end(), {"--node", std::to_string(node) + "=127.0.0.1:" + std::to_string(ingress_port(node))});
        args.insert(args.end(), {"--aeron-dir", (root / ("client-aeron-" + std::to_string(run))).string(),
                                 "--sequence", std::to_string(sequence), "--timeout-ms", std::to_string(timeout_ms)});
        args.insert(args.end(), verb.begin(), verb.end());
        const auto pid = spawn(args, log);
        int status = 0;
        require(::waitpid(pid, &status, 0) == pid, "client waitpid failed");
        return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, read_file(log)};
    }

    // SIGHUP, then wait for the node's own verdict line (applied or refused).
    std::string reload(int node) {
        const auto before = read_file(log_of(node));
        require(::kill(pids.at(node), SIGHUP) == 0, "SIGHUP failed");
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (Clock::now() < deadline) {
            const auto now = read_file(log_of(node));
            if (now.size() > before.size()) {
                const auto added = now.substr(before.size());
                if (added.find("RELOADED") != std::string::npos) return "applied";
                if (added.find("RELOAD_REFUSED") != std::string::npos) return "refused";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        throw std::runtime_error("node " + std::to_string(node) + " never answered SIGHUP");
    }

    ~Harness() {
        for (auto& [node, pid] : pids) {
            ::kill(pid, SIGKILL);
            int status = 0;
            ::waitpid(pid, &status, 0);
        }
    }
};

std::string field(const std::string& output, const std::string& key) {
    const auto at = output.find(" " + key + "=");
    if (at == std::string::npos) return {};
    const auto begin = at + key.size() + 2;
    return output.substr(begin, output.find_first_of(" \n", begin) - begin);
}

// Independent expectation for "kill-epoch N" after N-1 earlier such commands.
std::uint64_t expected_state_version(std::uint64_t commands) {
    TradingState state{};
    ApplyScratch scratch{};
    ApplyResult result{};
    for (std::uint64_t s = 1; s <= commands; ++s) {
        Digest256 digest{};
        digest.bytes[0] = std::byte{1};
        result = apply(state, {{CallerId{7}, RequestSequence{s}, digest}, AdvanceKillEpoch{KillEpoch{s}}}, scratch,
                       &catalog_test::standard_catalog());
        require(result.code == ApplyCode::applied, "independent reducer refused the scenario");
    }
    return result.state_version;
}

void require_committed(const Harness::ClientResult& result, std::uint64_t kill_epoch, std::uint64_t commands,
                       const std::string& what) {
    require(result.exit_code == 0, what + ": client did not commit (exit " + std::to_string(result.exit_code) +
                                       "): " + result.output);
    require(field(result.output, "kill_epoch") == std::to_string(kill_epoch),
            what + ": committed kill epoch differs: " + result.output);
    require(field(result.output, "state_version") == std::to_string(expected_state_version(commands)),
            what + ": state version differs from the independent reducer: " + result.output);
}

void scenario(Harness& h) {
    h.start(1, "bootstrap");
    h.start(2, "bootstrap");
    const int leader = h.await_leader();
    const int follower = leader == 1 ? 2 : 1;

    // 1. The client asks the follower first and must follow the redirect. The
    //    follower's refusal counter proves it was really asked (and refused).
    require_committed(h.client(1, 1, follower, 15000), 1, 1, "first commit via redirect");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // one health publish interval
    const auto asked = h.health(follower);
    require(asked && asked->ingress_refusals >= 1, "the follower was never asked: no redirect happened");

    // 2. Two voters: with the follower dead nothing commits (fail-closed).
    const int killed = h.stop(follower, SIGKILL);
    require(WIFSIGNALED(killed), "follower did not die by SIGKILL");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto log_before = h.health(leader)->current_index;
    const auto blocked = h.client(2, 2, leader, 3000);
    require(blocked.exit_code == 3, "commit succeeded without the second voter: " + blocked.output);
    // Once the leader has ADMITTED the request, resending it only adds
    // duplicate log entries (M3 finding 4). A 3 s outage may cost at most two.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto growth = h.health(leader)->current_index - log_before;
    require(growth >= 1 && growth <= 2, "client flooded the leader's log with " + std::to_string(growth) +
                                            " copies of one request during an outage");
    h.start(follower, "recover");
    require(read_file(h.log_of(follower)).find("epoch=2") != std::string::npos,
            "restarted follower did not advance its session epoch");
    h.await_leader();
    require_committed(h.client(2, 2, leader, 20000), 2, 2, "commit after follower restart");
}

void write_catalog(const fs::path& file) {
    std::vector<std::byte> catalog(kMaxExecutionCatalogBytes);
    const auto encoded = catalog_test::standard_catalog().encode(catalog);
    require(encoded.error == ExecutionCatalogError::none, "catalog fixture did not encode");
    std::ofstream(file, std::ios::binary)
        .write(reinterpret_cast<const char*>(catalog.data()), static_cast<std::streamsize>(encoded.bytes_written));
}

// Three real processes over real Aeron, voters addressing each other directly
// as the three-host templates do. Any one voter may die (SIGKILL: Aeron then
// reports it not connected) and the other two keep committing; two dead is a
// lost quorum and nothing commits.
void three_voter_scenario(Harness& h) {
    for (const int who : {0, 1, 2, 3}) {
        fs::create_directories(h.keys(who));
        fs::permissions(h.keys(who), fs::perms::owner_all, fs::perm_options::replace);
        write_secret(h.keys(who) / "caller.key", 0x61);
        write_secret(h.keys(who) / "shadow.key", 0x71);
            write_secret(h.keys(who) / "strategy.key", 0x81);
        for (const int other : h.voters)
            if (who != 0 && other != who)
                write_secret(h.keys(who) / link_key(who, other), static_cast<std::uint8_t>(0x20 + who + other));
    }
    write_catalog(h.root / "catalog.bin");
    for (const int node : h.voters) h.start(node, "bootstrap");
    const int leader = h.await_leader();
    require_committed(h.client(1, 1, leader, 20000), 1, 1, "three voters: first commit");

    const int lost = leader == 3 ? 2 : 3;
    const auto killed = [&](int node) {
        const int status = h.stop(node, SIGKILL);
        return WIFSIGNALED(status);
    };
    const auto refused_before = h.health(leader)->peer_backpressure;
    require(killed(lost), "follower did not die by SIGKILL");
    // Aeron keeps a dead peer's publication "connected" until its liveness
    // timeout; commit only once the leader's offers to it are really refused.
    const auto refused_deadline = Clock::now() + std::chrono::seconds(30);
    while (h.health(leader)->peer_backpressure < refused_before + 20 && Clock::now() < refused_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(h.health(leader)->peer_backpressure >= refused_before + 20,
            "the leader never saw the dead follower as unreachable");
    require_committed(h.client(2, 2, leader, 20000), 2, 2, "three voters: one follower dead");
    h.start(lost, "recover");

    require(killed(leader), "leader did not die by SIGKILL");
    const int next = h.await_leader();
    require(next != leader, "a killed voter is still the leader");
    require_committed(h.client(3, 3, next, 20000), 3, 3, "three voters: leader dead");
    h.start(leader, "recover");
    require_committed(h.client(4, 4, h.await_leader(), 20000), 4, 4, "three voters: every voter back");

    // Both restarted voters caught up from the log to the same applied index.
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    bool converged = false;
    while (!converged && Clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto a = h.health(1), b = h.health(2), c = h.health(3);
        converged = a && b && c && a->applied_index == a->commit_index && a->applied_index == b->applied_index &&
                    a->applied_index == c->applied_index && a->applied_index >= 4;
    }
    require(converged, "three voters did not converge on one applied index:\n" + h.all_logs());

    const int survivor = h.await_leader();
    for (const int node : h.voters)
        if (node != survivor) require(killed(node), "voter did not die by SIGKILL");
    const auto blocked = h.client(5, 5, survivor, 3000);
    require(blocked.exit_code == 3, "committed with one voter of three: " + blocked.output);
    const auto stopped = h.stop(survivor, SIGTERM);
    require(WIFEXITED(stopped) && WEXITSTATUS(stopped) == 0, "surviving voter did not exit cleanly on SIGTERM");
}

// A child process with pipes on stdin and stdout, read line by line.
struct Piped {
    pid_t pid{-1};
    int in{-1}, out{-1};
    std::string buffer;

    Piped(const std::vector<std::string>& args, const fs::path& stderr_log) {
        int to_child[2], from_child[2];
        require(::pipe(to_child) == 0 && ::pipe(from_child) == 0, "pipe failed");
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, to_child[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, from_child[1], STDOUT_FILENO);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, stderr_log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_addclose(&actions, to_child[1]);
        posix_spawn_file_actions_addclose(&actions, from_child[0]);
        std::vector<char*> argv;
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        require(::posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ) == 0, "spawn failed");
        posix_spawn_file_actions_destroy(&actions);
        ::close(to_child[0]);
        ::close(from_child[1]);
        in = to_child[1];
        out = from_child[0];
    }
    void send(const std::string& line) {
        const auto text = line + "\n";
        require(::write(in, text.data(), text.size()) == static_cast<ssize_t>(text.size()), "write to child failed");
    }
    std::string line(std::chrono::milliseconds timeout) {
        const auto deadline = Clock::now() + timeout;
        while (true) {
            if (const auto nl = buffer.find('\n'); nl != std::string::npos) {
                auto got = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                return got;
            }
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (left <= 0) throw std::runtime_error("child produced no line in time; partial: " + buffer);
            pollfd fd{out, POLLIN, 0};
            if (::poll(&fd, 1, static_cast<int>(left)) <= 0) continue;
            char chunk[4096];
            const auto n = ::read(out, chunk, sizeof(chunk));
            if (n <= 0) throw std::runtime_error("child closed its output; partial: " + buffer);
            buffer.append(chunk, static_cast<std::size_t>(n));
        }
    }
    int finish() {
        ::close(in);
        int status = 0;
        require(::waitpid(pid, &status, 0) == pid, "waitpid failed");
        ::close(out);
        return status;
    }
};

std::string word(const std::string& line, const std::string& key) { return field(" " + line, key); }

// The shadow submitter over real processes and real Aeron: the operator's
// one-shot setup, then the strategy's tick loop with one shadow decision per
// tick on the rule worked out by hand, leader loss survived, and a restart in
// PAPER mode on generation 2 with request sequences past the first run's
// reservation, authorizing through the kernel's own risk limits and halting.
void shadow_submitter_scenario(Harness& h, const std::string& submitter) {
    for (const int who : {0, 1, 2, 3}) {
        fs::create_directories(h.keys(who));
        fs::permissions(h.keys(who), fs::perms::owner_all, fs::perm_options::replace);
        write_secret(h.keys(who) / "caller.key", 0x61);
        write_secret(h.keys(who) / "shadow.key", 0x71);
        write_secret(h.keys(who) / "strategy.key", 0x81);
        for (const int other : h.voters)
            if (who != 0 && other != who)
                write_secret(h.keys(who) / link_key(who, other), static_cast<std::uint8_t>(0x20 + who + other));
    }
    write_catalog(h.root / "catalog.bin");
    for (const int node : h.voters) h.start(node, "bootstrap");
    const int leader = h.await_leader();

    // Operator ceremony through the operator client, as a person would run it.
    require(h.operator_command(1, {"kill-epoch", "1"}, leader, 20000).exit_code == 0, "kill epoch refused");
    require(h.operator_command(2, {"executor-generation", "1"}, leader, 20000).exit_code == 0,
            "executor generation refused");
    const auto installed = h.operator_command(
        3, {"install-catalog", to_hex(catalog_test::standard_catalog().root()), "1", "1"}, leader, 20000);
    require(installed.exit_code == 0, "catalog installation refused: " + installed.output);

    const auto program = h.root / "trend.program";
    {
        std::ofstream(program) << "catalog.root = " << to_hex(catalog_test::standard_catalog().root()) << "\n"
            "catalog.account = 5\ncatalog.venue = 9\ncatalog.instrument = 4\n"
            "catalog.instrument_spec_version = 7\ncatalog.venue_capabilities_version = 8\n"
            "policy.id = 21\npolicy.version = 1\npolicy.feature_schema_id = 31\npolicy.risk_version = 1\n"
            "policy.model_digest = 5a" << std::string(62, '1') << "\n"
            "policy.valid_from_ns = 1000\npolicy.valid_until_ns = 1000000000000\nfeatures.generic = 4\n"
            "rule.1 = 0 gt 0 enter_long\nrule.2 = 0 le 0 exit\n"
            "order.time_in_force = gtc\norder.post_only = required\norder.quantity_lots = 1\n"
            "order.max_notional_micros = 10000\norder.max_market_data_age_ns = 5000000000\nsource.id = 41\n";
    }
    // Each role its own key, CallerId, session, state file and response port.
    const auto role_args = [&](const std::string& key_id, const std::string& caller, const std::string& name,
                               int port_offset) {
        std::vector<std::string> args{submitter, "--program", program.string(), "--catalog",
            (h.root / "catalog.bin").string(), "--caller-key-id", key_id, "--caller-id", caller, "--key-file",
            (h.keys(0) / (name + ".key")).string(), "--incarnation", "ab5a0000000000000000000000000077",
            "--session-file", (h.root / (name + ".session")).string(), "--state-file",
            (h.root / (name + ".state")).string(), "--response-endpoint",
            "127.0.0.1:" + std::to_string(h.response_port() + port_offset), "--aeron-dir",
            (h.root / (name + "-aeron")).string(), "--timeout-ms", "20000"};
        for (const int node : h.voters)
            args.insert(args.end(), {"--node", std::to_string(node) + "=127.0.0.1:" + std::to_string(h.ingress_port(node))});
        return args;
    };
    const auto setup_args = [&] {
        auto args = role_args("11", "9", "shadow", 1);
        args.push_back("--setup-only");
        return args;
    }();
    const auto args = role_args("12", "21", "strategy", 2);

    // Split authority, refused before anything is sent: the tick loop with the
    // operator's CallerId, and setup with the strategy's.
    {
        auto operator_ticks = role_args("11", "9", "shadow", 1);
        Piped refused(operator_ticks, h.root / "refused-ticks.log");
        const int code = refused.finish();
        require(WIFEXITED(code) && WEXITSTATUS(code) == 64 &&
                    read_file(h.root / "refused-ticks.log").find("operator") != std::string::npos,
                "the tick loop ran with an operator caller: " + read_file(h.root / "refused-ticks.log"));
        auto strategy_setup = role_args("12", "21", "strategy", 2);
        strategy_setup.push_back("--setup-only");
        Piped refused_setup(strategy_setup, h.root / "refused-setup.log");
        const int setup_code = refused_setup.finish();
        require(WIFEXITED(setup_code) && WEXITSTATUS(setup_code) == 64,
                "setup ran with a strategy caller: " + read_file(h.root / "refused-setup.log"));
    }
    {
        Piped setup(setup_args, h.root / "setup.log");
        const auto done = setup.line(std::chrono::seconds(60));
        require(done.rfind("SETUP ", 0) == 0 &&
                    word(done, "strategy_mode") == std::to_string(static_cast<int>(StrategyMode::normal)),
                "operator setup did not enable the strategy: " + done + " " + read_file(h.root / "setup.log"));
        const int code = setup.finish();
        require(WIFEXITED(code) && WEXITSTATUS(code) == 0, "setup did not exit cleanly");
    }

    const auto shadow_recorded = std::to_string(static_cast<int>(DecisionCode::shadow_recorded));
    const auto no_action = std::to_string(static_cast<int>(Directive::no_action));
    const auto tick = [&](Piped& p, int id, bool up, int expected_source) {
        std::ostringstream digest;
        digest << std::hex;
        for (int i = 0; i < 32; ++i) digest << (i == 0 ? "e" : "0") << (i == 31 ? (id % 16) : (i == 30 ? id / 16 % 16 : 0));
        p.send("TICK id=" + std::to_string(id) + " event_ns=" + std::to_string(2000 + id) + " receive_ns=" +
               std::to_string(2010 + id) + " age_ns=10 features=" + (up ? "50000" : "-50000") + ",0,0,0,11 evidence=" +
               digest.str());
        const auto line = p.line(std::chrono::seconds(30));
        require(line.rfind("DECISION ", 0) == 0, "tick " + std::to_string(id) + " got: " + line);
        require(word(line, "decision_code") == shadow_recorded && word(line, "effective") == no_action,
                "tick " + std::to_string(id) + " was not a shadow-only decision: " + line);
        require(word(line, "rule") == (up ? "0" : "1"), "tick " + std::to_string(id) + " matched the wrong rule: " + line);
        require(word(line, "source_seq") == std::to_string(expected_source), "source sequence not contiguous: " + line);
    };

    Piped first(args, h.root / "submitter-1.log");
    const auto ready = first.line(std::chrono::seconds(60));
    require(ready.rfind("READY ", 0) == 0 && word(ready, "generation") == "1" && word(ready, "first_sequence") == "1",
            "submitter did not set up: " + ready + " " + read_file(h.root / "submitter-1.log"));
    for (int id = 1; id <= 10; ++id) tick(first, id, id % 2 == 1, id);
    // Idle past the node's once-a-second trigger: every voter holds the whole
    // log, so each compacts (threshold 16; ~28 entries), below its floor.
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    for (const int node : h.voters) {
        const auto page = h.health(node);
        require(page && page->compactions >= 1 && page->snapshot_index > 0 &&
                    page->snapshot_index <= page->compaction_floor,
                "node " + std::to_string(node) + " did not compact below its floor: compactions " +
                    std::to_string(page ? page->compactions : 0) + " snapshot " +
                    std::to_string(page ? page->snapshot_index : 0) + " floor " +
                    std::to_string(page ? page->compaction_floor : 0));
    }
    // ...and the scenario goes on over a compacted log: a leader SIGKILL, then
    // a restarted submitter on the next generation.
    const int killed = h.stop(leader, SIGKILL);
    require(WIFSIGNALED(killed), "leader did not die by SIGKILL");
    for (int id = 11; id <= 20; ++id) tick(first, id, id % 2 == 1, id);
    const int status = first.finish();
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "submitter did not exit cleanly at end of input");

    // Paper: the operator sets limits for the fixture account (a fresh account
    // starts at risk version 1, position version 1, which the frames bind by
    // default): one lot per order and position. The clip is 1 lot at 11 ticks x
    // 100 micros = 1,100 micros notional, 1,111 micros held with a 1 % fee hold.
    // Two lots of reserve and 10,000 micros of holds would admit a second order,
    // so only the slot (and, where the kernel has it, the position limit) stops one.
    const auto limits = h.operator_command(4, {"risk-limits", "account=5", "instrument=4", "risk_policy_version=1",
        "expected_risk_version=0", "kill_epoch=1", "executor_generation=1", "max_order_lots=1", "max_reserved_lots=2",
        "max_order_notional_micros=10000", "max_reserved_exposure_micros=10000", "initial_margin_ppm=1000000",
        "maximum_fee_ppm=10000", "max_position_lots=1", "allow_short=0", "reason_code=7"}, h.await_leader(), 20000);
    require(limits.exit_code == 0 && limits.output.find("RISK-LIMITS") != std::string::npos &&
                word(limits.output.substr(limits.output.find("COMMITTED")), "risk_version") == "1",
            "risk limits were not configured: " + limits.output);
    auto paper_args = args;
    paper_args.insert(paper_args.end(), {"--mode", "paper"});
    const auto action = std::to_string(static_cast<int>(DecisionCode::action));
    const auto place = std::to_string(static_cast<int>(Directive::place_limit));
    const auto paper_tick = [&](Piped& p, int id, bool up, const std::string& extra = "") {
        std::ostringstream digest;
        digest << std::hex;
        for (int i = 0; i < 32; ++i) digest << (i == 0 ? "f" : "0") << (i == 31 ? (id % 16) : (i == 30 ? id / 16 % 16 : 0));
        p.send("TICK id=" + std::to_string(id) + " event_ns=" + std::to_string(2000 + id) + " receive_ns=" +
               std::to_string(2010 + id) + " age_ns=10 features=" + (up ? "50000" : "-50000") + ",0,0,0,11 evidence=" +
               digest.str() + extra);
        const auto line = p.line(std::chrono::seconds(30));
        require(line.rfind("DECISION ", 0) == 0 && word(line, "risk_version") == "1" &&
                    word(line, "position_version") == "1",
                "paper tick " + std::to_string(id) + " got: " + line);
        return line;
    };
    Piped second(paper_args, h.root / "submitter-2.log");
    const auto again = second.line(std::chrono::seconds(60));
    require(again.rfind("READY ", 0) == 0 && word(again, "generation") == "2" &&
                word(again, "first_sequence") == "10001" && word(again, "mode") == "paper",
            "restart did not continue in paper mode on generation 2 past the reservation: " + again + " " +
                read_file(h.root / "submitter-2.log"));
    // Up: the enter rule's buy, the slot empty and within every limit -> an order.
    const auto buy = paper_tick(second, 21, true);
    require(word(buy, "decision_code") == action && word(buy, "effective") == place &&
                word(buy, "authorization") == "applied",
            "the first paper buy was not authorized: " + buy);
    // Down, then up: the buy is still open (no gateway), so neither the exit's
    // sell nor another buy may become a second order on the slot.
    for (const auto& [id, up] : {std::pair{22, false}, std::pair{23, true}}) {
        const auto refused = paper_tick(second, id, up);
        const auto why = word(refused, "authorization");
        require(word(refused, "decision_code") == action && why != "applied" && why != "none" &&
                    (why == "order_slot_occupied" || why == "position_limit_exceeded"),
                "a second order on an occupied slot was not refused for that reason: " + refused);
    }
    // The edge's loss stop: with exits_only=1 the buy is not even asked for (the
    // kernel would have answered with a refusal code), while the exit still is.
    const auto withheld = paper_tick(second, 24, true, " exits_only=1");
    require(word(withheld, "decision_code") == action && word(withheld, "authorization") == "withheld",
            "a buy under the loss stop was sent for authorization: " + withheld);
    const auto exit_asked = paper_tick(second, 25, false, " exits_only=1");
    const auto exit_answer = word(exit_asked, "authorization");
    require(exit_answer != "withheld" && exit_answer != "none", "the loss stop withheld an exit: " + exit_asked);
    second.send("TICK id=26 event_ns=2026 receive_ns=2036 age_ns=10 features=50000,0,0,0,11 evidence=f" +
                std::string(63, '1') + " exits_only=yes");
    const auto unclear = second.line(std::chrono::seconds(30));
    require(unclear.rfind("ERROR id=26 reason=bad_tick", 0) == 0, "an unclear stop token was guessed: " + unclear);
    // Halt: the next frame is not actionable, so nothing is sent for authorization.
    const auto halted = h.operator_command(5, {"strategy-mode", "halted", "1", "1"}, h.await_leader(), 20000);
    require(halted.exit_code == 0, "halt refused: " + halted.output);
    const auto after_halt = paper_tick(second, 27, true);
    require(word(after_halt, "decision_code") == std::to_string(static_cast<int>(DecisionCode::control_halted)) &&
                word(after_halt, "authorization") == "none",
            "a halted cluster still offered a paper decision for authorization: " + after_halt);
    const int status2 = second.finish();
    require(WIFEXITED(status2) && WEXITSTATUS(status2) == 0, "restarted submitter did not exit cleanly");
    // The retained pools hold exactly the log above the snapshot.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // one health publish interval
    for (const int node : h.voters) {
        if (!h.pids.contains(node)) continue;
        const auto page = h.health(node);
        require(page && page->snapshot_index > 0 &&
                    page->retained_ordinary + page->retained_protected ==
                        static_cast<std::uint64_t>(page->current_index - page->snapshot_index),
                "node " + std::to_string(node) + " health page miscounts the retained log: ordinary " +
                    std::to_string(page ? page->retained_ordinary : 0) + " protected " +
                    std::to_string(page ? page->retained_protected : 0) + " current " +
                    std::to_string(page ? page->current_index : 0));
    }
    for (const int node : h.voters)
        if (h.pids.contains(node)) (void)h.stop(node, SIGTERM);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) {
        std::cerr << "usage: v2-node-process-tests NODE CLIENT STATUS [SHADOW_SUBMITTER]\n";
        return 64;
    }
    auto pattern = (fs::temp_directory_path() / "v2-node-process-XXXXXX").string();
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    if (::mkdtemp(bytes.data()) == nullptr) return 1;
    const fs::path root = fs::canonical(bytes.data());
    int status = 0;
    try {
        Harness h;
        h.node_binary = argv[1];
        h.client_binary = argv[2];
        h.status_binary = argv[3];
        h.root = root;
        h.base = static_cast<std::uint16_t>(42000 + (::getpid() % 400) * 50);
        for (const int who : {0, 1, 2}) {
            fs::create_directories(h.keys(who));
            fs::permissions(h.keys(who), fs::perms::owner_all, fs::perm_options::replace);
            if (who != 0) write_secret(h.keys(who) / "link-1-2.key", 0x21);
            write_secret(h.keys(who) / "caller.key", 0x61);
            write_secret(h.keys(who) / "shadow.key", 0x71);
            write_secret(h.keys(who) / "strategy.key", 0x81);
        }
        write_catalog(root / "catalog.bin");

        Relay to1(h.relay_port(1), h.peer_port(1));
        Relay to2(h.relay_port(2), h.peer_port(2));

        scenario(h);

        // 3. Partition through the relays: fail-closed, then heal and commit.
        to1.partitioned = true;
        to2.partitioned = true;
        const auto isolated = h.client(3, 3, h.leader() ? h.leader() : 1, 3000);
        require(isolated.exit_code == 3, "commit succeeded across a partition: " + isolated.output);
        to1.partitioned = false;
        to2.partitioned = false;
        h.await_leader();
        require_committed(h.client(3, 3, h.leader(), 20000), 3, 3, "commit after heal");

        // 4. Replay: restart BOTH processes from disk; they recover and commit.
        for (const int node : {1, 2}) {
            const int killed = h.stop(node, SIGKILL);
            require(WIFSIGNALED(killed), "node did not die by SIGKILL");
        }
        h.start(1, "recover");
        h.start(2, "recover");
        const int recovered = h.await_leader();
        require_committed(h.client(4, 4, recovered, 20000), 4, 4, "commit after full restart");
        const auto h1 = h.health(1), h2 = h.health(2);
        require(h1 && h2, "health pages unreadable after restart");

        // 4b. Live rotation of the link and caller secrets, no restart. Each
        //     voter moves at its own moment; a commit follows every step.
        std::uint64_t next_command = 5;
        const auto commit_now = [&](const std::string& step) {
            require_committed(h.client(next_command, next_command, h.await_leader(), 20000), next_command,
                              next_command, "rotation: " + step);
            ++next_command;
        };
        for (const int node : {1, 2}) {
            write_secret(h.keys(node) / "link-1-2.key.next", 0x41);
            write_secret(h.keys(node) / "caller.key.next", 0x81);
            require(h.reload(node) == "applied", "node " + std::to_string(node) + " refused the new secrets");
            commit_now("node " + std::to_string(node) + " accepts new secrets");
        }
        // The client now signs with the new caller secret, still accepting the old.
        write_secret(h.keys(0) / "caller.key.previous", 0x61);
        write_secret(h.keys(0) / "caller.key", 0x81);
        commit_now("client signs with the new secret");
        for (const int node : {1, 2}) {
            write_secret(h.keys(node) / "link-1-2.key.previous", 0x21);
            write_secret(h.keys(node) / "link-1-2.key", 0x41);
            write_secret(h.keys(node) / "caller.key.previous", 0x61);
            write_secret(h.keys(node) / "caller.key", 0x81);
            fs::remove(h.keys(node) / "link-1-2.key.next");
            fs::remove(h.keys(node) / "caller.key.next");
            require(h.reload(node) == "applied", "node " + std::to_string(node) + " refused promotion");
            commit_now("node " + std::to_string(node) + " promoted");
        }
        for (const int who : {0, 1, 2}) {
            fs::remove(h.keys(who) / "caller.key.previous");
            if (who != 0) fs::remove(h.keys(who) / "link-1-2.key.previous");
        }
        for (const int node : {1, 2}) require(h.reload(node) == "applied", "retiring the old secrets was refused");
        commit_now("old secrets retired");

        // A torn push is refused and the node keeps its working keys.
        write_secret(h.keys(1) / "link-1-2.key.next", 0x55, 31);
        require(h.reload(1) == "refused", "a torn secret push was applied");
        commit_now("after a refused push");
        fs::remove(h.keys(1) / "link-1-2.key.next");

        std::this_thread::sleep_for(std::chrono::milliseconds(300));  // one health publish interval
        for (const int node : {1, 2}) {
            const auto page = h.health(node);
            require(page && page->auth_refusals == 0, "rotation caused authentication refusals on node " +
                                                          std::to_string(node));
            require(page->secret_reloads == 3 && page->secret_reload_failures == (node == 1 ? 1U : 0U),
                    "unexpected reload counts on node " + std::to_string(node));
            // Every command this scenario commits is an operator kill-epoch,
            // which draws on the protected reserve; nothing is compacted, so
            // that reserve holds the whole log and the ordinary pool nothing.
            require(page->current_index > 0 && page->current_index <= 64 && page->retained_ordinary == 0 &&
                        page->retained_protected == static_cast<std::uint64_t>(page->current_index),
                    "the health page does not account for the retained log on node " + std::to_string(node) +
                        ": ordinary " + std::to_string(page->retained_ordinary) + " protected " +
                        std::to_string(page->retained_protected) + " current " +
                        std::to_string(page->current_index));
        }

        // Status tool reads the same page.
        const auto status_log = root / "status.log";
        const auto status_pid = h.spawn({h.status_binary, (h.data(recovered) / "health.page").string()}, status_log);
        int status_code = 0;
        ::waitpid(status_pid, &status_code, 0);
        require(WIFEXITED(status_code) && WEXITSTATUS(status_code) == 0 &&
                    read_file(status_log).find("role=leader") != std::string::npos,
                "status tool did not report the leader: " + read_file(status_log));

        // 5. Graceful stop.
        for (const int node : {1, 2}) {
            const auto stopped = h.stop(node, SIGTERM);
            require(WIFEXITED(stopped) && WEXITSTATUS(stopped) == 0, "node did not exit cleanly on SIGTERM");
        }
        require(read_file(h.log_of(1)).find("STOPPED node=1") != std::string::npos, "node 1 did not report STOPPED");

        // 6. Three voters, on the same ports now that the two-voter nodes are gone.
        Harness three;
        three.node_binary = h.node_binary;
        three.client_binary = h.client_binary;
        three.status_binary = h.status_binary;
        three.root = root / "three";
        three.base = h.base;
        three.voters = {1, 2, 3};
        three.relayed = false;
        fs::create_directories(three.root);
        three_voter_scenario(three);

        // 7. The shadow submitter end to end (when its binary is given).
        if (argc == 5) {
            Harness shadow;
            shadow.node_binary = h.node_binary;
            shadow.client_binary = h.client_binary;
            shadow.status_binary = h.status_binary;
            shadow.root = root / "shadow";
            shadow.base = h.base;
            shadow.voters = {1, 2, 3};
            shadow.relayed = false;
            shadow.compact_at_entries = 16;
            fs::create_directories(shadow.root);
            shadow_submitter_scenario(shadow, argv[4]);
        }
        std::cout << "node process tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "node process tests failed: " << error.what() << '\n';
        status = 1;
    }
    std::error_code ignored;
    if (status == 0 && !std::getenv("VELOCITA_KEEP_EVIDENCE")) fs::remove_all(root, ignored);
    else std::cerr << "evidence kept in " << root << '\n';
    return status;
}
