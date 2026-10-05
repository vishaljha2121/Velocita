#pragma once

struct BallotModelResult {
    int events{};
    bool granted{};
};

inline BallotModelResult ballot_async_scenario(const Names schedule, const int cut) {
    auto pattern = (std::filesystem::temp_directory_path() /
                    "v2-async-ballot-model-XXXXXX").string();
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    const char* made = ::mkdtemp(storage.data());
    require(made != nullptr, "ballot model mkdtemp failed");
    const auto root = std::filesystem::canonical(made);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    } cleanup{root};

    const auto node_root = root / "node";
    const auto image_root = root / "image";
    const auto identity = identity_test::identity(1, {1, 2});
    auto node = std::make_unique<ReplicatedKernel>(
        VerifiedIdentityStartup::bootstrap(node_root, identity,
                                           catalog_test::standard_catalog()));
    Model model(node_root, schedule);
    model.cut = cut;
    compaction_disk_model::active.store(&model, std::memory_order_release);
    node->start_writer();

    bool granted{};
    bool completed{};
    bool failed{};
    const auto observe = [&] {
        try {
            while (auto message = identity_test::take_outbound_message(*node)) {
                if (const auto* response = std::get_if<RequestVoteResponse>(&message->payload))
                    granted = granted || response->vote_granted;
            }
        } catch (const std::exception&) {}
    };
    try {
        const RequestVote request{7, 2, 0, 0};
        identity_test::deliver_declared_peer(*node, {2, 1, request});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline &&
               (node->persistence_pending() || !granted)) {
            node->flush_durability();
            observe();
            std::this_thread::yield();
        }
        observe();
        completed = granted && !node->persistence_pending();
    } catch (const std::exception&) {
        observe();
        failed = true;
    }

    node.reset();
    compaction_disk_model::active.store(nullptr, std::memory_order_release);
    require(!model.bad.load(), model.wrong_thread.load()
        ? "ballot model intercepted protocol-owner I/O"
        : "ballot model hook raised an internal exception");
    require(cut < 0 ? completed && !failed :
                model.frozen.load() && failed && model.events.load() == cut,
            "ballot model missed a cut or successful grant baseline");
    model.materialize(model.root_key, image_root);

    std::optional<PersistentMetadata> durable;
    const auto metadata_path = identity_test::runtime_directory(image_root) / kV2MetadataFileName;
    if (std::filesystem::exists(metadata_path)) {
        const auto bytes = read_bytes(metadata_path);
        PersistentMetadata decoded{};
        BoundFrameScratch scratch{};
        if (decode_bound_metadata(bytes, identity_test::verified_identity(identity),
                                  decoded, scratch) == BoundFrameError::none)
            durable = decoded;
    }
    if (granted)
        require(durable && durable->term == 7 && durable->voted_for == 2 &&
                    durable->commit_index == 0,
                "granted ballot lacks exact durable term/vote tuple");
    else
        require(durable && ((durable->term == 0 && durable->voted_for == -1 &&
                 durable->commit_index == 0) ||
                    (durable->term == 7 && durable->voted_for == 2 &&
                     durable->commit_index == 0)),
                "ballot crash image contains an unauthored metadata tuple");
    return {model.events.load(), granted};
}
