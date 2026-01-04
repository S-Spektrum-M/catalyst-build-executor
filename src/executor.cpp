#include "cbe/executor.hpp"

#include "cbe/builder.hpp"
#include "cbe/process_exec.hpp"

#include <condition_variable>
#include <filesystem>
#include <format>
#include <mutex>
#include <print>
#include <queue>
#include <ranges>
#include <string>
#include <vector>

namespace catalyst {

bool file_changed(const std::filesystem::path &input_file, const auto &out_mod_time) {
    std::error_code ec;
    auto in_time = std::filesystem::last_write_time(input_file, ec);
    if (ec)
        return true;
    return in_time >= out_mod_time;
}

Executor::Executor(CBEBuilder &&builder) : builder(std::move(builder)) {
}

Result<void> Executor::execute() {
    pool.clear(); // Ensure clean state

    catalyst::BuildGraph build_graph = builder.emit_graph();

    // Still use topo_sort to validate cycle-free graph
    if (auto res = build_graph.topo_sort(); !res) {
        return std::unexpected(res.error());
    }

    const auto &defs = builder.definitions();
    auto get_def = [&](std::string_view key) -> std::string {
        if (auto it = defs.find(key); it != defs.end())
            return std::string(it->second);
        return "";
    };

    const std::string cc = get_def("cc");
    const std::string cxx = get_def("cxx");
    const std::string cxxflags = get_def("cxxflags");
    const std::string cflags = get_def("cflags");
    const std::string ldflags = get_def("ldflags");
    const std::string ldlibs = get_def("ldlibs");

    const std::vector cc_vec = std::ranges::views::split(cc, ' ') | std::ranges::to<std::vector>();
    const std::vector cxx_vec = std::ranges::views::lazy_split(cxx, ' ') | std::ranges::to<std::vector>();
    const std::vector cflags_vec = std::ranges::views::split(cflags, ' ') | std::ranges::to<std::vector>();
    const std::vector cxxflags_vec = std::ranges::views::split(cxxflags, ' ') | std::ranges::to<std::vector>();
    const std::vector ldflags_vec = std::ranges::views::split(ldflags, ' ') | std::ranges::to<std::vector>();
    const std::vector ldlibs_vec = std::ranges::views::split(ldlibs, ' ') | std::ranges::to<std::vector>();

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
    std::condition_variable cv_done;
    size_t completed_count = 0;
    size_t total_nodes = build_graph.nodes().size();

    // If graph is empty
    if (total_nodes == 0)
        return {};

    auto process_step = [&](size_t node_idx) {
        const auto &node = build_graph.nodes()[node_idx];
        if (node.step_id.has_value()) {
            const auto &step = build_graph.steps()[*node.step_id];

            std::vector<std::string> inputs;
            std::string_view pending = step.inputs;
            while (true) {
                size_t pos = pending.find(',');
                if (pos == std::string_view::npos) {
                    if (!pending.empty())
                        inputs.emplace_back(pending);
                    break;
                }
                inputs.emplace_back(pending.substr(0, pos));
                pending.remove_prefix(pos + 1);
            }

            std::string inputs_str;
            for (const auto &input : inputs) {
                if (!inputs_str.empty())
                    inputs_str += " ";
                inputs_str += input;
            }

            std::string command_string;
            bool needs_rebuild = false;

            if (std::filesystem::exists(step.output)) {
                auto output_modtime = std::filesystem::last_write_time(step.output);
                if (step.depfile_inputs.has_value()) {
                    for (const auto &dep : *step.depfile_inputs) {
                        if (file_changed(std::filesystem::path(dep), output_modtime)) {
                            needs_rebuild = true;
                            break;
                        }
                    }
                }
                if (!needs_rebuild)
                    for (const auto &input : inputs) {
                        if (file_changed(input, output_modtime)) {
                            needs_rebuild = true;
                            break;
                        }
                    }
            } else {
                needs_rebuild = true;
            }

            if (needs_rebuild) {
                std::println("{} -> {}", step.tool, step.output);

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
                    args.insert(args.end(), inputs.begin(), inputs.end());
                    args.push_back("-o");
                    args.push_back(std::string(step.output));
                } else if (step.tool == "cxx") {
                    add_parts(cxx_vec);
                    add_parts(cxxflags_vec);
                    args.insert(args.end(), {"-MMD", "-MF", std::string(step.output) + ".d", "-c"});
                    args.insert(args.end(), inputs.begin(), inputs.end());
                    args.push_back("-o");
                    args.push_back(std::string(step.output));
                } else if (step.tool == "ld") {
                    add_parts(cxx_vec);
                    args.insert(args.end(), inputs.begin(), inputs.end());
                    args.push_back("-o");
                    args.push_back(std::string(step.output));
                    add_parts(ldflags_vec);
                    add_parts(ldlibs_vec);
                } else if (step.tool == "ar") {
                    args.insert(args.end(), {"ar", "rcs", std::string(step.output)});
                    args.insert(args.end(), inputs.begin(), inputs.end());
                } else if (step.tool == "sld") {
                    add_parts(cxx_vec);
                    args.push_back("-shared");
                    args.insert(args.end(), inputs.begin(), inputs.end());
                    args.push_back("-o");
                    args.push_back(std::string(step.output));
                }

                auto res = catalyst::process_exec(std::move(args));
                if (res) {
                    int ec = res->get();
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
                cv_ready.wait(lock, [&] { return !ready_queue.empty() || completed_count == total_nodes; });

                if (ready_queue.empty()) {
                    if (completed_count == total_nodes)
                        return;
                    continue;
                }

                node_idx = ready_queue.front();
                ready_queue.pop();
            }

            if (auto res = process_step(node_idx); res != 0) {
                std::exit(res);
            }

            {
                std::lock_guard lock(mtx);
                completed_count++;

                const auto &node = build_graph.nodes()[node_idx];
                for (size_t neighbor : node.out_edges) {
                    in_degrees[neighbor]--;
                    if (in_degrees[neighbor] == 0) {
                        ready_queue.push(neighbor);
                        cv_ready.notify_one();
                    }
                }

                if (completed_count == total_nodes) {
                    cv_done.notify_one();
                    cv_ready.notify_all(); // Wake up other workers to exit
                }
            }
        }
    };

    size_t thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0)
        thread_count = 1;

    for (size_t i = 0; i < thread_count; ++i) {
        pool.emplace_back(worker);
    }

    // Wait for completion
    {
        std::unique_lock lock(mtx);
        cv_done.wait(lock, [&] { return completed_count == total_nodes; });
    }

    pool.clear(); // Join all threads
    return {};
}

} // namespace catalyst
