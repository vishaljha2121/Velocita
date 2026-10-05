#include "velocita/v2/wal_arena.hpp"
#include "v2_identity_test_support.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
using namespace velocita::v2;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const VerifiedStaticIdentity& identity() {
    static const auto value = identity_test::verified_identity(
        identity_test::identity(1, {1, 2, 3}));
    return value;
}

struct TempDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("velocita-wal-maintenance-" + std::to_string(::getpid()));
    TempDirectory() {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDirectory() { std::filesystem::remove_all(path); }
};

template <typename Predicate>
void wait_until(Predicate predicate, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error(message);
        std::this_thread::yield();
    }
}

struct BlockingJob {
    std::atomic<bool> entered{};
    std::atomic<bool> release{};
    std::atomic<unsigned> runs{};
    int input{};
    int observed{};
};

void run_blocking(void* raw) {
    auto& job = *static_cast<BlockingJob*>(raw);
    job.runs.fetch_add(1, std::memory_order_relaxed);
    job.entered.store(true, std::memory_order_release);
    while (!job.release.load(std::memory_order_acquire)) std::this_thread::yield();
    job.observed = job.input;
}

void run_once(void* raw) {
    static_cast<BlockingJob*>(raw)->runs.fetch_add(1, std::memory_order_relaxed);
}

void throw_job(void*) { throw std::runtime_error("maintenance sentinel"); }

void test_blocking_job_keeps_owner_nonblocking_and_hands_off_payload(
    const std::filesystem::path& root) {
    WalBlockArena arena;
    std::filesystem::create_directories(root);
    WalSegment segment(root, identity());
    WalWriter writer(segment, arena);
    writer.start();

    BlockingJob job;
    struct ReleaseAndJoin {
        BlockingJob& job;
        WalWriter& writer;
        ~ReleaseAndJoin() {
            job.release.store(true, std::memory_order_release);
            writer.stop();
        }
    } cleanup{job, writer};
    job.input = 73;
    require(writer.submit_maintenance(run_blocking, &job), "maintenance refused");
    wait_until([&] { return job.entered.load(std::memory_order_acquire); },
               "maintenance body never entered");

    BlockDescriptor arbitrary{0, 1, 1, 1, 1};
    BlockDescriptor completion{9, 9, 9, 9, 9};
    require(!writer.submit(arbitrary), "block accepted while maintenance was pending");
    require(!writer.submit_maintenance(run_once, &job),
            "second maintenance accepted while first was pending");
    require(!writer.take_maintenance_completion(),
            "maintenance completed before its body returned");
    require(!writer.take_completion(completion),
            "blocked maintenance manufactured a WAL completion");
    require(completion.slot == 9 && completion.generation == 9,
            "empty completion poll changed its destination");

    job.release.store(true, std::memory_order_release);
    wait_until([&] { return writer.take_maintenance_completion(); },
               "maintenance completion was not published");
    require(job.observed == 73, "release/acquire did not hand off job payload");
    require(job.runs.load(std::memory_order_relaxed) == 1,
            "maintenance body did not execute exactly once");

    require(writer.submit_maintenance(run_once, &job),
            "maintenance slot was not reusable after collection");
    wait_until([&] { return writer.take_maintenance_completion(); },
               "reused maintenance did not complete");
    require(job.runs.load(std::memory_order_relaxed) == 2,
            "reused maintenance ran other than exactly once");
    writer.stop();
}

void test_exception_is_a_permanent_writer_failure(const std::filesystem::path& root) {
    WalBlockArena arena;
    WalSegment segment(root, identity());
    WalWriter writer(segment, arena);
    writer.start();
    BlockingJob context;
    require(writer.submit_maintenance(throw_job, &context),
            "throwing maintenance refused before execution");
    wait_until([&] { return writer.failed(); },
               "maintenance exception did not latch writer failure");
    require(!writer.submit({0, 1, 1, 1, 1}),
            "failed writer accepted WAL work");
    require(!writer.submit_maintenance(run_once, &context),
            "failed writer accepted maintenance work");
    require(!writer.take_maintenance_completion(),
            "throwing maintenance reported successful completion");
    bool rethrown{};
    try { writer.rethrow_failure(); }
    catch (const std::runtime_error& error) {
        rethrown = std::string(error.what()) == "maintenance sentinel";
    }
    require(rethrown, "maintenance exception was not preserved by failure latch");
    writer.stop();
}

}

int main() {
    try {
        TempDirectory temp;
        test_blocking_job_keeps_owner_nonblocking_and_hands_off_payload(temp.path / "normal");
        std::filesystem::create_directories(temp.path / "failure");
        test_exception_is_a_permanent_writer_failure(temp.path / "failure");
        std::cout << "WAL maintenance tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
