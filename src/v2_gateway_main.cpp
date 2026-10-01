// velocita-v2-gateway: one venue's execution gateway, a separate process from
// every voter. It reads committed state, obtains a one-shot authority proof per
// risk-increasing send, delivers to the venue, and submits what the venue
// reports as gateway-caller commands. It never changes a position itself.
//
//   velocita-v2-gateway --caller-key-id N --caller-id N --key-file FILE --incarnation HEX32
//       --catalog FILE --session-file FILE --state-file FILE --response-endpoint HOST:PORT
//       --node ID=HOST:PORT [--node ...] --aeron-dir DIR --venue paper|simulated|truemarkets
//       --metrics-textfile PATH [--arm-live-venue --signer-endpoint PATH]
//       [--freshness-ms N --allow-new-risk] [--catalog-root HEX64]
//       [--catalog-row ACCOUNT,VENUE,INSTRUMENT,SPEC,CAPS] [--lease-ms N] [--timeout-ms N]
//       [--state-interval-ms N] [--buy-fee-bps N] [--sell-fee-bps N] [--rearm-quiet-ms N]
//       [--paper-state-file FILE [--paper-state-init --paper-quote-usd N
//                                 | --paper-adopt-position --paper-adopt-cost-micros N --paper-quote-usd N]]
//
// The catalog root comes from the cluster's committed state (or must equal
// --catalog-root); the row defaults to the one the active executable policy
// binds. New risk needs BOTH --allow-new-risk and a nonzero --freshness-ms;
// otherwise it only reconciles and cancels. New risk arms only at a committed
// read where the venue's book matches the kernel's position (zero lots), with
// no order open. A fence (term or leader change, timed-out read) re-arms after
// --rearm-quiet-ms (default 30000) through the same check; any other stop
// (unknown outcome, conflict, refused evidence, control change, mismatch)
// holds until a restart.
//
// --venue paper needs --paper-state-file: the venue's balances, orders and
// owned reports, rewritten durably before every answer. A missing file is
// refused; the FIRST start creates it with exactly one of
//   --paper-state-init      a flat account funded with --paper-quote-usd
//                           (refused unless the kernel is flat on the row);
//   --paper-adopt-position  base = the kernel's committed position, quote =
//                           --paper-quote-usd minus --paper-adopt-cost-micros
//                           (the position's cost, fees included);
// then exits 0 (PAPER_STATE line) without trading. Both refuse an existing file.
// stdout: STOP cause=..., ARMED/REARMED ..., RECONCILIATION mismatch ... lines.
// --venue truemarkets needs --arm-live-venue AND --signer-endpoint, and is then
// still refused because no build compiles a live venue transport (ADR 0019).
// stdout: READY ..., then NEW_RISK stopped=0|1 on every change.
// Exit: 0 on SIGINT/SIGTERM, 2 runtime failure, 64 usage.

#include "velocita/aeron_runtime.hpp"
#include "velocita/v2/aeron_node_transport.hpp"
#include "velocita/v2/gateway_loop.hpp"
#include "velocita/v2/node_files.hpp"
#include "velocita/v2/paper_venue_store.hpp"
#include "velocita/v2/shadow_submitter.hpp"
#include "velocita/v2/truemarkets_venue.hpp"
#include "velocita/v2/venue_simulator.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <sys/random.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace velocita::v2;

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true, std::memory_order_relaxed); }

struct Usage final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

constexpr std::uint64_t kReserve = 10'000;

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

