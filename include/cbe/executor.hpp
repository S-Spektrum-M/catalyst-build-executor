#pragma once

#include "cbe/builder.hpp"
#include "cbe/graph.hpp"
#include "cbe/utility.hpp"

#include <shared_mutex>
#include <thread>
#include <vector>

namespace catalyst {

class StatCache {
    struct Entry {
        std::filesystem::path path;
        std::filesystem::file_time_type time;
        std::error_code ec;

        bool operator<(const Entry &other) const;
        bool operator<(const std::filesystem::path &other_path) const;
    };

    std::vector<Entry> cache;
    std::shared_mutex cache_mtx;

public:
    std::pair<std::filesystem::file_time_type, std::error_code> get_or_update(const std::filesystem::path &p);
    bool changed_since(const std::filesystem::path &input, std::filesystem::file_time_type output_time);
};

struct ExecutorConfig {
    bool dry_run = false;
    bool clean = false;
    size_t jobs = 0; // 0 means auto-detect
    std::string build_file = "catalyst.build";
};

class Executor {
public:
    Executor(CBEBuilder &&builder, ExecutorConfig config = {});
    Result<void> execute();
    Result<void> clean();
    Result<void> emit_compdb();
    Result<void> emit_graph();

private:
    bool needs_rebuild(const BuildStep &step, StatCache &stat_cache);

    CBEBuilder builder;
    ExecutorConfig config;
    std::vector<std::jthread> pool;
};
} // namespace catalyst
