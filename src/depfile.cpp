#include "depfile.hpp"

#include <fstream>
#include <iostream>
#include <vector>

namespace catalyst {

Depfile parse_depfile(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open depfile: " + path.string());
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Depfile result;
    std::string current_token;
    bool in_target = true;
    bool escape = false;

    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];

        if (escape) {
            // If it was a backslash...
            if (c == '\n') {
                // nop
            } else if (c == '\r') {
                if (i + 1 < content.size() && content[i + 1] == '\n') {
                    i++;
                }
            } else {
                // Escaped character (e.g. space), keep literal
                current_token += c;
            }
            escape = false;
        } else {
            if (c == '\\') {
                escape = true;
            } else if (c == ':' && in_target) {
                if (!current_token.empty()) {
                    result.target = current_token;
                    current_token.clear();
                }
                in_target = false;
            } else if (std::isspace(c)) {
                if (!current_token.empty()) {
                    if (in_target) {
                        result.target = current_token;
                        current_token.clear();
                    } else {
                        result.dependencies.push_back(current_token);
                        current_token.clear();
                    }
                }
            } else {
                current_token += c;
            }
        }
    }

    if (!current_token.empty()) {
        if (in_target) {
            result.target = current_token;
        } else {
            result.dependencies.push_back(current_token);
        }
    }

    return result;
}

} // namespace catalyst
