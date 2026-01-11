#pragma once

#include "cbe/utility.hpp"

#include <future>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace catalyst {
/**
 * @brief Executes a subprocess.
 *
 * @param args The command line arguments (first argument is the executable).
 * @param working_dir Optional working directory for the subprocess.
 * @param env Optional environment variables to extend/override the parent environment.
 * @return The exit code of the process on success, or -1 on error.
 */
Result<int> process_exec(std::vector<std::string> &&args,
                         std::optional<std::string> working_dir = std::nullopt,
                         std::optional<std::unordered_map<std::string, std::string>> env = std::nullopt);
} // namespace catalyst