struct Arguments {
    std::filesystem::path key_file, session_file, state_file, aeron_dir, catalog, metrics_textfile;
    std::int32_t caller_key_id{};
    std::uint64_t caller_id{};
    std::array<std::byte, 16> incarnation{};
    bool have_incarnation{};
    Digest256 catalog_root{};
    bool have_root{};
    Endpoint response_endpoint;
    std::map<std::int32_t, Endpoint> nodes;
    std::vector<std::int32_t> order;
    std::string venue;
    std::optional<std::array<std::uint64_t, 5>> row;  // account, venue, instrument, spec version, capabilities version
    std::uint64_t freshness_ms{};  // zero (the ADR 0013 default): never new risk
    std::uint64_t lease_ms{2000}, timeout_ms{3000}, state_interval_ms{250};
    std::uint64_t buy_fee_bps{115}, sell_fee_bps{120};
    std::optional<std::uint64_t> paper_quote_usd;
    std::optional<std::uint64_t> paper_adopt_cost_micros;
    std::filesystem::path paper_state_file;
    bool paper_state_init{}, paper_adopt_position{};
    std::uint64_t rearm_quiet_ms{30'000};
    bool allow_new_risk{};
    bool arm_live_venue{};
    std::string signer_endpoint;
};

// Every paper-state refusal that needs no cluster happens here, before any
// key file, Aeron directory or network is touched. A torn or corrupt file is
// refused (exit 64), never replaced: replacing it would reset the balances.
void check_paper_state(const Arguments& a) {
    const bool bootstrap = a.paper_state_init || a.paper_adopt_position;
    if (a.venue != "paper") {
        if (!a.paper_state_file.empty() || bootstrap || a.paper_quote_usd || a.paper_adopt_cost_micros)
            throw Usage("--paper-* options apply only to --venue paper");
        return;
    }
    if (a.paper_state_file.empty()) throw Usage("--venue paper needs --paper-state-file");
    if (a.paper_state_init && a.paper_adopt_position)
        throw Usage("--paper-state-init and --paper-adopt-position are exclusive");
    if (a.paper_adopt_position != a.paper_adopt_cost_micros.has_value())
        throw Usage("--paper-adopt-position needs --paper-adopt-cost-micros, and only it takes one");
    if (bootstrap != a.paper_quote_usd.has_value())
        throw Usage(bootstrap ? "the first start needs --paper-quote-usd (the account's funding)"
                              : "--paper-quote-usd applies only to the first start; the balance is in the paper "
                                "state file. Remove it (runbook section 5)");
    const auto existing = paper::load_paper_state(a.paper_state_file);
    if (bootstrap) {
        if (existing.error != paper::PaperStateError::missing)
            throw Usage("paper state refused: --paper-state-init/--paper-adopt-position never replace a file (" +
                        std::string(paper::describe(existing.error == paper::PaperStateError::none
                                                        ? paper::PaperStateError::exists
                                                        : existing.error)) + ")");
        return;
    }
    if (existing.error == paper::PaperStateError::missing)
        throw Usage("paper state refused: " + std::string(paper::describe(existing.error)) +
                    "; the first start needs --paper-state-init or --paper-adopt-position (runbook section 5)");
    if (existing.error != paper::PaperStateError::none)
        throw Usage("paper state refused: " + std::string(paper::describe(existing.error)));
}

// Served-row orders that are not terminal: a bootstrap beside one could not
// say what the venue holds.
std::size_t unresolved_orders(const TradingState& state, const VerifiedExecutionCatalog& catalog,
                              const ExecutionCatalogRow& row) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < state.order_count; ++i) {
        const auto& order = state.orders[i];
        if (order.state == ClusterOrderState::filled || order.state == ClusterOrderState::cancelled ||
            order.state == ClusterOrderState::rejected)
            continue;
        const auto binding = materialize_bound_order_effect(state, order.effect_id, catalog);
        if (binding && (binding->effect.account_id != row.account_id ||
                        binding->effect.instrument_id != row.instrument.instrument_id))
            continue;
        ++count;
    }
    return count;
}

