#define main original_gateway_suite_main
#include "../../../tests/v2_gateway_tests.cpp"
#undef main

int main() {
    const auto fixture = authorized_cancel();
    auto control = matching_control(fixture.state);
    control.now = control.confirmed_at + control.lease_bound + 1;
    GatewayDeliveryState delivery;
    delivery.started = true;
    const auto guard = evaluate_pre_send(fixture.state, fixture.effect_id, control,
                                         capable_venue(), {0, 1});
    const auto plan = plan_delivery(fixture.state, delivery, {}, control,
                                    capable_venue(), {0, 1}, {}, control.now);
    std::cout << "cancel_guard_permitted=" << guard.permitted()
              << " planned_sends=" << plan.send_count << '\n';
    if (!guard.permitted()) return 2;
    if (plan.send_count != 1 || plan.to_send[0] != fixture.effect_id) {
        std::cerr << "RED: planner drops reserved emergency quota although guard permits cancel\n";
        return 1;
    }
    return 0;
}
