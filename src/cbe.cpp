#include "cbe/builder.hpp"
#include "cbe/executor.hpp"
#include "cbe/parser.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <print>

int main(const int argc, const char *const *argv) {
    catalyst::CBEBuilder builder;
    std::filesystem::path input_path = "catalyst.build";

    if (auto res = catalyst::parse(builder, input_path); !res) {
        std::println(std::cerr, "Failed to parse: {}", res.error());
        return 1;
    }

    catalyst::Executor e{std::move(builder)};
    auto _ = e.execute();

    return 0;
}