Arguments parse(int argc, char** argv) {
    Arguments a;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--allow-new-risk") {
            a.allow_new_risk = true;
            continue;
        }
        if (key == "--arm-live-venue") {
            a.arm_live_venue = true;
            continue;
        }
        if (key == "--paper-state-init") {
            a.paper_state_init = true;
            continue;
        }
        if (key == "--paper-adopt-position") {
            a.paper_adopt_position = true;
            continue;
        }
        if (key == "--paper-base-lots")
            throw Usage("--paper-base-lots is replaced by the persisted paper state: adopt the position once with "
                        "--paper-adopt-position (runbook section 5)");
        if (i + 1 >= argc) throw Usage("missing value for " + key);
        const std::string value = argv[++i];
        if (key == "--key-file") a.key_file = value;
        else if (key == "--session-file") a.session_file = value;
        else if (key == "--state-file") a.state_file = value;
        else if (key == "--aeron-dir") a.aeron_dir = value;
        else if (key == "--catalog") a.catalog = value;
        else if (key == "--metrics-textfile") a.metrics_textfile = value;
        else if (key == "--catalog-root") a.have_root = hex_bytes(value, a.catalog_root.bytes);
        else if (key == "--caller-key-id") a.caller_key_id = static_cast<std::int32_t>(number(value, "caller key id"));
        else if (key == "--caller-id") a.caller_id = number(value, "caller id");
        else if (key == "--incarnation") a.have_incarnation = hex_bytes(value, a.incarnation);
        else if (key == "--response-endpoint") a.response_endpoint = endpoint(value);
        else if (key == "--venue") a.venue = value;
        else if (key == "--signer-endpoint") a.signer_endpoint = value;
        else if (key == "--catalog-row") {
            std::array<std::uint64_t, 5> row{};
            std::size_t at = 0;
            for (std::size_t field = 0; field < row.size(); ++field) {
                const auto comma = value.find(',', at);
                if ((comma == std::string::npos) != (field + 1 == row.size())) throw Usage("--catalog-row needs A,V,I,S,C");
                row[field] = number(value.substr(at, comma - at), "catalog row field");
                at = comma + 1;
            }
            a.row = row;
        }
        else if (key == "--freshness-ms") a.freshness_ms = number(value, "freshness");
        else if (key == "--lease-ms") a.lease_ms = number(value, "lease");
        else if (key == "--timeout-ms") a.timeout_ms = number(value, "timeout");
        else if (key == "--state-interval-ms") a.state_interval_ms = number(value, "state interval");
        else if (key == "--buy-fee-bps") a.buy_fee_bps = number(value, "buy fee bps");
        else if (key == "--sell-fee-bps") a.sell_fee_bps = number(value, "sell fee bps");
        else if (key == "--paper-quote-usd") a.paper_quote_usd = number(value, "paper quote usd");
        else if (key == "--paper-adopt-cost-micros") a.paper_adopt_cost_micros = number(value, "adoption cost micros");
        else if (key == "--paper-state-file") a.paper_state_file = value;
        else if (key == "--rearm-quiet-ms") a.rearm_quiet_ms = number(value, "re-arm quiet period");
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
    // Checked before anything else so the refusal is the whole diagnostic and
    // no key file, Aeron directory or network is touched. Every build today
    // refuses here: none compiles a live transport (ADR 0019).
    if (a.venue == "truemarkets") {
        const auto refusal = truemarkets::evaluate_live_arming({a.arm_live_venue, a.signer_endpoint});
        if (refusal != truemarkets::LiveArmingRefusal::none)
            throw Usage("--venue truemarkets refused: " + std::string(truemarkets::describe(refusal)));
    }
    if (a.arm_live_venue || !a.signer_endpoint.empty()) {
        if (a.venue != "truemarkets") throw Usage("--arm-live-venue and --signer-endpoint apply only to --venue truemarkets");
    }
    check_paper_state(a);
    if (a.key_file.empty() || a.session_file.empty() || a.state_file.empty() || a.aeron_dir.empty() ||
        a.catalog.empty() || a.metrics_textfile.empty() || !a.have_incarnation || a.nodes.empty() ||
        a.response_endpoint.port == 0 || a.caller_key_id <= 0 || a.caller_id == 0 ||
        (a.venue != "simulated" && a.venue != "paper"))
        throw Usage("usage: see the header of v2_gateway_main.cpp");
    if (a.buy_fee_bps > 10'000 || a.sell_fee_bps > 10'000) throw Usage("fee bps above 10000");
    if (a.state_interval_ms == 0 || a.state_interval_ms >= a.lease_ms)
        throw Usage("--state-interval-ms must be positive and below --lease-ms");
    return a;
}

Tick monotonic_ms(void*) noexcept {
    timespec now{};
#ifdef CLOCK_BOOTTIME
    ::clock_gettime(CLOCK_BOOTTIME, &now);  // counts suspend: a sleeping host does not age proofs slowly
#else
    ::clock_gettime(CLOCK_MONOTONIC, &now);  // macOS: includes sleep
#endif
    return static_cast<Tick>(now.tv_sec) * 1000 + static_cast<Tick>(now.tv_nsec / 1'000'000);
}

