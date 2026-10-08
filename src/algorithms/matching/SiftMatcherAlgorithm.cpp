#include "SiftMatcherAlgorithm.h"
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <stdexcept>

namespace stitching::matching {

bool runSiftMatching(const cv::Mat& a, const cv::Mat& b,
                     const PairContext& ctx, const Json& params, Matches& out) {
    out.pointsA.clear();
    out.pointsB.clear();
    out.scores.clear();
    out.dx = 0.f;
    out.dy = 0.f;

    if (a.empty() || b.empty()) return false;

    const int maxFeatures = params.value("max_features", 500);
    const float ratio = params.value("lowe_ratio", 0.75f);
    const double contrastThreshold = params.value("contrast_threshold", 0.01);
    const double claheClipLimit = params.value("clahe_clip_limit", 2.0);
    if (maxFeatures <= 0 || ratio <= 0.f || ratio >= 1.f
        || contrastThreshold <= 0.0 || claheClipLimit < 0.0) {
        throw std::invalid_argument(
            "SIFT parameters must have max_features > 0, lowe_ratio between 0 and 1, "
            "contrast_threshold > 0, and clahe_clip_limit >= 0");
    }

    const auto toGray = [](const cv::Mat& image) {
        cv::Mat gray;
        switch (image.channels()) {
        case 1:
            gray = image;
            break;
        case 3:
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
            break;
        case 4:
            cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
            break;
        default:
            throw std::invalid_argument("SIFT expects 1-, 3-, or 4-channel images");
        }
        return gray;
    };

    const cv::Mat grayA = toGray(a);
    const cv::Mat grayB = toGray(b);
    cv::Rect roiA(0, 0, grayA.cols, grayA.rows);
    cv::Rect roiB(0, 0, grayB.cols, grayB.rows);
    cv::Point2f offsetA(0.f, 0.f);
    cv::Point2f offsetB(0.f, 0.f);

    const float overlap = std::clamp(ctx.overlap, 0.f, 1.f);
    if (overlap > 0.f && ctx.dir == Direction::LeftRight) {
        const int width = std::min({
            static_cast<int>(grayB.cols * overlap), grayA.cols, grayB.cols
        });
        if (width > 0) {
            roiA = cv::Rect(grayA.cols - width, 0, width, grayA.rows);
            roiB = cv::Rect(0, 0, width, grayB.rows);
            offsetA.x = static_cast<float>(grayA.cols - width);
        }
    } else if (overlap > 0.f) {
        const int height = std::min({
            static_cast<int>(grayB.rows * overlap), grayA.rows, grayB.rows
        });
        if (height > 0) {
            roiA = cv::Rect(0, grayA.rows - height, grayA.cols, height);
            roiB = cv::Rect(0, 0, grayB.cols, height);
            offsetA.y = static_cast<float>(grayA.rows - height);
        }
    }

    const cv::Mat cropA = grayA(roiA);
    const cv::Mat cropB = grayB(roiB);

    cv::Mat enhancedA, enhancedB;
    if (claheClipLimit > 0.0) {
        const auto clahe = cv::createCLAHE(claheClipLimit);
        clahe->apply(cropA, enhancedA);
        clahe->apply(cropB, enhancedB);
    } else {
        enhancedA = cropA;
        enhancedB = cropB;
    }

    const auto sift = cv::SIFT::create(
        maxFeatures, 3, contrastThreshold, 10.0, 1.6);
    std::vector<cv::KeyPoint> keypointsA, keypointsB;
    cv::Mat descriptorsA, descriptorsB;
    sift->detectAndCompute(enhancedA, cv::noArray(), keypointsA, descriptorsA);
    sift->detectAndCompute(enhancedB, cv::noArray(), keypointsB, descriptorsB);
    if (descriptorsA.empty() || descriptorsB.empty()
        || descriptorsA.rows < 2 || descriptorsB.rows < 2) {
        return false;
    }

    cv::BFMatcher matcher(cv::NORM_L2);
    std::vector<std::vector<cv::DMatch>> nearest;
    matcher.knnMatch(descriptorsB, descriptorsA, nearest, 2);

    for (const auto& candidates : nearest) {
        if (candidates.size() < 2
            || candidates[0].distance >= ratio * candidates[1].distance) {
            continue;
        }

        const cv::DMatch& match = candidates[0];
        out.pointsA.push_back(keypointsA[match.trainIdx].pt + offsetA);
        out.pointsB.push_back(keypointsB[match.queryIdx].pt + offsetB);
        out.scores.push_back(1.f - match.distance / candidates[1].distance);
    }

    if (out.pointsA.size() < 4) {
        out.pointsA.clear();
        out.pointsB.clear();
        out.scores.clear();
        return false;
    }

    std::vector<float> translationsX, translationsY;
    translationsX.reserve(out.pointsA.size());
    translationsY.reserve(out.pointsA.size());
    for (size_t i = 0; i < out.pointsA.size(); ++i) {
        translationsX.push_back(out.pointsA[i].x - out.pointsB[i].x);
        translationsY.push_back(out.pointsA[i].y - out.pointsB[i].y);
    }

    const auto median = [](std::vector<float>& values) {
        const auto middle = values.begin() + values.size() / 2;
        std::nth_element(values.begin(), middle, values.end());
        return *middle;
    };
    out.dx = median(translationsX);
    out.dy = median(translationsY);
    return true;
}

} // namespace stitching::matching


// Ce que contient out
// out comporte deux listes de points :

// out.pointsA[i] : un point dans l’image A ;
// out.pointsB[i] : le point correspondant dans l’image B.
// Les deux points de même indice forment une correspondance. 
// Par exemple, pointsA[0] correspond à pointsB[0]. Ce sont ces paires que 
// l’étape suivante transmet au rejet des mauvaises correspondances, notamment à RANSAC.
// Comment les correspondances sont obtenues
// Le matcher :

// Convertit les images en niveaux de gris si nécessaire.
// Limite la recherche à la zone de recouvrement estimée, selon ctx.dir et ctx.overlap.
// Détecte les points et descripteurs SIFT.
// Compare les descripteurs avec le plus proche voisin et le filtre de Lowe.
// Ajoute dans out les paires qui passent ce filtre.
// La méthode renvoie true si elle obtient au moins quatre correspondances. Sinon, elle 
// renvoie false ; si elle en a trouvé moins de quatre, les listes sont vidées.

// Les réglages par défaut sont 500 points caractéristiques maximum et un ratio de 
// Lowe de 0,75. On peut les fournir au constructeur via le JSON, 
// par exemple {"max_features": 800, "lowe_ratio": 0.7}.




// Ajout le 07/10 :
// scores[i] est le score de confiance de la paire pointsA[i] / pointsB[i], 
// calculé à partir du ratio de Lowe. Il varie de 0 à 1 ; plus il est élevé, 
// plus le meilleur descripteur se distingue du suivant.
// dx et dy sont les médianes des différences pointA - pointB, 
// donc une estimation de la translation de B vers A.

// L'ia : J’ai ajouté ces champs à Types.h, leur calcul dans SiftMatcher.cpp, 
// et leur inclusion dans les données JSON de l’étape de stitching dans StitchingStep.cpp. 
// Ils apparaîtront donc aussi dans le journal du pipeline.