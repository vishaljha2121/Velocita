#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/kernel_codec.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>

using namespace velocita::v2;

std::vector<std::byte> slurp(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> out(raw.size());
    std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

const char* order_state(ClusterOrderState s) {
    switch (s) {
        case ClusterOrderState::dispatch_authorized: return "dispatch_authorized";
        case ClusterOrderState::acknowledged: return "acknowledged";
        case ClusterOrderState::live: return "live";
        case ClusterOrderState::partially_filled: return "partially_filled";
        case ClusterOrderState::cancel_pending: return "cancel_pending";
        case ClusterOrderState::replace_pending: return "replace_pending";
        case ClusterOrderState::submission_unknown: return "submission_unknown";
        case ClusterOrderState::reconciling: return "reconciling";
        case ClusterOrderState::filled: return "filled";
        case ClusterOrderState::cancelled: return "cancelled";
        case ClusterOrderState::rejected: return "rejected";
        case ClusterOrderState::conflict: return "conflict";
    }
    return "?";
}

int main(int argc, char** argv) {
    const auto catalog_bytes = slurp(argv[1]);
    auto state = std::make_unique<TradingState>();
    auto scratch = std::make_unique<ApplyScratch>();
    auto codec = std::make_unique<KernelCommandCodecScratch>();
    VerifiedExecutionCatalog catalog;
    bool have_catalog = false;
    std::uint64_t applied = 0;
    const std::uint64_t watch = std::strtoull(argv[2], nullptr, 10);
    for (int file = 3; file < argc; ++file) {
        const auto bytes = slurp(argv[file]);
        const std::byte magic[4] = {std::byte{'V'}, std::byte{'2'}, std::byte{'K'}, std::byte{'C'}};
        for (std::size_t at = 0; at + 12 <= bytes.size(); ++at) {
            if (std::memcmp(bytes.data() + at, magic, 4) != 0) continue;
            std::uint32_t payload = 0;
            std::memcpy(&payload, bytes.data() + at + 4, 4);
            const std::size_t total = kKernelCommandPayloadOffset + payload + kDigestBytes;
            Command command;
            if (at + total > bytes.size() ||
                decode_kernel_command({bytes.data() + at, total}, command, *codec).error != KernelCodecError::none)
                continue;
            if (const auto* install = std::get_if<InstallExecutionCatalog>(&command.body); install && !have_catalog) {
                have_catalog = VerifiedExecutionCatalog::decode(catalog_bytes, install->catalog_root, catalog) ==
                               ExecutionCatalogError::none;
                std::cout << "catalog decoded=" << have_catalog << "\n";
            }
            ApplyResult result;
            try {
                result = apply(*state, command, *scratch, have_catalog ? &catalog : nullptr);
            } catch (...) {
                std::cout << "apply threw at command " << applied << "\n";
                return 1;
            }
            ++applied;
            const bool gateway = command.identity.caller_id == CallerId{22};
            const VenueObservation* observation = nullptr;
            if (const auto* fill = std::get_if<ObserveFill>(&command.body)) observation = &fill->observation;
            if (const auto* event = std::get_if<ObserveOrderEvent>(&command.body)) observation = &event->observation;
            const bool watched = observation && observation->cluster_order_id.value() == watch;
            if ((gateway && result.code != ApplyCode::applied && result.code != ApplyCode::already_applied) || watched ||
                (std::holds_alternative<AuthorizeDecision>(command.body) && state->order_count != 0 &&
                 result.cluster_order_id().value() == watch)) {
                std::cout << "#" << applied << " caller=" << command.identity.caller_id.value() << " seq="
                          << command.identity.request_sequence.value() << " body=" << command.body.index()
                          << " code=" << static_cast<int>(result.code);
                if (observation) std::cout << " obs_kind=" << static_cast<int>(observation->kind) << " qty="
                                           << observation->quantity.atoms();
                for (std::size_t i = 0; i < state->order_count; ++i)
                    if (state->orders[i].cluster_order_id.value() == watch)
                        std::cout << " order_state=" << order_state(state->orders[i].state);
                std::cout << " orders=" << state->order_count << " position="
                          << (state->risk_account_count ? state->risk_accounts[0].position_quantity.atoms() : 0) << "\n";
            }
        }
    }
    std::cout << "applied " << applied << " commands; state_version " << state->state_version << " orders "
              << state->order_count << " receipts " << state->receipt_count << "\n";
    for (std::size_t i = 0; i < state->order_count; ++i)
        std::cout << "order " << state->orders[i].cluster_order_id.value() << " " << order_state(state->orders[i].state)
                  << "\n";
}