double wall_seconds(void*) noexcept {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void write_metrics(const std::filesystem::path& file, const std::string& text) {
    const auto temporary = file.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << text;
        if (!out) throw std::runtime_error("metrics file write failed: " + temporary);
    }
    std::filesystem::rename(temporary, file);  // readers never see a torn page
}

// THE ONE PLACE A VENUE IS CONSTRUCTED. Everything the loop knows about a
// venue is the VenueTransport seam and the capability view returned here.
struct Venue {
    std::unique_ptr<sim::SimulatedVenue> simulated;
    std::unique_ptr<paper::DurablePaperVenue> paper;
    sim::VenueTransport transport{};
    sim::VenueBookSource book{};
    VenueCapabilityView capabilities{};
};

paper::PaperVenueConfig paper_config(const Arguments& a, const ExecutionCatalogRow& row) {
    paper::PaperVenueConfig config;
    config.spec = {row.instrument.venue_id, row.instrument.instrument_id, row.tick_quote_micros, row.lot_base_atoms,
                   row.base_decimals};
    config.profile = row.instrument;
    config.buy_fee_bps = static_cast<std::uint32_t>(a.buy_fee_bps);
    config.sell_fee_bps = static_cast<std::uint32_t>(a.sell_fee_bps);
    config.latency = 1;
    if (!paper::valid(config)) throw Usage("paper venue refused its configuration (spec/profile disagree?)");
    return config;
}

Venue make_venue(const Arguments& a, const ExecutionCatalogRow& row) {
    Venue venue;
    venue.capabilities.venue_id = row.capabilities.venue_id;
    venue.capabilities.capabilities_version = row.capabilities.venue_capabilities_version;
    if (a.venue == "simulated") {
        venue.simulated = std::make_unique<sim::SimulatedVenue>(1, sim::FaultPolicy::clean(), row.capabilities.venue_id);
        venue.transport = sim::simulated_venue_transport(*venue.simulated);
        venue.book = sim::simulated_venue_book(*venue.simulated);
        venue.capabilities.supports_post_only = true;
        venue.capabilities.supports_good_til_cancel = true;
        return venue;
    }
    auto opened = paper::DurablePaperVenue::open(a.paper_state_file, paper_config(a, row),
                                                 paper::PaperFaultPolicy::clean(), a.incarnation);
    if (opened.error != paper::PaperStateError::none)
        throw Usage("paper state refused: " + std::string(paper::describe(opened.error)));
    venue.paper = std::move(opened.venue);
    venue.transport = venue.paper->transport();
    venue.book = venue.paper->book();
    // Fill-or-kill only: no post-only, no resting orders.
    venue.capabilities.supports_post_only = false;
    venue.capabilities.supports_good_til_cancel = false;
    return venue;
}

// The row every effect this gateway sends must bind to; lookup is exact.
const ExecutionCatalogRow& venue_row(const VerifiedExecutionCatalog& catalog, const std::array<std::uint64_t, 5>& key) {
    const auto* row = catalog.lookup(AccountId{key[0]}, VenueId{key[1]}, InstrumentId{key[2]},
                                     InstrumentSpecVersion{key[3]}, VenueCapabilitiesVersion{key[4]});
    if (!row) throw Usage("--catalog-row names no row of the verified catalog");
    return *row;
}

