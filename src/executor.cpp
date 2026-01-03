#include "cbe/executor.hpp"

#include "cbe/builder.hpp"

#include <algorithm>
#include <format>
#include <print>

namespace catalyst {

Executor::Executor(CBEBuilder &&builder) : builder(std::move(builder)) {
}

Result<void> Executor::execute() {
    catalyst::BuildGraph build_graph = builder.emit_graph();

    std::vector<size_t> build_order;
    if (auto res = build_graph.topo_sort(); !res) {
        return std::unexpected(res.error());
    } else {
        build_order = *res;
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

    for (size_t node_idx : build_order) {
        const auto &node = build_graph.nodes()[node_idx];
        if (node.step_id.has_value()) {
            const auto &step = build_graph.steps()[*node.step_id];

            std::string inputs = std::string(step.inputs);
            std::replace(inputs.begin(), inputs.end(), ',', ' ');

            std::string command_string;
            if (step.tool == "cc") {
                command_string = std::format("{} {} -MMD -MF {}.d -c {} -o {}", cc, cflags, step.output, inputs, step.output);
            } else if (step.tool == "cxx") {
                command_string = std::format("{} {} -MMD -MF {}.d -c {} -o {}", cxx, cxxflags, step.output, inputs, step.output);
            } else if (step.tool == "ld") {
                command_string = std::format("{} {} -o {} {} {}", cxx, inputs, step.output, ldflags, ldlibs);
            } else if (step.tool == "ar") {
                command_string = std::format("ar rcs {} {}", step.output, inputs);
            } else if (step.tool == "sld") {
                command_string = std::format("{} -shared {} -o {}", cxx, inputs, step.output);
            }
            std::println("{}", command_string);
            std::system(command_string.c_str());
        }
    }
    return {};
}

} // namespace catalyst
