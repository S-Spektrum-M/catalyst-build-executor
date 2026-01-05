#pragma once

#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace catalyst {

struct BuildStep {
    std::string_view tool;
    std::string_view inputs; // Comma-separated list
    std::string_view output;
    std::optional<std::vector<std::string_view>> depfile_inputs = std::nullopt;
    std::vector<std::string_view> parsed_inputs;
};

using Definitions = std::unordered_map<std::string_view, std::string_view>;

} // namespace catalyst
