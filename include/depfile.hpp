#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace catalyst {

struct Depfile {
    std::string target;
    std::vector<std::string> dependencies;
};

Depfile parse_depfile(const std::filesystem::path &path);

} // namespace catalyst
