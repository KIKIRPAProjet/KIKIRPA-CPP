#include "algorithms/rejection/ransac/RansacRejector.h"

#include "algorithms/rejection/ransac/RansacAlgorithm.h"
#include "algorithms/rejection/ransac/RansacPreprocessor.h"
#include "algorithms/rejection/ransac/RansacTypes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace stitching::rejection {

namespace {

ransac::RansacParameters makeParameters(
    const Json& params
)
{
    ransac::RansacParameters result;

    // --------------------------------------------------------
    // Nombre minimum de matches
    // --------------------------------------------------------

    result.minMatches =
        std::max(
            2,
            params.value(
                "minMatches",
                4
            )
        );

    // --------------------------------------------------------
    // Seuil de reprojection RANSAC
    // --------------------------------------------------------

    result.reprojectionThreshold =
        std::max(
            0.000001,
            params.value(
                "ransacReprojThreshold",
                3.0
            )
        );

    // --------------------------------------------------------
    // Nombre maximum d'itérations
    // --------------------------------------------------------

    const int maxIters =
        params.value(
            "maxIters",
            2000
        );

    result.maxIters =
        static_cast<std::size_t>(
            std::max(
                1,
                maxIters
            )
        );

    // --------------------------------------------------------
    // Niveau de confiance
    // --------------------------------------------------------

    result.confidence =
        std::clamp(
            params.value(
                "confidence",
                0.99
            ),
            0.000001,
            0.999999
        );

    // --------------------------------------------------------
    // Raffinement final
    // --------------------------------------------------------

    const int refineIters =
        params.value(
            "refineIters",
            10
        );

    result.refineIters =
        static_cast<std::size_t>(
            std::max(
                0,
                refineIters
            )
        );

    // --------------------------------------------------------
    // Nombre minimum d'inliers
    // --------------------------------------------------------

    result.minInliers =
        std::max(
            2,
            params.value(
                "minInliers",
                6
            )
        );

    return result;
}

} // namespace


RansacRejector::RansacRejector(
    const Json& p
)
    : params(p.is_object() ? p : Json::object())
{
}


bool RansacRejector::estimate(
    const Matches& matches,
    Alignment& out
)
{
    // --------------------------------------------------------
    // 1. Nettoyage de la sortie du pipeline
    // --------------------------------------------------------

    out.transform.release();
    out.inliers.clear();

    // --------------------------------------------------------
    // 2. Conversion des paramètres JSON vers une structure
    // propre à RANSAC.
    // --------------------------------------------------------

    const ransac::RansacParameters settings =
        makeParameters(params);

    // --------------------------------------------------------
    // 3. STRUCTURE D'ENTREE
    //
    // On cherche une transformation :
    //
    // B -> A
    //
    // source      = pointsB
    // destination = pointsA
    // --------------------------------------------------------

    const ransac::RansacInput input{
        matches.pointsB,
        matches.pointsA
    };

    // --------------------------------------------------------
    // 4. PRETRAITEMENT
    // --------------------------------------------------------

    ransac::RansacPreprocessor preprocessor;

    ransac::RansacPreparedInput preparedInput;

    if (
        !preprocessor.prepare(
            input,
            settings,
            preparedInput
        )
    ) {
        return false;
    }

    // --------------------------------------------------------
    // 5. ALGORITHME RANSAC
    // --------------------------------------------------------

    ransac::RansacAlgorithm algorithm;

    ransac::RansacOutput result;

    if (
        !algorithm.estimate(
            preparedInput,
            settings,
            result
        )
    ) {
        return false;
    }

    // --------------------------------------------------------
    // 6. Conversion de la sortie interne RANSAC
    // vers la sortie générique du projet.
    // --------------------------------------------------------

    out.transform =
        result.transform.clone();

    out.inliers =
        std::move(
            result.inliers
        );

    return true;
}

// ============================================================================
// [AD 21/10/2026 20:41] Implémentation de VoteRejector (Vote de Hough 2D + Médiane itérative)
// ============================================================================
// Principe : Sur un banc mécanique rigide (rail X-Y), les tuiles ne tournent pas.
// Un modèle de pure translation 2D supprime le risque d'overfitting angulaire
// (faux angle induit par le bruit sous-pixel sur une bande étroite de recouvrement).
// L'algorithme s'exécute en temps linéaire O(N), est déterministe et résiste
// jusqu'à 90% d'aberrants (cf. Section 7 des notebooks KIK-IRPA 02_sift et 03_lightglue).
// ============================================================================

