#include <pipeline/dependency_resolver.hpp>
#include "diagnostic_utils.hpp"
#include <algorithm>
#include <set>

namespace pipeline {
using detail::issue;
Resolution DependencyResolver::resolve(const std::map<std::string, Step>& steps) const {
    Resolution result;
    std::map<std::string, std::vector<std::string>> graph, successors;
    std::map<std::string, std::size_t> indegree;
    for (const auto& [id, step] : steps) {
        graph[id]; indegree[id] = 0;
        std::set<std::string> seen;
        for (const auto& dependency : step.dependencies) {
            if (!seen.insert(dependency).second) continue;
            if (!steps.count(dependency)) {
                result.errors.push_back(issue("UNKNOWN_DEPENDENCY", "Dependency ID does not exist.", id,
                                              {{"dependencyId", dependency}}));
            } else if (dependency == id) {
                result.errors.push_back(issue("SELF_DEPENDENCY", "Step depends on itself.", id));
            } else {
                graph[id].push_back(dependency);
                successors[dependency].push_back(id);
                ++indegree[id];
            }
        }
        std::sort(graph[id].begin(), graph[id].end());
    }
    // Une pile explicite évite les limites de récursion sur les grands pipelines.
    // Un sommet gris appartient au chemin actif : une arête qui revient vers
    // ce sommet permet de reconstruire un chemin circulaire fermé.
    std::map<std::string, int> color;
    std::map<std::string, std::size_t> position;
    for (const auto& [root, unused] : steps) {
        (void)unused;
        if (color[root]) continue;
        std::vector<std::pair<std::string, std::size_t>> stack{{root, 0}};
        color[root] = 1; position[root] = 0;
        while (!stack.empty()) {
            auto& frame = stack.back();
            const auto& edges = graph[frame.first];
            if (frame.second == edges.size()) {
                color[frame.first] = 2; position.erase(frame.first); stack.pop_back(); continue;
            }
            auto next = edges[frame.second++];
            if (color[next] == 1) {
                std::vector<std::string> cycle;
                for (auto i = position[next]; i < stack.size(); ++i) cycle.push_back(stack[i].first);
                cycle.push_back(next);
                result.errors.push_back(issue("CYCLIC_DEPENDENCY", "Circular dependency path detected.",
                                               frame.first, {{"cycle", cycle}}));
            } else if (!color[next]) {
                color[next] = 1; position[next] = stack.size(); stack.emplace_back(next, 0);
            }
        }
    }
    // L’algorithme de Kahn retire les sommets sans prérequis restant. Un ensemble
    // trié garantit le même ordre, quel que soit l’ordre des étapes et des
    // dépendances dans le fichier JSON.
    std::set<std::string> ready;
    for (const auto& [id, count] : indegree) if (!count) ready.insert(id);
    while (!ready.empty()) {
        auto id = *ready.begin(); ready.erase(ready.begin()); result.order.push_back(id);
        for (const auto& child : successors[id]) if (--indegree[child] == 0) ready.insert(child);
    }
    return result;
}
}
