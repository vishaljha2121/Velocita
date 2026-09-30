#include "velocita/v2/election_jitter.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

// Each voter is its own process. The first rand() value a fresh process draws
// is what willemt/raft turns into its first randomized election timeout.

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// The first rand() of a fresh child process, optionally seeded first.
int first_draw_in_a_new_process(bool seeded) {
    int fds[2];
    require(::pipe(fds) == 0, "pipe failed");
    const pid_t pid = ::fork();
    require(pid >= 0, "fork failed");
    if (pid == 0) {
        ::close(fds[0]);
        const bool ok = !seeded || velocita::v2::seed_election_jitter();
        const int value = ok ? std::rand() : -1;
        (void)!::write(fds[1], &value, sizeof(value));
        ::_exit(0);
    }
    ::close(fds[1]);
    int value = -2;
    require(::read(fds[0], &value, sizeof(value)) == static_cast<ssize_t>(sizeof(value)), "child sent nothing");
    ::close(fds[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    return value;
}

void unseeded_processes_draw_the_same_timeouts() {
    // The defect this module exists for, pinned so a libc that seeds on its own
    // would show up here rather than silently.
    require(first_draw_in_a_new_process(false) == first_draw_in_a_new_process(false),
            "unseeded processes already differ: the premise changed");
}

void seeded_processes_draw_different_timeouts() {
    const int a = first_draw_in_a_new_process(true), b = first_draw_in_a_new_process(true);
    require(a >= 0 && b >= 0, "seeding failed in a child");
    // Equal with probability 1/RAND_MAX per pair; three pairs all equal would be a defect.
    const int c = first_draw_in_a_new_process(true);
    require(!(a == b && b == c), "seeded processes still draw identical election timeouts");
}

}  // namespace

int main() {
    try {
        unseeded_processes_draw_the_same_timeouts();
        seeded_processes_draw_different_timeouts();
        std::cout << "election jitter tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "election jitter tests failed: " << error.what() << '\n';
        return 1;
    }
}
