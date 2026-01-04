#include "cbe/graph.hpp"

#include "cbe/mmap.hpp"
#include "cbe/utility.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>

namespace fs = std::filesystem;

namespace catalyst {

size_t BuildGraph::get_or_create_node(std::string_view path) {
    if (auto it = index_.find(path); it != index_.end()) {
        return it->second;
    }

    size_t id = nodes_.size();
    nodes_.push_back({path, {}, std::nullopt});
    index_.emplace(path, id);
    return id;
}

std::shared_ptr<MappedFile> parse_depfile(const std::filesystem::path &path, auto callback) {
    if (!fs::exists(path)) {
        return nullptr;
    }
    auto map = std::make_shared<MappedFile>(path);
    std::string_view content = map->content();

    if (content.empty())
        return map;

    const char *ptr = content.data();
    const char *end = ptr + content.size();

    // 1. Skip to deps, ignoring final output
    const char *colon = static_cast<const char *>(std::memchr(ptr, ':', end - ptr));
    if (!colon)
        return map;
    ptr = colon + 1;

    // Main parsing loop
    while (ptr < end) {
        while (ptr < end) {
            unsigned char c = *ptr;
            if (c > ' ' && c != '\\')
                break;

            if (c <= ' ') {
                ptr++;
            } else if (c == '\\') {
                if (ptr + 1 < end && (ptr[1] == '\n' || ptr[1] == '\r')) {
                    ptr++; // skip
                    if (*ptr == '\r')
                        ptr++;
                    if (ptr < end && *ptr == '\n')
                        ptr++;
                } else {
                    break; // Escaped character, part of a filename
                }
            }
        }

        if (ptr >= end)
            break;

        // Extract Token
        const char *start = ptr;
        while (ptr < end) {
            unsigned char c = *ptr;

            if (c <= ' ' || c == '\\')
                break;
            ptr++;
        }

        // Handle the edge case of an escaped space or line continuation within a token
        if (ptr < end && *ptr == '\\') {
            // If we hit a backslash, we fall back to a slower scan for this specific token
            while (ptr < end) {
                if (*ptr == '\\') {
                    if (ptr + 1 >= end) {
                        // Dangling backslash at EOF
                        ++ptr;
                        break;
                    }
                    if (ptr[1] == '\n' || ptr[1] == '\r') {
                        break; // line continuation
                    }
                    ptr += 2; // Safe: we know ptr + 1 < end
                } else if (static_cast<unsigned char>(*ptr) <= ' ') {
                    break;
                } else {
                    ptr++;
                }
            }
        }

        if (ptr > start) {
            callback(std::string_view(start, ptr - start));
        }
    }

    return map;
}

Result<size_t> BuildGraph::add_step(BuildStep step) {
    size_t out_id = get_or_create_node(step.output);

    auto depfile_parse_callback = [this, out_id](std::string_view fn) {
        size_t in_id = get_or_create_node(fn);
        this->nodes_[in_id].out_edges.push_back(out_id);
    };

    if (nodes_[out_id].step_id.has_value()) { // 2 different steps create the same file.
        return std::unexpected(std::format("Duplicate producer for output: {}", step.output));
    }

    size_t step_id = steps_.size();
    steps_.push_back(step); // Store the step
    nodes_[out_id].step_id = step_id;

    if (step.tool == "cc" || step.tool == "cxx") {
        const fs::path depfile_path = std::format("{}.d", step.output);
        if (auto mmap = parse_depfile(depfile_path, depfile_parse_callback)) {
            add_resource(mmap);
        }
    } else if (step.tool == "ld" || step.tool == "sld" || step.tool == "ar") {
        // TODO: parse .rsp file
    }

    // Iterate over comma-separated inputs
    std::string_view remaining = step.inputs;
    while (!remaining.empty()) {
        size_t comma_pos = remaining.find(',');
        std::string_view in_path;
        if (comma_pos == std::string_view::npos) {
            in_path = remaining;
            remaining = {};
        } else {
            in_path = remaining.substr(0, comma_pos);
            remaining = remaining.substr(comma_pos + 1);
        }

        if (!in_path.empty()) {
            size_t in_id = get_or_create_node(in_path);
            nodes_[in_id].out_edges.push_back(out_id);
        }
    }

    return step_id;
}

Result<std::vector<size_t>> BuildGraph::topo_sort() const {
    enum class STATUS : uint8_t { UNSTARTED, WORKING, FINISHED };

    std::vector<STATUS> status(nodes_.size(), STATUS::UNSTARTED);
    std::vector<size_t> order;
    order.reserve(nodes_.size());

    std::function<Result<void>(size_t)> dfs = [&](size_t u) -> Result<void> {
        status[u] = STATUS::WORKING;
        for (size_t v : nodes_[u].out_edges) {
            if (status[v] == STATUS::UNSTARTED) {
                if (auto res = dfs(v); !res)
                    return res;
            } else if (status[v] == STATUS::WORKING) {
                return std::unexpected(std::format("Cycle detected in the build graph at: {}", nodes_[v].path));
            }
        }
        status[u] = STATUS::FINISHED;
        order.push_back(u);
        return {};
    };

    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (status[i] == STATUS::UNSTARTED) {
            if (auto res = dfs(i); !res)
                return std::unexpected(res.error());
        }
    }

    std::reverse(order.begin(), order.end());
    return order;
}

} // namespace catalyst