VoteRejector::VoteRejector(const Json& p) : params(p.is_object() ? p : Json::object()) {}

bool VoteRejector::estimate(const Matches& matches, Alignment& out) {
    out.transform.release();
    out.inliers.clear();

    // 1. Paramètres avec valeurs par défaut alignées sur les notebooks de recherche KIK-IRPA
    ransac::RansacParameters settings = makeParameters(params);
    settings.minMatches = std::max(2, params.value("minMatches", 3));
    settings.minInliers = std::max(2, params.value("minInliers", 3));
    const double seuil = std::max(1e-6, params.value("seuil", params.value("ransacReprojThreshold", 1.5)));
    const double c = std::max(1e-6, params.value("case", 1.0)); // taille de cellule de l'accumulateur (1 px)

    // 2. Prétraitement : réutilisation de RansacPreprocessor (filtre NaN/Inf et conserve originalIndices)
    ransac::RansacPreprocessor preprocessor;
    ransac::RansacPreparedInput prep;
    if (!preprocessor.prepare({matches.pointsB, matches.pointsA}, settings, prep)) return false;

    // 3. Accumulation 2D avec lissage par voisinage 3x3 (équivalent exact à cv2.blur(hist, (3, 3)))
    // Chaque point d_k = dest - src vote pour sa cellule (ix, iy) et ses 8 voisines directes.
    // L'usage de std::map sparse évite d'allouer une matrice dense géante si des faux appariements sont très éloignés.
    const std::size_t n = prep.sourcePoints.size();
    std::vector<double> dx(n), dy(n);
    std::map<std::pair<int, int>, int> votes;
    for (std::size_t i = 0; i < n; ++i) {
        dx[i] = static_cast<double>(prep.destinationPoints[i].x) - prep.sourcePoints[i].x;
        dy[i] = static_cast<double>(prep.destinationPoints[i].y) - prep.sourcePoints[i].y;
        const int ix = static_cast<int>(std::floor(dx[i] / c));
        const int iy = static_cast<int>(std::floor(dy[i] / c));
        for (int dy_c = -1; dy_c <= 1; ++dy_c)
            for (int dx_c = -1; dx_c <= 1; ++dx_c)
                ++votes[{iy + dy_c, ix + dx_c}];
    }

    // 4. Extraction déterministe du mode (la cellule ayant reçu le maximum de votes)
    auto best = votes.begin();
    for (auto it = votes.begin(); it != votes.end(); ++it)
        if (it->second > best->second) best = it;

    double tx = (best->first.second + 0.5) * c;
    double ty = (best->first.first + 0.5) * c;
    auto med = [](std::vector<double>& v) {
        const auto m = v.begin() + v.size() / 2;
        std::nth_element(v.begin(), m, v.end());
        double val = *m;
        if (v.size() % 2 == 0) val = 0.5 * (val + *std::max_element(v.begin(), m));
        return val;
    };

    // 5. Raffinement par médiane itérative (3 passes) sur les correspondances dans le rayon 'seuil' (1.5 px)
    // Permet d'obtenir une précision sous-pixel insensible aux aberrants restés sous le seuil.
    for (int iter = 0; iter < 3; ++iter) {
        std::vector<double> inlX, inlY;
        for (std::size_t i = 0; i < n; ++i)
            if (std::hypot(dx[i] - tx, dy[i] - ty) <= seuil) { inlX.push_back(dx[i]); inlY.push_back(dy[i]); }
        if (inlX.empty()) break;
        tx = med(inlX);
        ty = med(inlY);
    }

    // 6. Marquage final des inliers dans le repère des Matches d'origine
    out.inliers.assign(prep.originalCount, 0);
    int inlierCount = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (std::hypot(dx[i] - tx, dy[i] - ty) <= seuil) {
            out.inliers[prep.originalIndices[i]] = 1;
            ++inlierCount;
        }
    }
    if (inlierCount < settings.minInliers) { out.inliers.assign(prep.originalCount, 0); return false; }

    // 7. Matrice homogène de pure translation 3x3 (B -> A) : rotation parasite strictement nulle (theta = 0)
    out.transform = cv::Mat::eye(3, 3, CV_64F);
    out.transform.at<double>(0, 2) = tx;
    out.transform.at<double>(1, 2) = ty;
    return true;
}

} // namespace stitching::rejection