// One journal line per stop (with its cause), arm, re-arm and reconciliation break.
void report_events(GatewayLoop& loop) {
    std::array<GatewayEvent, kMaxGatewayEvents> events{};
    const auto count = loop.drain_events(events);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& e = events[i];
        switch (e.kind) {
        case GatewayEventKind::stopped:
            std::cout << "STOP new_risk cause=" << describe(e.cause)
                      << " rearmable=" << (rearmable(e.cause) ? 1 : 0) << " term=" << e.term << std::endl;
            break;
        case GatewayEventKind::armed:
        case GatewayEventKind::rearmed:
            std::cout << (e.kind == GatewayEventKind::armed ? "ARMED" : "REARMED") << " new_risk term=" << e.term
                      << " applied_index=" << e.applied_index << " kernel_lots=" << e.kernel_lots
                      << " venue_lots=" << e.venue_lots << std::endl;
            break;
        case GatewayEventKind::reconciliation_mismatch:
            std::cout << "RECONCILIATION mismatch kernel_lots=" << e.kernel_lots << " venue_lots=" << e.venue_lots
                      << " unknown_order=" << e.order.value() << " term=" << e.term
                      << " applied_index=" << e.applied_index
                      << " (new risk stays stopped; resolve by the break workflow, never by editing a file)"
                      << std::endl;
            break;
        }
    }
}

