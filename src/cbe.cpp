#include "cbe/builder.hpp"
#include "cbe/executor.hpp"
#include "cbe/parser.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>

int main(const int argc, const char *const *argv) {

    catalyst::CBEBuilder builder;
    std::filesystem::path input_path = "catalyst.build";

    if (!std::filesystem::exists("catalyst.build")) {
        std::println(std::cerr, "Build File: catalyst.build does not exist.\n");
        return 1;
    }
    if (std::filesystem::is_symlink("catalyst.build")) {
        // FIXME: figure out __how__ to support.
        std::println(std::cerr, "cbe does not support parsing symbolically linked files.");
        return 1;
    }

    if (auto res = catalyst::parse(builder, input_path); !res) {
        std::println(std::cerr, "Failed to parse: {}", res.error());
        return 1;
    }

    catalyst::Executor executor{std::move(builder)};
    if (argc > 1 && strcmp(argv[1], "COMPDB") == 0) {
        auto _ = executor.emit_compdb();
    } else {
        if (auto res = executor.execute(); !res) {
            std::println(std::cerr, "Execution failed: {}", res.error());
            return 1;
        }
    }

    return 0;
}
