#include "velocita/tape/voter.hpp"

#include "velocita/tape/peer_codec.hpp"

#include "velocita/v2/digest.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace velocita::tape {
namespace {

void put(std::vector<std::byte>& out, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) out.push_back(std::byte((v >> (8 * i)) & 0xFF));
}

std::uint64_t get(const std::byte* p, int n) {
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) v |= std::uint64_t(std::to_integer<std::uint8_t>(p[i])) << (8 * i);
    return v;
}

void atomic_write(const std::filesystem::path& path, const std::string& text) {
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
    }
    std::filesystem::rename(tmp, path);
}

std::string atoms_text(std::int64_t v) {
    const auto whole = v / kAtomScale, frac = v % kAtomScale;
    auto f = std::to_string(frac);
    f = std::string(9 - f.size(), '0') + f;
    while (f.size() > 1 && f.back() == '0') f.pop_back();
    return std::to_string(whole) + "." + f;
}

}

std::vector<std::byte> encode_ack(std::uint8_t recorder, std::uint64_t session, std::uint64_t applied) {
    std::vector<std::byte> out;
    put(out, recorder, 1);
    put(out, session, 8);
    put(out, applied, 8);
    return out;
}

bool decode_ack(std::span<const std::byte> bytes, std::uint8_t& recorder, std::uint64_t& session, std::uint64_t& applied) {
    if (bytes.size() != 17) return false;
    recorder = std::to_integer<std::uint8_t>(bytes[0]);
    session = get(bytes.data() + 1, 8);
    applied = get(bytes.data() + 9, 8);
    return recorder >= 1 && recorder <= kRecorders;
}

Voter::Voter(VoterConfig config, Network& network)
    : config_(std::move(config)), network_(network),
      self_{Role::voter, static_cast<std::uint8_t>(config_.replica.node_id)},
      sealer_(config_.master, config_.incarnation, self_, config_.boot) {
    if (!config_.signing_key.empty()) signing_ = SigningKey::load(config_.signing_key);
    for (std::size_t i = 0; i < kVoters; ++i)
        if (!config_.public_keys[i].empty()) verify_[i] = VerifyKey::load(config_.public_keys[i]);
    std::filesystem::create_directories(config_.replica.data_dir / "heads");
    replica_ = std::make_unique<TapeReplica>(
        config_.replica,
        [this](const PeerMessage& m) {
            const auto bytes = encode_peer(m);
            const Party to{Role::voter, static_cast<std::uint8_t>(m.recipient_id)};
            network_.send(to, sealer_.seal_to(to, FrameKind::peer, bytes));
        },
        [this](std::uint64_t index, std::span<const std::byte> bytes, const ApplyResult& result) {
            on_applied(index, bytes, result);
        });
}

void Voter::broadcast_submission(std::span<const std::byte> bytes) {
    for (std::uint8_t v = 1; v <= kVoters; ++v) {
        if (v == self_.id) continue;
        const Party to{Role::voter, v};
        network_.send(to, sealer_.seal_to(to, FrameKind::submit, bytes));
    }
    handle_submission(self_, std::vector<std::byte>(bytes.begin(), bytes.end()));
}

void Voter::handle_submission(const Party& from, std::vector<std::byte> bytes) {
    const auto entry = decode_entry(bytes);
    if (!entry) {
        ++refused_;
        return;
    }
    if (from.role == Role::recorder) {
        const RecorderHeader* h = nullptr;
        if (const auto* o = std::get_if<Observation>(&*entry)) h = &o->header;
        else if (const auto* p = std::get_if<ObservationPart>(&*entry)) h = &p->header;
        else if (const auto* e = std::get_if<RecorderEvent>(&*entry)) h = &e->header;
        if (!h || h->recorder != from.id) {
            ++refused_;
            return;
        }
        if (!replica_->is_leader()) return;
        if (replica_->request_in_log({h->recorder, h->session, h->request_seq}) ||
            h->request_seq <= replica_->highest_request_in_log(h->recorder, h->session))
            return;
        auto& queue = pending_[h->recorder - 1];
        if (queue.size() >= config_.pending_per_recorder) {
            ++refused_;
            return;
        }
        queue[{h->session, h->request_seq}] = std::move(bytes);
        return;
    }
    const auto* head = std::get_if<HeadSignature>(&*entry);
    if (!head || head->voter != from.id) {
        ++refused_;
        return;
    }
    if (!replica_->is_leader() || replica_->head_signature_in_log(head->tick, head->voter)) return;
    if (const auto* h = find_head(replica_->state(), head->tick); h && h->signed_by[head->voter - 1]) return;
    pending_heads_[{head->tick, head->voter}] = std::move(bytes);
}

