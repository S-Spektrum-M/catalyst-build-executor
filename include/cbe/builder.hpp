#pragma once

#include "cbe/graph.hpp"

#include <filesystem>
#include <memory>

namespace catalyst {

/**
 * @brief Builder class for constructing the build graph.
 *
 * This class acts as a facade for populating the `BuildGraph` and `Definitions`.
 * It is used by parsers to construct the build representation.
 */
class CBEBuilder {
public:
    /**
     * @brief Adds a build step to the underlying graph.
     * @param bs The build step to add.
     * @return Success or error.
     */
    Result<void> add_step(BuildStep &&bs);

    const BuildGraph &graph() const {
        return graph_;
    }
    
    /**
     * @brief Returns the built graph by moving it out of the builder.
     * @return The completed `BuildGraph`.
     */
    BuildGraph &&emit_graph() {
        return std::move(graph_);
    }

    /**
     * @brief Adds a global definition/variable.
     * @param key The name of the definition.
     * @param value The value of the definition.
     */
    void add_definition(std::string_view key, std::string_view value) {
        definitions_.emplace(key, value);
    }

    /**
     * @brief Registers a resource to be managed by the graph's lifetime.
     * @param res A shared pointer to the resource.
     */
    void add_resource(std::shared_ptr<void> res) {
        graph_.add_resource(std::move(res));
    }

    const Definitions &definitions() const {
        return definitions_;
    }

    friend Result<void> parse(CBEBuilder &, const std::filesystem::path &);
    friend Result<void> parse_bin(CBEBuilder &);
    friend Result<void> emit_bin(CBEBuilder &);

private:
    BuildGraph graph_;
    Definitions definitions_;
};

} // namespace catalyst