int run(const Arguments& a) {
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
    std::uint64_t reserved = start.first_sequence + kReserve;

    // Fresh per process: a proof or state answer for any other process can
    // never correlate with this one's requests.
    std::array<std::byte, 16> process_incarnation{};
    if (::getentropy(process_incarnation.data(), process_incarnation.size()) != 0)
        throw std::runtime_error("entropy unavailable for the gateway process incarnation");

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
    GatewayLink link(epoch.epoch, process_incarnation, std::move(routes), transport);

    // One authenticated read of the leader's committed state pins the catalog
    // root and names the row the active executable policy binds.
    std::optional<LinkOutcome> first;
    for (int attempt = 0; attempt < 10 && !first && !g_stop.load(); ++attempt) {
        if (!link.request_state(monotonic_ms(nullptr), a.timeout_ms)) throw std::runtime_error("state request refused");
        while (!g_stop.load()) {
            auto outcome = link.pump(monotonic_ms(nullptr));
            if (outcome.status == LinkStatus::pending) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (outcome.status == LinkStatus::done) first = std::move(outcome);
            break;
        }
    }
    if (!first) throw std::runtime_error("no committed state from the cluster");
    const auto root = link.state().execution_catalog_root;
    if (root.is_zero()) throw Usage("the cluster has no execution catalog installed");
    if (a.have_root && a.catalog_root != root) throw Usage("--catalog-root differs from the cluster's catalog");
    const auto catalog = load_catalog_file(a.catalog, root);
    if (catalog.error != NodeFileError::none) throw Usage("catalog file refused against the cluster's root");
    std::array<std::uint64_t, 5> key{};
    if (a.row) {
        key = *a.row;
    } else {
        const auto& bundle = link.state().active_executable_policy;
        if (!bundle.present) throw Usage("no active executable policy; pass --catalog-row");
        const auto* policy = find_policy(link.state(), bundle.executable_policy.policy_id,
                                         bundle.executable_policy.policy_version);
        if (!policy) throw Usage("active executable policy has no policy record; pass --catalog-row");
        key = {policy->artifact.account_id.value(), bundle.instrument.venue_id.value(),
               bundle.instrument.instrument_id.value(), bundle.instrument.instrument_spec_version.value(),
               bundle.venue.venue_capabilities_version.value()};
    }
    const auto& row = venue_row(catalog.catalog, key);

    if (a.paper_state_init || a.paper_adopt_position) {
        const auto& state = link.state();
        std::int64_t held = 0;
        for (std::size_t i = 0; i < state.risk_account_count; ++i)
            if (state.risk_accounts[i].account_id == row.account_id &&
                state.risk_accounts[i].instrument_id == row.instrument.instrument_id)
                held = state.risk_accounts[i].position_quantity.atoms();
        if (const auto open = unresolved_orders(state, catalog.catalog, row); open != 0)
            throw Usage("paper bootstrap refused: " + std::to_string(open) +
                        " order(s) on the row are not terminal; resolve them first");
        const auto funding = MoneyMicros::from_atoms(static_cast<std::int64_t>(*a.paper_quote_usd) * 1'000'000);
        paper::PaperBootstrap bootstrap;
        if (a.paper_state_init) {
            if (held != 0)
                throw Usage("paper bootstrap refused: the kernel holds " + std::to_string(held) +
                            " lots on the row; use --paper-adopt-position");
            bootstrap = paper::fresh_paper_state(paper_config(a, row), a.incarnation, funding);
        } else {
            bootstrap = paper::adopted_paper_state(
                paper_config(a, row), a.incarnation, funding, QuantityLots::from_atoms(held),
                MoneyMicros::from_atoms(static_cast<std::int64_t>(*a.paper_adopt_cost_micros)),
                first->reading.applied_index, state.state_version);
        }
        if (!bootstrap.error.empty()) throw Usage("paper bootstrap refused: " + bootstrap.error);
        if (const auto error = paper::create_paper_state(a.paper_state_file, *bootstrap.state);
            error != paper::PaperStateError::none)
            throw Usage("paper bootstrap refused: " + std::string(paper::describe(error)));
        const auto& made = *bootstrap.state;
        std::cout << "PAPER_STATE created file=" << a.paper_state_file.string()
                  << " mode=" << (made.origin.adopted ? "adopted" : "fresh")
                  << " base_lots=" << made.venue.balances.base.atoms()
                  << " quote_micros=" << made.venue.balances.quote.atoms()
                  << " funding_micros=" << made.origin.funding.atoms()
                  << " cost_micros=" << made.origin.adopted_cost.atoms()
                  << " applied_index=" << first->reading.applied_index
                  << " state_version=" << state.state_version << std::endl;
        return 0;
    }

    auto venue = make_venue(a, row);
    GatewayLoopConfig config;
    config.caller = CallerId{a.caller_id};
    config.first_sequence = start.first_sequence;
    config.sequence_limit = reserved;
    config.venue = venue.capabilities;
    config.account = row.account_id;
    config.instrument = row.instrument.instrument_id;
    config.instrument_spec_version = row.instrument.instrument_spec_version;
    config.freshness_bound = a.freshness_ms;
    config.lease_bound = a.lease_ms;
    config.state_interval = a.state_interval_ms;
    config.request_timeout = a.timeout_ms;
    config.allow_new_risk = a.allow_new_risk;
    config.rearm_quiet = a.rearm_quiet_ms;
    GatewayLoop loop(config, link, catalog.catalog, venue.transport, {&monotonic_ms, &wall_seconds, nullptr},
                     venue.book);

    std::cout << "READY gateway caller=" << a.caller_id << " venue=" << a.venue << " row=" << key[0] << ',' << key[1] << ',' << key[2] << ',' << key[3] << ',' << key[4]
              << " first_sequence=" << start.first_sequence << " session_epoch=" << epoch.epoch
              << " allow_new_risk=" << (a.allow_new_risk ? 1 : 0) << " freshness_ms=" << a.freshness_ms << std::endl;
    std::optional<bool> reported_stop;
    Tick last_write = 0;
    while (!g_stop.load(std::memory_order_relaxed)) {
        loop.step();
        if (loop.next_sequence() + kReserve / 2 >= reserved) {
            const auto next = reserved + kReserve;
            if (const auto error = extend_reservation(a.state_file, a.incarnation, start.generation, next);
                !error.empty())
                throw std::runtime_error("reservation: " + error);
            reserved = next;
            loop.extend_sequence_limit(reserved);
        }
        report_events(loop);
        if (venue.paper && venue.paper->failed())
            throw std::runtime_error("paper state write failed; the venue answers nothing more (restart reloads "
                                     "the last durable state)");
        if (reported_stop != loop.new_risk_stopped()) {
            reported_stop = loop.new_risk_stopped();
            std::cout << "NEW_RISK stopped=" << (*reported_stop ? 1 : 0) << std::endl;
        }
        const auto now = monotonic_ms(nullptr);
        if (now >= last_write + 500) {
            write_metrics(a.metrics_textfile, render_gateway_metrics(loop.metrics()));
            last_write = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    write_metrics(a.metrics_textfile, render_gateway_metrics(loop.metrics()));
    std::cout << "STOPPED gateway" << std::endl;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    try {
        return run(parse(argc, argv));
    } catch (const Usage& usage) {
        std::cerr << "velocita-v2-gateway: " << usage.what() << '\n';
        return 64;
    } catch (const std::exception& error) {
        std::cerr << "velocita-v2-gateway FAILED: " << error.what() << '\n';
        return 2;
    }
}
