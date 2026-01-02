#include "depfile.hpp"

#include <CLI/CLI.hpp>
#include <iostream>
#include <unordered_map>
#include <vector>

struct File;

std::unordered_map<File, std::vector<File>> build_depgraph();

int main(int argc, char **argv) {
    CLI::App app("Catalyst Build Executor");

    std::string depfile_path;
    app.add_option("--depfile", depfile_path, "Path to GCC dependency file to parse");

    CLI11_PARSE(app, argc, argv);

    if (!depfile_path.empty()) {
        try {
            catalyst::Depfile dep = catalyst::parse_depfile(depfile_path);
            std::cout << "Target: " << dep.target << std::endl;
            std::cout << "Dependencies:" << std::endl;
            for (const auto &d : dep.dependencies) {
                std::cout << "  - " << d << std::endl;
            }
        } catch (const std::exception &e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
        return 0;
    }

    std::cout << "Hello, Catalyst!" << std::endl;
}
