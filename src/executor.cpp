#include "cbe/executor.hpp"

#include "cbe/builder.hpp"

#include <condition_variable>
#include <filesystem>
#include <format>
#include <mutex>
#include <print>
#include <queue>

namespace catalyst {

bool file_changed(const std::filesystem::path &input_file, const auto &out_mod_time) {
    return std::filesystem::last_write_time(input_file) >= out_mod_time;
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
                if (step.tool == "cc") {
                    command_string = std::format(
                        "{} {} -MMD -MF {}.d -c {} -o {}", cc, cflags, step.output, inputs_str, step.output);
                } else if (step.tool == "cxx") {
                    command_string = std::format(
                        "{} {} -MMD -MF {}.d -c {} -o {}", cxx, cxxflags, step.output, inputs_str, step.output);
                } else if (step.tool == "ld") {
                    command_string = std::format("{} {} -o {} {} {}", cxx, inputs_str, step.output, ldflags, ldlibs);
                } else if (step.tool == "ar") {
                    command_string = std::format("ar rcs {} {}", step.output, inputs_str);
                } else if (step.tool == "sld") {
                    command_string = std::format("{} -shared {} -o {}", cxx, inputs_str, step.output);
                }
                std::println("{}", command_string);
                std::system(command_string.c_str());
            }
        }
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

            process_step(node_idx);

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
