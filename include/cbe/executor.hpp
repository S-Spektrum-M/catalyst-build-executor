#pragma once

#include "cbe/builder.hpp"
#include "cbe/graph.hpp"
#include "cbe/utility.hpp"

#include <thread>
#include <vector>
namespace catalyst {
class Executor {
public:
    Executor(CBEBuilder &&builder);
    Result<void> execute();

private:
    CBEBuilder builder;
    std::vector<std::jthread> pool;
};
} // namespace catalyst
