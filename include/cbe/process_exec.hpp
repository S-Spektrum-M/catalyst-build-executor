#pragma once

#include "cbe/utility.hpp"

#include <future>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace catalyst {
Result<int> process_exec(const std::vector<std::string_view> &args);
Result<std::future<int>> process_exec(std::vector<std::string> &&args,
                                      std::optional<std::string> working_dir = std::nullopt,
                                      std::optional<std::unordered_map<std::string, std::string>> env = std::nullopt);
} // namespace catalyst
