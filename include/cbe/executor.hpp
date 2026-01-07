#pragma once

#include "cbe/builder.hpp"
#include "cbe/graph.hpp"
#include "cbe/utility.hpp"

#include <thread>
#include <vector>

namespace catalyst {

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

private:
    CBEBuilder builder;
    ExecutorConfig config;
    std::vector<std::jthread> pool;
};
} // namespace catalyst
