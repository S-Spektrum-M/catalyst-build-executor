#include "cbe/executor.hpp"

#include "cbe/builder.hpp"
#include "cbe/process_exec.hpp"
#include "cbe/utility.hpp"
#include "nlohmann/detail/json_custom_base_class.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <ostream>
#include <print>
#include <queue>
#include <ranges>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <vector>

namespace catalyst {

bool file_changed_since(const std::filesystem::path &input_file, const auto &out_mod_time) {
    std::error_code ec;
    auto in_time = std::filesystem::last_write_time(input_file, ec);
    if (ec)
        return true;
    return in_time >= out_mod_time;
}

bool is_newer(const std::filesystem::path &new_file, const std::filesystem::path &old_file) {
    std::error_code ec;
    auto new_time = std::filesystem::last_write_time(new_file, ec);
    if (ec)
        return true;
    auto old_time = std::filesystem::last_write_time(old_file, ec);
    if (ec)
        return true;
    return new_time > old_time;
}

Executor::Executor(CBEBuilder &&builder, ExecutorConfig config) : builder(std::move(builder)), config(config) {
}

Result<void> Executor::clean() {
    catalyst::BuildGraph build_graph = builder.emit_graph();
    std::println("Cleaning build artifacts...");

    for (const auto &step : build_graph.steps()) {
        if (std::filesystem::exists(step.output)) {
            std::error_code ec;
            std::filesystem::remove(step.output, ec);
            if (ec) {
                std::println(stderr, "Failed to remove {}: {}", step.output, ec.message());
            } else {
                std::println("Removed {}", step.output);
            }
        }
        // Also clean .d files if they exist
        auto d_file = std::string(step.output) + ".d";
        if (std::filesystem::exists(d_file)) {
            std::filesystem::remove(d_file);
        }
    }
    return {};
}

[[clang::always_inline]]
bool inline Executor::needs_rebuild(const BuildStep &step, StatCache &stat_cache) {
    if (std::filesystem::exists(step.output)) {
        auto output_modtime = std::filesystem::last_write_time(step.output);

        if (stat_cache.changed_since(config.build_file, output_modtime)) {
            return true;
        }

        if (step.depfile_inputs.has_value()) {
            for (const auto &dep : *step.depfile_inputs) {
                if (stat_cache.changed_since(std::filesystem::path(dep), output_modtime)) {
                    return true;
                }
            }
        }
        if (step.opaque_inputs.has_value()) {
            for (const auto &opaque : *step.opaque_inputs) {
                if (stat_cache.changed_since(std::filesystem::path(opaque), output_modtime)) {
                    return true;
                }
            }
        }
        // this is our way of making sure that the .d file isn't stale
        for (const auto &input : step.parsed_inputs) {
            if (stat_cache.changed_since(input, output_modtime)) {
                return true;
            }
        }
        return false;
    }
    return true;
}

Result<void> Executor::emit_graph() {
    catalyst::BuildGraph build_graph = builder.emit_graph();
    StatCache stat_cache;

    std::cout << "digraph catalyst_build {\n";
    std::cout << "  rankdir=LR;\n";
    std::cout << "  node [shape=box, style=filled, fontname=\"Helvetica\"];\n";

    for (size_t i = 0; i < build_graph.nodes().size(); ++i) {
        const auto &node = build_graph.nodes()[i];
        std::string color = "0.9 0.9 0.9"; // light gray for source files

        if (node.step_id.has_value()) {
            const auto &step = build_graph.steps()[*node.step_id];
            if (needs_rebuild(step, stat_cache)) {
                color = "green";
            } else {
                color = "white";
            }
        }

        std::cout << "  n" << i << " [label=\"" << node.path << "\", fillcolor=\"" << color << "\"];\n";

        for (size_t target_idx : node.out_edges) {
            std::cout << "  n" << i << " -> n" << target_idx << ";\n";
        }
    }
    std::cout << "}\n";
    return {};
}

Result<void> Executor::emit_compdb() {
    catalyst::BuildGraph build_graph = builder.emit_graph();
    std::vector<size_t> order;
    if (auto res = build_graph.topo_sort(); !res)
        return std::unexpected(res.error());
    else
        order = *res;

    const auto &defs = builder.definitions();
    auto get_def = [&](std::string_view key) -> std::string {
        if (auto it = defs.find(key); it != defs.end())
            return std::string(it->second);
        return "";
    };

    // This __must__ be done otherwise optimizations will fuck up.
    const std::string cc = get_def("cc");
    const std::string cxx = get_def("cxx");
    const std::string cxxflags = get_def("cxxflags");
    const std::string cflags = get_def("cflags");
    const std::string ldflags = get_def("ldflags");
    const std::string ldlibs = get_def("ldlibs");

    const std::vector cc_vec = std::ranges::views::split(cc, ' ') | std::ranges::to<std::vector<std::string>>();
    const std::vector cxx_vec = std::ranges::views::split(cxx, ' ') | std::ranges::to<std::vector<std::string>>();
    const std::vector cflags_vec = std::ranges::views::split(cflags, ' ') | std::ranges::to<std::vector<std::string>>();
    const std::vector cxxflags_vec =
        std::ranges::views::split(cxxflags, ' ') | std::ranges::to<std::vector<std::string>>();

    using json = nlohmann::json;
    json compdb = json::array();
    auto cwd = std::filesystem::current_path().string();

    for (size_t node_idx : order) {
        const auto &node = build_graph.nodes()[node_idx];
        if (!node.step_id.has_value())
            continue;
        const auto &step = build_graph.steps()[*node.step_id];

        // Only emit for compilation steps
        if (step.tool != "cc" && step.tool != "cxx")
            continue;

        const std::vector<std::string_view> &inputs = step.parsed_inputs;

        std::vector<std::string> args;
        auto add_parts = [&args](const auto &parts) {
            for (const auto &part : parts) {
                if (part.begin() != part.end()) {
                    args.push_back(std::ranges::to<std::string>(part));
                }
            }
        };

        if (step.tool == "cc") {
            add_parts(cc_vec);
            add_parts(cflags_vec);
            args.reserve(args.size() + inputs.size() + 7);
            args.insert(args.end(), {"-MMD", "-MF", std::string(step.output) + ".d", "-c"});
            for (const auto &in : inputs)
                args.emplace_back(in);
            args.push_back("-o");
            args.push_back(std::string(step.output));
        } else if (step.tool == "cxx") {
            add_parts(cxx_vec);
            add_parts(cxxflags_vec);
            args.reserve(args.size() + inputs.size() + 7);
            args.insert(args.end(), {"-MMD", "-MF", std::string(step.output) + ".d", "-c"});
            args.reserve(args.size() + inputs.size());
            for (const auto &in : inputs)
                args.emplace_back(in);
            args.push_back("-o");
            args.push_back(std::string(step.output));
        }

        json entry;
        entry["directory"] = cwd;
        entry["arguments"] = args;
        if (!inputs.empty()) {
            entry["file"] = inputs[0];
        }
        entry["output"] = step.output;
        compdb.push_back(entry);
    }

    std::ofstream f("compile_commands.json");
    f << compdb.dump(4);
    return {};
}

Result<void> Executor::execute() {
    pool.clear(); // Ensure clean state

    catalyst::BuildGraph build_graph = builder.emit_graph();

    const auto &defs = builder.definitions();
    auto get_def = [&](std::string_view key) -> std::string {
        if (auto it = defs.find(key); it != defs.end())
            return std::string(it->second);
        return "";
    };

    // This __must__ be done otherwise optimizations will fuck up.
    const std::string cc = get_def("cc");
    const std::string cxx = get_def("cxx");
    const std::string cxxflags = get_def("cxxflags");
    const std::string cflags = get_def("cflags");
    const std::string ldflags = get_def("ldflags");
    const std::string ldlibs = get_def("ldlibs");

    const std::vector cc_vec = std::ranges::views::split(cc, ' ') | std::ranges::to<std::vector<std::string>>();
    const std::vector cxx_vec = std::ranges::views::split(cxx, ' ') | std::ranges::to<std::vector<std::string>>();
    const std::vector cflags_vec = std::ranges::views::split(cflags, ' ') | std::ranges::to<std::vector<std::string>>();
    const std::vector cxxflags_vec =
        std::ranges::views::split(cxxflags, ' ') | std::ranges::to<std::vector<std::string>>();
    const std::vector ldflags_vec =
        std::ranges::views::split(ldflags, ' ') | std::ranges::to<std::vector<std::string>>();
    const std::vector ldlibs_vec = std::ranges::views::split(ldlibs, ' ') | std::ranges::to<std::vector<std::string>>();

    // Build in-degrees
    std::vector<int> in_degrees(build_graph.nodes().size(), 0);
    for (const auto &node : build_graph.nodes()) {
        for (size_t out : node.out_edges) {
            in_degrees[out]++;
        }
    }

    std::queue<size_t> ready_queue;
    for (size_t i = 0; i < in_degrees.size(); ++i) {
        if (in_degrees[i] == 0) {
            ready_queue.push(i);
        }
    }

    std::mutex mtx;
    std::condition_variable cv_ready;
    std::atomic<size_t> completed_count = 0;
    size_t total_nodes = build_graph.nodes().size();
    bool error_occurred = false;
    size_t active_workers = 0;

    StatCache stat_cache;

#ifdef _WIN32
    std::ofstream tty("CON");
#elifdef _WIN64
    std::ofstream tty("CON");
#elifdef __linux__
    std::ofstream tty("/dev/tty");
#endif
    std::mutex cout_tty_mtx;

    // If graph is empty
    if (total_nodes == 0)
        return {};

    auto process_step = [&](size_t node_idx) {
        const auto &node = build_graph.nodes()[node_idx];
        if (node.step_id.has_value()) {
            const auto &step = build_graph.steps()[*node.step_id];
            const auto &inputs = step.parsed_inputs;

            if (needs_rebuild(step, stat_cache)) {
                {
                    std::lock_guard lock(cout_tty_mtx);
                    tty << "\033[1m" << std::flush;
                    if (config.dry_run)
                        std::cout << "[DRY RUN] " << std::flush;
                    else
                        std::cout << "[" << completed_count + 1 << "/" << total_nodes << "] " << std::flush;
                    tty << "\033[0m\033[1;32m" << std::flush;
                    std::cout << std::setw(3) << step.tool << std::flush;
                    tty << "\033[0m\033[0m" << std::flush;
                    std::cout << " -> " << step.output << std::endl;
                    if (config.dry_run)
                        return 0;
                }

                static constexpr auto ARGS_VEC_INIT_SZ = 40;
                std::vector<std::string> args;
                args.reserve(ARGS_VEC_INIT_SZ);
                auto add_parts = [&args](const auto &parts) {
                    args.reserve(args.size() + parts.size());
                    for (const auto &part : parts) {
                        if (part.begin() != part.end()) {
                            args.push_back(std::ranges::to<std::string>(part));
                        }
                    }
                };

                if (step.tool == "cc") {
                    add_parts(cc_vec);
                    add_parts(cflags_vec);
                    args.insert(args.end(), {"-MMD", "-MF", std::string(step.output) + ".d", "-c"});
                    for (const auto &in : inputs)
                        args.emplace_back(in);
                    args.push_back("-o");
                    args.push_back(std::string(step.output));
                } else if (step.tool == "cxx") {
                    add_parts(cxx_vec);
                    add_parts(cxxflags_vec);
                    args.insert(args.end(), {"-MMD", "-MF", std::string(step.output) + ".d", "-c"});
                    for (const auto &in : inputs)
                        args.emplace_back(in);
                    args.push_back("-o");
                    args.push_back(std::string(step.output));
                } else if (step.tool == "ld") {
                    add_parts(cxx_vec);
                    static constexpr auto TUNABLE__INPUT_SZ = 50;
                    std::filesystem::path rsp_path = std::filesystem::path(step.output).replace_extension(".rsp");
                    if (std::filesystem::exists(rsp_path) && is_newer(rsp_path, config.build_file)) {
                        args.push_back(std::string("@") + rsp_path.string());
                    } else if (inputs.size() > TUNABLE__INPUT_SZ) {
                        std::string rsp_content;
                        constexpr auto TUNABLE__rsp_path_estimate = 100;
                        rsp_content.reserve(inputs.size() * TUNABLE__rsp_path_estimate);
                        for (const auto &input : inputs) {
                            rsp_content += input;
                            rsp_content += '\n';
                        }
                        std::ofstream rsp_file(rsp_path);
                        rsp_file.write(rsp_content.data(), rsp_content.size());
                        args.push_back(std::string("@") + rsp_path.string());
                    } else {
                        for (const auto &in : inputs)
                            args.emplace_back(in);
                    }
                    args.push_back("-o");
                    args.push_back(std::string(step.output));
                    add_parts(ldflags_vec);
                    add_parts(ldlibs_vec);
                } else if (step.tool == "ar") {
                    args.insert(args.end(), {"ar", "rcs", std::string(step.output)});
                    for (const auto &in : inputs)
                        args.emplace_back(in);
                } else if (step.tool == "sld") {
                    add_parts(cxx_vec);
                    args.push_back("-shared");
                    for (const auto &in : inputs)
                        args.emplace_back(in);
                    args.push_back("-o");
                    args.push_back(std::string(step.output));
                }

                auto res = catalyst::process_exec(std::move(args));

                if (res) {
                    int ec = *res;
                    if (ec != 0) {
                        std::println(stderr, "Build failed: {} -> {} (exit code {})", step.tool, step.output, ec);
                        return ec;
                    }
                } else {
                    std::println(stderr, "Failed to execute: {}", res.error());
                    return 1;
                }
            }
        }
        return 0;
    };

    auto worker = [&]() {
        while (true) {
            size_t node_idx;
            {
                std::unique_lock lock(mtx);
                cv_ready.wait(lock, [&] {
                    return !ready_queue.empty() || completed_count == total_nodes || active_workers == 0; //
                });

                if (ready_queue.empty()) {
                    if (completed_count == total_nodes)
                        return;
                    // If queue is empty and no active workers, but work not done -> Cycle
                    return;
                }

                node_idx = ready_queue.front();
                ready_queue.pop();
                active_workers++;
            }

            int result = process_step(node_idx);

            {
                std::lock_guard lock(mtx);
                active_workers--;

                size_t new_work_count = 0;

                if (result != 0) {
                    error_occurred = true;
                    completed_count = total_nodes; // Force exit
                } else {
                    completed_count.fetch_add(1, std::memory_order_relaxed);
                    const auto &node = build_graph.nodes()[node_idx];
                    for (size_t neighbor : node.out_edges) {
                        in_degrees[neighbor]--;
                        if (in_degrees[neighbor] == 0) {
                            ready_queue.push(neighbor);
                            new_work_count++;
                        }
                    }
                }

                bool build_finished = (completed_count == total_nodes);
                bool stall_detected = (active_workers == 0);
                constexpr auto TUNABLE__notify_all_criteria = 10;
                if (build_finished || error_occurred || stall_detected) {
                    cv_ready.notify_all();
                } else if (new_work_count == 1) {
                    cv_ready.notify_one();
                } else if (new_work_count >= TUNABLE__notify_all_criteria) {
                    cv_ready.notify_all();
                } else {
                    for (size_t ii = 0; ii < new_work_count; ++ii) {
                        cv_ready.notify_one();
                    }
                }
            }
        }
    };

    size_t thread_count = config.jobs;
    if (thread_count == 0)
        thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0)
        thread_count = 1;

    for (size_t i = 0; i < thread_count; ++i) {
        pool.emplace_back(worker);
    }

    pool.clear(); // Join all threads

    if (error_occurred)
        return std::unexpected("Build Failed");

    if (completed_count != total_nodes)
        return std::unexpected("Cycle detected: Build stalled with pending nodes.");

    return {};
}

} // namespace catalyst
