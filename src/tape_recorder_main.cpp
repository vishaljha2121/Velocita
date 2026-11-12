// velocita-tape-recorder: records the venue's public feed and submits it to the tape voters.
#include "velocita/tape/config.hpp"
#include "velocita/tape/link.hpp"
#include "velocita/tape/recorder.hpp"
#include "velocita/tape/voter.hpp"

#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

using namespace velocita;
using namespace velocita::tape;

namespace {

std::atomic<bool> running{true};
void stop(int) { running = false; }

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream in(s);
    std::string part;
    while (std::getline(in, part, ','))
        if (!part.empty()) out.push_back(part);
    return out;
}

std::uint64_t request_of(std::span<const std::byte> bytes) {
    const auto e = decode_entry(bytes);
    if (!e) return 0;
    if (const auto* o = std::get_if<Observation>(&*e)) return o->header.request_seq;
    if (const auto* p = std::get_if<ObservationPart>(&*e)) return p->header.request_seq;
    if (const auto* v = std::get_if<RecorderEvent>(&*e)) return v->header.request_seq;
    return 0;
}

}

int main(int argc, char** argv) {
    std::string config_path, sink_path;
    std::uint64_t stall_after = 0, stall_ms = 0, run_ms = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) config_path = argv[++i];
        else if (a == "--sink" && i + 1 < argc) sink_path = argv[++i];
        else if (a == "--sink-stall-after" && i + 1 < argc) stall_after = std::stoull(argv[++i]);
        else if (a == "--sink-stall-ms" && i + 1 < argc) stall_ms = std::stoull(argv[++i]);
        else if (a == "--run-ms" && i + 1 < argc) run_ms = std::stoull(argv[++i]);
        else {
            std::cerr << "usage: velocita-tape-recorder --config FILE [--sink FILE [--sink-stall-after N --sink-stall-ms M]] [--run-ms MS]\n";
            return 64;
        }
    }
    if (config_path.empty()) {
        std::cerr << "velocita-tape-recorder: --config is required\n";
        return 64;
    }
    try {
        const auto kv = KvConfig::load(config_path);
        RecorderConfig c;
        c.recorder_id = static_cast<std::uint8_t>(kv.number("recorder_id"));
        if (c.recorder_id < 1 || c.recorder_id > 3) throw std::runtime_error("recorder_id must be 1, 2 or 3");
        c.url = kv.get("url");
        c.ca_file = kv.find("ca_file").value_or("");
        c.symbols = split(kv.get("symbols"));
        if (const auto ch = kv.find("channels")) c.channels = split(*ch);
        c.ring_capacity = kv.number("ring_capacity", 4096);
        c.unacked_capacity = kv.number("unacked_capacity", 4096);
        c.dead_after = std::chrono::milliseconds(kv.number("dead_after_ms", 30000));
        c.read_timeout = std::chrono::milliseconds(kv.number("read_timeout_ms", 500));
        c.backoff_start = std::chrono::milliseconds(kv.number("backoff_start_ms", 1000));
        c.backoff_cap = std::chrono::milliseconds(kv.number("backoff_cap_ms", 60000));
        c.refresh_every = std::chrono::milliseconds(kv.number("refresh_ms", 15 * 60 * 1000));
        c.resend_after = std::chrono::milliseconds(kv.number("resend_ms", 2000));
        c.send_bytes_per_second = kv.number("send_bytes_per_second", 32 * 1024);

        std::signal(SIGTERM, stop);
        std::signal(SIGINT, stop);
        const auto session = random_u64();
        Ring ring(c.ring_capacity);
        Reader reader(c, ring);

        if (!sink_path.empty()) {
            std::ofstream sink(sink_path, std::ios::binary | std::ios::trunc);
            std::uint64_t written = 0;
            std::vector<std::uint64_t> to_ack;
            Submitter submitter(c.recorder_id, session, ring, c.unacked_capacity, c.resend_after,
                                [&](std::span<const std::byte> entry) {
                                    const auto n = static_cast<std::uint32_t>(entry.size());
                                    char len[4] = {char(n & 0xFF), char((n >> 8) & 0xFF), char((n >> 16) & 0xFF), char(n >> 24)};
                                    sink.write(len, 4);
                                    sink.write(reinterpret_cast<const char*>(entry.data()), n);
                                    sink.flush();
                                    ++written;
                                    to_ack.push_back(request_of(entry));
                                },
                                std::size_t{1} << 40, std::size_t{1} << 40);
            reader.start();
            const auto start = std::chrono::steady_clock::now();
            std::optional<std::chrono::steady_clock::time_point> stall_until;
            while (running) {
                const auto now = std::chrono::steady_clock::now();
                if (run_ms && now - start >= std::chrono::milliseconds(run_ms)) break;
                submitter.step(now);
                if (stall_after && written >= stall_after && !stall_until) stall_until = now + std::chrono::milliseconds(stall_ms);
                const bool stalled = stall_until && now < *stall_until;
                if (!stalled) {
                    for (const auto r : to_ack) submitter.on_ack(session, r);
                    to_ack.clear();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            reader.stop();
            std::cerr << "frames " << reader.frames() << " lost " << reader.lost() << " requests "
                      << submitter.next_request() - 1 << "\n";
            return 0;
        }

        Sealer sealer(load_master_key(kv.get("master_key_file")), kv.number("incarnation"),
                      Party{Role::recorder, c.recorder_id}, random_u64());
        std::map<Party, Endpoint> voters;
        for (std::uint8_t v = 1; v <= 3; ++v) voters[{Role::voter, v}] = Endpoint::parse(kv.get("voter." + std::to_string(v)));
        AeronRuntime runtime(kv.get("aeron_dir"));
        AeronLink link(runtime, Endpoint::parse(kv.get("listen")), voters);
        Submitter submitter(c.recorder_id, session, ring, c.unacked_capacity, c.resend_after,
                            [&](std::span<const std::byte> entry) {
                                for (std::uint8_t v = 1; v <= 3; ++v) {
                                    const Party to{Role::voter, v};
                                    link.send(to, sealer.seal_to(to, FrameKind::submit, entry));
                                }
                            },
                            c.send_bytes_per_second);
        reader.start();
        std::cerr << "velocita-tape-recorder " << int(c.recorder_id) << " session " << session << " started\n";
        const auto start = std::chrono::steady_clock::now();
        auto last_report = start;
        while (running) {
            const auto now = std::chrono::steady_clock::now();
            if (run_ms && now - start >= std::chrono::milliseconds(run_ms)) break;
            std::deque<std::vector<std::byte>> frames;
            link.poll(frames);
            for (const auto& f : frames) {
                const auto o = sealer.open(f);
                std::uint8_t r{};
                std::uint64_t s{}, applied{};
                if (o && o->kind == FrameKind::ack && o->from.role == Role::voter && decode_ack(o->payload, r, s, applied) &&
                    r == c.recorder_id)
                    submitter.on_ack(s, applied);
            }
            submitter.step(now);
            if (now - last_report >= std::chrono::seconds(60)) {
                last_report = now;
                std::cerr << "recorder " << int(c.recorder_id) << " frames " << reader.frames() << " lost " << reader.lost()
                          << " submitted " << submitter.next_request() - 1 << " acked " << submitter.acked()
                          << " unacked " << submitter.unacked() << " ring " << ring.size() << "\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        reader.stop();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "velocita-tape-recorder: " << e.what() << "\n";
        return 70;
    }
}