void Voter::drain_pending() {
    if (!replica_->is_leader()) {
        for (auto& q : pending_) q.clear();
        pending_heads_.clear();
        return;
    }
    for (std::uint8_t r = 1; r <= kRecorders; ++r) {
        auto& queue = pending_[r - 1];
        const auto& session = replica_->state().sessions[r - 1];
        std::set<std::uint64_t> sessions;
        for (const auto& [key, bytes] : queue) sessions.insert(key.first);
        for (const auto s : sessions) {
            const bool known = (session.started && session.session == s) || replica_->request_in_log({r, s, 1});
            if (!known) {
                const auto it = queue.find({s, 1});
                if (it == queue.end()) continue;
                if (replica_->propose(it->second) != ProposeStatus::proposed) return;
                queue.erase(it);
            }
            auto next = replica_->highest_request_in_log(r, s) + 1;
            while (true) {
                const auto it = queue.find({s, next});
                if (it == queue.end()) break;
                if (replica_->propose(it->second) != ProposeStatus::proposed) return;
                queue.erase(it);
                ++next;
            }
            for (auto it = queue.begin(); it != queue.end();) {
                if (it->first.first == s && it->first.second < next) it = queue.erase(it);
                else ++it;
            }
        }
    }
    for (auto it = pending_heads_.begin(); it != pending_heads_.end();) {
        if (!replica_->head_signature_in_log(it->first.first, it->first.second) &&
            replica_->propose(it->second) != ProposeStatus::proposed)
            return;
        it = pending_heads_.erase(it);
    }
}

void Voter::on_applied(std::uint64_t, std::span<const std::byte>, const ApplyResult& result) {
    if (result.head_recorded && signing_) heads_to_sign_.push_back(replica_->state().last_tick);
    if (result.decoded && result.type == EntryType::head_signature && result.code == ApplyCode::applied) {
        for (const auto& head : replica_->state().heads)
            if (head.used) heads_to_check_.push_back(head.tick);
    }
}

void Voter::publish_head(std::uint64_t tick) {
    const auto* head = find_head(replica_->state(), tick);
    if (!head) return;
    const auto message = head_message(config_.incarnation, head->tick, head->tree_size, head->root);
    std::size_t valid = 0;
    HeadRecord record{head->tick, head->tree_size, head->root, {}};
    for (std::size_t v = 0; v < kVoters; ++v) {
        if (!head->signed_by[v] || !verify_[v]) continue;
        if (verify_[v]->verify(message, head->signatures[v])) {
            record.signed_by[v] = true;
            ++valid;
        }
    }
    const auto already = published_ticks_.find(tick);
    if (valid < 2 || (already != published_ticks_.end() && valid <= already->second)) return;
    std::ostringstream json;
    json << "{\"incarnation\":" << config_.incarnation << ",\"tick\":" << head->tick << ",\"tree_size\":"
         << head->tree_size << ",\"root\":\"" << v2::to_hex(head->root) << "\",\"signatures\":{";
    bool first = true;
    for (std::size_t v = 0; v < kVoters; ++v) {
        if (!record.signed_by[v]) continue;
        const Digest a = [&] {
            Digest d;
            std::memcpy(d.bytes.data(), head->signatures[v].data(), 32);
            return d;
        }();
        const Digest b = [&] {
            Digest d;
            std::memcpy(d.bytes.data(), head->signatures[v].data() + 32, 32);
            return d;
        }();
        json << (first ? "" : ",") << "\"" << (v + 1) << "\":\"" << v2::to_hex(a) << v2::to_hex(b) << "\"";
        first = false;
    }
    json << "}}\n";
    atomic_write(config_.replica.data_dir / "heads" / ("head-" + std::to_string(head->tick) + ".json"), json.str());
    if (already == published_ticks_.end()) {
        published_ticks_.emplace(tick, valid);
        published_.push_back(record);
        return;
    }
    already->second = valid;
    for (auto& r : published_)
        if (r.tick == tick) r = record;
}

void Voter::after_apply() {
    for (const auto tick : heads_to_sign_) {
        const auto* head = find_head(replica_->state(), tick);
        if (!head || !signing_) continue;
        HeadSignature sig;
        sig.tick = head->tick;
        sig.tree_size = head->tree_size;
        sig.root = head->root;
        sig.voter = self_.id;
        sig.signature = signing_->sign(head_message(config_.incarnation, head->tick, head->tree_size, head->root));
        std::vector<std::byte> bytes(kMaxEntryBytes);
        bytes.resize(encode_entry(sig, bytes));
        broadcast_submission(bytes);
    }
    heads_to_sign_.clear();
    for (const auto tick : heads_to_check_) publish_head(tick);
    heads_to_check_.clear();
}

