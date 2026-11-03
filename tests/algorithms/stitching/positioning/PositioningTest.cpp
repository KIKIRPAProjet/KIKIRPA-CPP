// Outil de test : calcule les positions des tuiles (SIFT 16 bits -> RANSAC -> moindres carrés) sans coupe de graphe.
// Usage : positioning_test [--chevauchement=0.3] <tuile1_rLL_cCC.tif> [tuile2 ...]
#include "algorithms/stitching/positioning/TilePositioner.h"
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    float overlap = 0.3f;
    std::string rejector; // [AD 22/10/2026 08:40]
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind("--chevauchement=", 0) == 0) {
            try { overlap = std::stof(a.substr(16)); } catch (...) { overlap = -1.f; }
        } else if (a.rfind("--rejector=", 0) == 0) { // [AD 22/10/2026 08:40]
            rejector = a.substr(11);                 // [AD 22/10/2026 08:40]
        } else paths.push_back(a);
    }
    if (paths.empty() || !(overlap > 0.f && overlap <= 1.f)) {
        std::cerr << "Usage: " << argv[0] << " [--chevauchement=0.3] [--rejector=ransac|vote] <tuile1_rLL_cCC.tif> [tuile2 ...]\n";
        return 2;
    }

    try {
        stitching::positioning::PositioningSettings settings;
        settings.overlap = overlap;
        if (!rejector.empty()) settings.rejector = rejector; // [AD 22/10/2026 08:40]
        std::vector<stitching::TilePositionData> positions;
        if (!stitching::positioning::computeTilePositions(paths, settings, positions, std::cout)) return 1;

        std::cout << std::setprecision(12) << "\nstd::vector<stitching::TilePositionData> tilePositions = {\n";
        for (const auto& t : positions)
            std::cout << "    {\"" << t.tile_id << "\", {" << t.position.x << ", " << t.position.y << "}},\n";
        std::cout << "};\n";
        return positions.size() == paths.size() ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Positioning failed: " << e.what() << '\n';
        return 1;
    }
}
