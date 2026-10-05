#include "velocita/v2/follower_ack.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using velocita::v2::FollowerAckTracker;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::int64_t kTerm = 5;
constexpr std::int32_t kLeader = 3;

void nothing_is_due_before_a_verified_append() {
    FollowerAckTracker t;
    require(!t.due(kTerm, kLeader, true, 100), "an ack was due with nothing verified");
    t.record(kTerm, kLeader, false, 0, 0);
    require(!t.due(kTerm, kLeader, true, 100), "a failed append made an ack due");
}

void an_ack_is_capped_by_both_the_verified_prefix_and_durability() {
    FollowerAckTracker t;
    t.record(kTerm, kLeader, true, 10, 7);
    require(!t.due(kTerm, kLeader, true, 7), "an ack repeated what was already reported");
    const auto partial = t.due(kTerm, kLeader, true, 9);
    require(partial && *partial == 9, "durable 9 of verified 10 was not acknowledged as 9");
    t.sent(9);
    require(!t.due(kTerm, kLeader, true, 9), "the same ack was due twice");
    const auto capped = t.due(kTerm, kLeader, true, 15);
    require(capped && *capped == 10, "an ack covered entries this leader never verified");
    t.sent(10);
    require(!t.due(kTerm, kLeader, true, 15), "an ack was due past the verified prefix");
}

void a_new_term_starts_from_nothing() {
    FollowerAckTracker t;
    t.record(kTerm, kLeader, true, 10, 0);
    require(!t.due(kTerm + 1, kLeader, true, 10), "a term-5 prefix was acknowledged in term 6");
    require(!t.due(kTerm + 1, 4, true, 10), "a term-5 prefix was offered to a term-6 leader");
    t.record(kTerm + 1, 4, true, 3, 0);
    const auto fresh = t.due(kTerm + 1, 4, true, 10);
    require(fresh && *fresh == 3, "the new term's verified prefix was not the bound");
    require(!t.due(kTerm, kLeader, true, 10), "an ack was offered for a term already left");
}

void a_different_leader_in_the_same_term_is_not_trusted() {
    FollowerAckTracker t;
    t.record(kTerm, kLeader, true, 10, 0);
    require(!t.due(kTerm, kLeader + 1, true, 10), "one leader's verified prefix was offered to another");
}

void only_a_follower_acknowledges() {
    FollowerAckTracker t;
    t.record(kTerm, kLeader, true, 10, 0);
    require(!t.due(kTerm, kLeader, false, 10), "a candidate or leader sent a follower ack");
}

void a_stale_packet_never_lowers_the_verified_prefix() {
    FollowerAckTracker t;
    t.record(kTerm, kLeader, true, 10, 0);
    t.record(kTerm, kLeader, true, 6, 6);
    const auto due = t.due(kTerm, kLeader, true, 10);
    require(due && *due == 10, "a reordered packet shrank the verified prefix");
}

void a_normal_reply_counts_as_reported() {
    FollowerAckTracker t;
    t.record(kTerm, kLeader, true, 10, 10);
    require(!t.due(kTerm, kLeader, true, 10), "an early ack duplicated a complete normal reply");
    t.record(kTerm, kLeader, true, 12, 11);
    const auto due = t.due(kTerm, kLeader, true, 12);
    require(due && *due == 12, "the remaining durable entry was not acknowledged");
}

void a_failed_append_silences_early_acks_until_the_next_success() {
    FollowerAckTracker t;
    t.record(kTerm, kLeader, true, 10, 5);
    t.record(kTerm, kLeader, false, 0, 0);
    require(!t.due(kTerm, kLeader, true, 10), "an early ack could overwrite a pending failure reply");
    t.record(kTerm, kLeader, true, 12, 8);
    const auto due = t.due(kTerm, kLeader, true, 12);
    require(due && *due == 12, "acks did not resume after the next successful append");
}

void a_reelected_leader_starts_its_new_term_from_nothing() {
    FollowerAckTracker t;
    t.record(kTerm, kLeader, true, 10, 0);
    t.record(kTerm + 1, kLeader, true, 3, 0);
    const auto due = t.due(kTerm + 1, kLeader, true, 10);
    require(due && *due == 3, "a re-elected leader inherited the old term's verified prefix");
}

void a_new_leader_in_the_same_term_starts_from_nothing() {
    FollowerAckTracker t;
    t.record(kTerm, kLeader, true, 10, 0);
    t.record(kTerm, kLeader + 1, true, 2, 0);
    const auto due = t.due(kTerm, kLeader + 1, true, 10);
    require(due && *due == 2, "another leader inherited a verified prefix it never checked");
}

}

int main() {
    try {
        nothing_is_due_before_a_verified_append();
        an_ack_is_capped_by_both_the_verified_prefix_and_durability();
        a_new_term_starts_from_nothing();
        a_different_leader_in_the_same_term_is_not_trusted();
        only_a_follower_acknowledges();
        a_stale_packet_never_lowers_the_verified_prefix();
        a_normal_reply_counts_as_reported();
        a_failed_append_silences_early_acks_until_the_next_success();
        a_reelected_leader_starts_its_new_term_from_nothing();
        a_new_leader_in_the_same_term_starts_from_nothing();
        std::cout << "follower ack tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "follower ack tests failed: " << error.what() << '\n';
        return 1;
    }
}