void Voter::send_acks() {
    for (std::uint8_t r = 1; r <= kRecorders; ++r) {
        const auto& s = replica_->state().sessions[r - 1];
        if (!s.started) continue;
        const Party to{Role::recorder, r};
        network_.send(to, sealer_.seal_to(to, FrameKind::ack, encode_ack(r, s.session, s.last_request)));
    }
}

void Voter::write_state_page(std::int64_t utc_ns) const {
    const auto st = replica_->status();
    const auto& s = replica_->state();
    std::ostringstream j;
    j << "{\"node\":" << st.node_id << ",\"utc_ns\":" << utc_ns << ",\"leader\":" << st.leader_id
      << ",\"is_leader\":" << (st.leader ? "true" : "false") << ",\"term\":" << st.term
      << ",\"current_index\":" << st.current_index << ",\"commit_index\":" << st.commit_index
      << ",\"applied_index\":" << st.applied_index << ",\"synced_index\":" << st.synced_index
      << ",\"wal_bytes\":" << st.wal_bytes << ",\"last_tick\":" << s.last_tick
      << ",\"tree_size\":" << s.frontier.size << ",\"root\":\"" << v2::to_hex(s.frontier.root()) << "\""
      << ",\"published_heads\":" << published_.size() << ",\"last_published_tick\":"
      << (published_.empty() ? 0 : published_.back().tick) << ",\"rejected_frames\":" << rejected_
      << ",\"refused_submissions\":" << refused_;
    const auto& c = s.counters;
    j << ",\"counters\":{\"entries\":" << c.entries << ",\"late_copies\":" << c.late_copies
      << ",\"late_divergences\":" << c.late_divergences << ",\"duplicate_copies\":" << c.duplicate_copies
      << ",\"heartbeats\":" << c.heartbeats << ",\"skew_samples\":" << c.skew_samples << ",\"skew_max_ns\":"
      << c.skew_max_ns;
    const char* outcomes[] = {"", "agree_3_of_3", "agree_2_of_3", "only_1_of_3", "divergent"};
    j << ",\"update_outcomes\":{";
    for (int i = 1; i <= 4; ++i) j << (i > 1 ? "," : "") << "\"" << outcomes[i] << "\":" << c.update_outcomes[i];
    j << "},\"all_outcomes\":{";
    for (int i = 1; i <= 4; ++i) j << (i > 1 ? "," : "") << "\"" << outcomes[i] << "\":" << c.all_outcomes[i];
    j << "},\"close_reasons\":{\"all_reported\":" << c.close_reasons[1] << ",\"n_ticks\":" << c.close_reasons[2]
      << ",\"capacity\":" << c.close_reasons[3] << "},\"skew_buckets\":[";
    for (std::size_t i = 0; i < c.skew_buckets.size(); ++i) j << (i ? "," : "") << c.skew_buckets[i];
    j << "],\"flags\":[";
    for (std::size_t i = 0; i < 9; ++i) j << (i ? "," : "") << c.flags[i];
    j << "],\"unknown_reasons\":{";
    for (std::size_t i = 1; i <= 8; ++i)
        j << (i > 1 ? "," : "") << "\"" << to_string(static_cast<Reason>(i)) << "\":" << c.unknown_reasons[i];
    j << "},\"events\":[";
    for (std::size_t r = 0; r < kRecorders; ++r) {
        j << (r ? "," : "") << "{";
        for (std::size_t k = 1; k <= 8; ++k)
            j << (k > 1 ? "," : "") << "\"" << to_string(static_cast<EventKind>(k)) << "\":" << c.events[r][k];
        j << "}";
    }
    j << "]},\"sessions\":[";
    for (std::size_t r = 0; r < kRecorders; ++r)
        j << (r ? "," : "") << "{\"started\":" << (s.sessions[r].started ? "true" : "false") << ",\"session\":"
          << s.sessions[r].session << ",\"last_request\":" << s.sessions[r].last_request << "}";
    j << "],\"books\":[";
    for (std::uint8_t i = 0; i < s.symbol_count; ++i) {
        const auto& b = s.books[i];
        j << (i ? "," : "") << "{\"symbol\":\"" << s.symbols[i].view() << "\",\"state\":\"" << to_string(b.state)
          << "\",\"reason\":\"" << to_string(b.reason) << "\",\"position\":" << b.position << ",\"bids\":"
          << b.bids.size << ",\"asks\":" << b.asks.size << ",\"hash\":\""
          << (b.hash.is_zero() ? "" : v2::to_hex(b.hash)) << "\"";
        if (b.state == BookState::valid && b.bids.size && b.asks.size)
            j << ",\"best_bid\":\"" << atoms_text(b.bids.levels[0].price) << "\",\"best_ask\":\""
              << atoms_text(b.asks.levels[0].price) << "\"";
        const auto silence = s.max_silence_ns[stream_id(Channel::depth, i)];
        j << ",\"max_silence_ns\":" << silence << "}";
    }
    j << "]}\n";
    atomic_write(config_.replica.data_dir / "state.json", j.str());

    std::ostringstream m;
    const auto node = std::to_string(st.node_id);
    const auto local = "node=\"" + node + "\",source=\"local\"";
    const auto log = "node=\"" + node + "\",source=\"log\"";
    m << "velocita_tape_info{" << local << ",incarnation=\"" << config_.incarnation << "\"} 1\n"
      << "velocita_tape_raft_term{" << local << "} " << st.term << "\n"
      << "velocita_tape_raft_is_leader{" << local << "} " << (st.leader ? 1 : 0) << "\n"
      << "velocita_tape_raft_leader_id{" << local << "} " << st.leader_id << "\n"
      << "velocita_tape_raft_commit_index{" << local << "} " << st.commit_index << "\n"
      << "velocita_tape_raft_applied_index{" << local << "} " << st.applied_index << "\n"
      << "velocita_tape_raft_synced_index{" << local << "} " << st.synced_index << "\n"
      << "velocita_tape_wal_bytes{" << local << "} " << st.wal_bytes << "\n"
      << "velocita_tape_published_heads{" << local << "} " << published_.size() << "\n"
      << "velocita_tape_last_published_tick{" << local << "} " << (published_.empty() ? 0 : published_.back().tick) << "\n"
      << "velocita_tape_rejected_frames_total{" << local << "} " << rejected_ << "\n"
      << "velocita_tape_refused_submissions_total{" << local << "} " << refused_ << "\n"
      << "velocita_tape_state_page_utc_seconds{" << local << "} " << utc_ns / 1'000'000'000 << "\n"
      << "velocita_tape_last_tick{" << log << "} " << s.last_tick << "\n"
      << "velocita_tape_tree_size{" << log << "} " << s.frontier.size << "\n"
      << "velocita_tape_entries_total{" << log << "} " << c.entries << "\n"
      << "velocita_tape_late_copies_total{" << log << "} " << c.late_copies << "\n"
      << "velocita_tape_late_divergences_total{" << log << "} " << c.late_divergences << "\n"
      << "velocita_tape_duplicate_copies_total{" << log << "} " << c.duplicate_copies << "\n"
      << "velocita_tape_heartbeats_total{" << log << "} " << c.heartbeats << "\n";
    for (int i = 1; i <= 4; ++i) {
        m << "velocita_tape_verdicts_total{" << log << ",kind=\"update\",outcome=\"" << outcomes[i] << "\"} "
          << c.update_outcomes[i] << "\n";
        m << "velocita_tape_verdicts_total{" << log << ",kind=\"all\",outcome=\"" << outcomes[i] << "\"} "
          << c.all_outcomes[i] << "\n";
    }
    const char* reasons[] = {"", "all_reported", "n_ticks", "capacity"};
    for (int i = 1; i <= 3; ++i)
        m << "velocita_tape_verdict_closes_total{" << log << ",reason=\"" << reasons[i] << "\"} " << c.close_reasons[i] << "\n";
    const char* bounds[] = {"0.0001", "0.001", "0.005", "0.01", "0.05", "0.1", "0.5", "1", "+Inf"};
    std::uint64_t cumulative = 0;
    for (std::size_t i = 0; i < c.skew_buckets.size(); ++i) {
        cumulative += c.skew_buckets[i];
        m << "velocita_tape_vantage_skew_seconds_bucket{" << log << ",le=\"" << bounds[i] << "\"} " << cumulative << "\n";
    }
    m << "velocita_tape_vantage_skew_seconds_count{" << log << "} " << c.skew_samples << "\n"
      << "velocita_tape_vantage_skew_max_seconds{" << log << "} " << static_cast<double>(c.skew_max_ns) / 1e9 << "\n";
    const char* flag_names[] = {"crossed", "locked", "ebbo_mismatch", "trade_outside_book", "trade_outside_band",
                                "snapshot_differs", "late_copy", "late_divergence", "stale_snapshot"};
    for (std::size_t i = 0; i < 9; ++i)
        m << "velocita_tape_integrity_flags_total{" << log << ",flag=\"" << flag_names[i] << "\"} " << c.flags[i] << "\n";
    for (std::size_t i = 1; i <= 8; ++i)
        m << "velocita_tape_book_unknown_total{" << log << ",reason=\"" << to_string(static_cast<Reason>(i)) << "\"} "
          << c.unknown_reasons[i] << "\n";
    for (std::size_t r = 0; r < kRecorders; ++r)
        for (std::size_t k = 1; k <= 8; ++k)
            m << "velocita_tape_recorder_events_total{" << log << ",recorder=\"" << (r + 1) << "\",event=\""
              << to_string(static_cast<EventKind>(k)) << "\"} " << c.events[r][k] << "\n";
    for (std::size_t r = 0; r < kRecorders; ++r)
        m << "velocita_tape_recorder_requests_total{" << log << ",recorder=\"" << (r + 1) << "\"} "
          << (s.sessions[r].started ? s.sessions[r].last_request : 0) << "\n";
    for (std::uint8_t i = 0; i < s.symbol_count; ++i) {
        const auto& b = s.books[i];
        const auto sym = std::string(s.symbols[i].view());
        for (const auto state : {BookState::valid, BookState::building, BookState::unknown})
            m << "velocita_tape_book_state{" << log << ",symbol=\"" << sym << "\",state=\"" << to_string(state) << "\"} "
              << (b.state == state ? 1 : 0) << "\n";
        m << "velocita_tape_book_max_silence_seconds{" << log << ",symbol=\"" << sym << "\"} "
          << static_cast<double>(s.max_silence_ns[stream_id(Channel::depth, i)]) / 1e9 << "\n";
        m << "velocita_tape_book_position{" << log << ",symbol=\"" << sym << "\"} " << b.position << "\n";
    }
    if (extra_metrics_) m << extra_metrics_();
    atomic_write(config_.replica.data_dir / "metrics.prom", m.str());
}

