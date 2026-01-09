#include "cbe/builder.hpp"
#include "cbe/executor.hpp"
#include "cbe/parser.hpp"

#include <charconv>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>

void print_help() {
    std::println("Usage: cbe [options]");
    std::println("Options:");
    std::println("  -h, --help       Show this help message");
    std::println("  -v, --version    Show version");
    std::println("  -d <dir>         Change working directory before doing anything");
    std::println("  -f <file>        Use <file> as the build manifest (default: catalyst.build)");
    std::println("  -j, --jobs <N>   Set number of parallel jobs (default: auto)");
    std::println("  --dry-run        Print commands without executing them");
    std::println("  --clean          Remove build artifacts");
    std::println("  --compdb         Generate compile_commands.json");
    std::println("  --graph          Generate DOT graph of build");
}

void print_version() {
    std::println("cbe {}", CATALYST_PROJ_VER);
}

int main(const int argc, const char *const *argv) {
    catalyst::ExecutorConfig config;
    bool compdb = false;
    bool graph = false;
    std::string input_path = "catalyst.build";
    std::filesystem::path work_dir = ".";

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            print_version();
            return 0;
        } else if (arg == "-d") {
            if (i + 1 < argc) {
                work_dir = argv[i + 1];
                i++;
            } else {
                std::println(std::cerr, "Missing argument for -d");
                return 1;
            }
        } else if (arg == "-f") {
            if (i + 1 < argc) {
                input_path = argv[i + 1];
                config.build_file = input_path;
                i++;
            } else {
                std::println(std::cerr, "Missing argument for -f");
                return 1;
            }
        } else if (arg == "--dry-run") {
            config.dry_run = true;
        } else if (arg == "--clean") {
            config.clean = true;
        } else if (arg == "--compdb") {
            compdb = true;
        } else if (arg == "--graph") {
            graph = true;
        } else if (arg == "-j" || arg == "--jobs") {
            if (i + 1 < argc) {
                size_t jobs = 0;
                auto res = std::from_chars(argv[i + 1], argv[i + 1] + strlen(argv[i + 1]), jobs);
                if (res.ec == std::errc()) {
                    config.jobs = jobs;
                    i++;
                } else {
                    std::println(std::cerr, "Invalid job count: {}", argv[i + 1]);
                    return 1;
                }
            } else {
                std::println(std::cerr, "Missing argument for {}", arg);
                return 1;
            }
        } else {
            std::println(std::cerr, "Unknown argument: {}", arg);
            print_help();
            return 1;
        }
    }

    if (work_dir != ".") {
        std::error_code ec;
        std::filesystem::current_path(work_dir, ec);
        if (ec) {
            std::println(std::cerr, "Failed to change directory to {}: {}", work_dir.string(), ec.message());
            return 1;
        }
    }

    catalyst::CBEBuilder builder;

    if (!std::filesystem::exists(input_path)) {
        std::println(std::cerr, "Build File: {} does not exist.\n", input_path);
        return 1;
    }
    if (std::filesystem::is_symlink(input_path)) {
        // FIXME: figure out __how__ to support.
        std::println(std::cerr, "cbe does not support parsing symbolically linked files.");
        return 1;
    }

    if (auto res = catalyst::parse(builder, input_path); !res) {
        std::println(std::cerr, "Failed to parse: {}", res.error());
        return 1;
    }

    catalyst::Executor executor{std::move(builder), config};

    if (compdb) {
        auto _ = executor.emit_compdb();
    } else if (graph) {
        auto _ = executor.emit_graph();
    } else if (config.clean) {
        if (auto res = executor.clean(); !res) {
            std::println(std::cerr, "Clean failed: {}", res.error());
            return 1;
        }
    } else {
        if (auto res = executor.execute(); !res) {
            std::println(std::cerr, "Execution failed: {}", res.error());
            return 1;
        }
    }

    return 0;
}
