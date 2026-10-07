#pragma once

#include "velocita/v2/paper_venue_store.hpp"

#include <iomanip>
#include <sstream>

namespace velocita::v2::paper {

inline std::string render_paper_report(const DurablePaperVenue& venue, const AccountId account,
                                      const std::uint32_t buy_bps, const std::uint32_t sell_bps,
                                      const double wall_seconds) {
    auto image = std::make_unique<PaperVenueImage>();
    venue.venue().capture(*image);
    const auto& origin = venue.origin();
    std::ostringstream out;
    out << std::setprecision(17) << "{\"version\":1,\"incarnation\":\"";
    for (const auto byte : origin.cluster_incarnation)
        out << std::hex << std::setw(2) << std::setfill('0') << std::to_integer<unsigned>(byte);
    out << std::dec << "\",\"account\":" << account.value()
        << ",\"instrument\":" << image->instrument_id.value()
        << ",\"venue\":" << image->venue_id.value()
        << ",\"funding\":" << origin.funding.atoms()
        << ",\"buy_bps\":" << buy_bps << ",\"sell_bps\":" << sell_bps
        << ",\"stamp\":" << wall_seconds << ",\"failed\":" << (venue.failed() ? "true" : "false")
        << ",\"cash\":" << image->balances.quote.atoms() << ",\"lots\":" << image->balances.base.atoms()
        << ",\"fees\":" << image->ledger.fee_micros_total
        << ",\"buy_volume\":" << image->ledger.volume_micros_buy
        << ",\"sell_volume\":" << image->ledger.volume_micros_sell
        << ",\"committed_through\":" << image->ledger.committed_through
        << ",\"last_fill_stamp\":" << image->ledger.last_fill_wall_seconds
        << ",\"pending_reports\":" << image->report_count << ",\"capacity\":" << kMaxPaperOrders
        << ",\"orders\":[";
    for (std::size_t i=0;i<image->order_count;++i) {
        const auto& order=image->orders[i];
        if(i) out << ',';
        out << "{\"id\":" << order.cluster_order_id.value() << ",\"side\":" << static_cast<int>(order.side)
            << ",\"price\":" << order.price.atoms() << ",\"lots\":" << order.quantity.atoms()
            << ",\"notional\":" << order.notional.atoms() << ",\"fee\":" << order.fee.atoms() << '}';
    }
    out << "]}\n";
    return out.str();
}

}