void Voter::step(std::uint64_t now_ms, std::int64_t utc_ns) {
    utc_ns_ = utc_ns;
    if (!started_) {
        started_ = true;
        last_step_ms_ = last_tick_ms_ = last_ack_ms_ = last_page_ms_ = now_ms;
    }
    std::deque<std::vector<std::byte>> frames;
    network_.poll(frames);
    for (auto& frame : frames) {
        auto opened = sealer_.open(frame);
        if (!opened) {
            ++rejected_;
            continue;
        }
        ++opened_;
        if (opened->kind == FrameKind::peer && opened->from.role == Role::voter) {
            const auto message = decode_peer(opened->payload);
            if (message && message->sender_id == opened->from.id) replica_->receive(*message);
            else ++rejected_;
        } else if (opened->kind == FrameKind::submit) {
            handle_submission(opened->from, std::move(opened->payload));
        }
    }
    const auto elapsed = static_cast<int>(now_ms - last_step_ms_);
    last_step_ms_ = now_ms;
    replica_->tick(elapsed);
    after_apply();
    if (replica_->is_leader() && now_ms - last_tick_ms_ >= config_.tick_ms) {
        last_tick_ms_ = now_ms;
        Tick t;
        t.number = replica_->highest_tick_in_log() + 1;
        t.proposer = self_.id;
        t.proposer_utc_ns = utc_ns;
        std::vector<std::byte> bytes(64);
        bytes.resize(encode_entry(t, bytes));
        (void)replica_->propose(bytes);
    } else if (!replica_->is_leader()) {
        last_tick_ms_ = now_ms;
    }
    if (config_.previous_segment && replica_->is_leader() && !replica_->state().segment_started) {
        const auto term = replica_->status().term;
        if (term != segment_proposed_term_) {
            std::vector<std::byte> bytes(128);
            bytes.resize(encode_entry(*config_.previous_segment, bytes));
            if (replica_->propose(bytes) == ProposeStatus::proposed) segment_proposed_term_ = term;
        }
    }
    drain_pending();
    if (now_ms - last_ack_ms_ >= config_.ack_ms) {
        last_ack_ms_ = now_ms;
        send_acks();
    }
    if (now_ms - last_page_ms_ >= config_.state_page_ms) {
        last_page_ms_ = now_ms;
        write_state_page(utc_ns);
    }
}

}
