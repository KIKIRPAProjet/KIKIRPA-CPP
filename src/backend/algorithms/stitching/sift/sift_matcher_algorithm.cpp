// src/backend/algorithms/stitching/sift/sift_matcher_algorithm.cpp
#include "algorithms/stitching/sift/sift_matcher_algorithm.h"

#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace SiftNs {

    namespace {

        // Conversion en niveaux de gris, SANS PERTE : tout est ramené en CV_16UC1 (8 bits -> x257, 16 bits inchangé).
        // Les images attendues sont des tuiles .tif 16 bits lues par GraphCutNs::ImagePretreatmentClass::read16Bit.
        cv::Mat convertToGrey(const cv::Mat& image) {
            cv::Mat image16Bit;
            switch (image.depth()) {
                case CV_16U:
                    image16Bit = image;
                    break;
                case CV_8U:
                    image.convertTo(image16Bit, CV_MAKETYPE(CV_16U, image.channels()), 257.0);
                    break;
                default:
                    throw std::invalid_argument("SIFT expects 8- or 16-bit integer images");
            }

            cv::Mat grey;
            switch (image16Bit.channels()) {
                case 1:
                    grey = image16Bit;
                    break;
                case 3:
                    cv::cvtColor(image16Bit, grey, cv::COLOR_BGR2GRAY);
                    break;
                case 4:
                    cv::cvtColor(image16Bit, grey, cv::COLOR_BGRA2GRAY);
                    break;
                default:
                    throw std::invalid_argument("SIFT expects 1-, 3-, or 4-channel images");
            }
            return grey;
        }

        // std::nth_element : plutôt que de trier complètement la liste (ce qui prend du temps), réorganise juste les
        // données pour placer la médiane (la valeur du milieu) à sa position exacte, avec tout ce qui est plus petit à
        // gauche et tout ce qui est plus grand à droite.
        float computeMedian(std::vector<float>& values) {
            const auto middle = values.begin() + values.size() / 2;
            std::nth_element(values.begin(), middle, values.end());
            return *middle;
        }

    }

    // & = on ne copie pas l'image en mémoire et const = on ne la modifie pas. matches : un & mais pas de const, c'est
    // la variable de sortie, remplie avec nos résultats.
    bool runSiftMatching(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                         const nlohmann::json& parameters, DataNs::Matches& matches) {
        // Avant de commencer : vide les listes et remet la translation à 0, au cas où matches contiendrait de vieux
        // résultats
        matches.pointsInA.clear();
        matches.pointsInB.clear();
        matches.confidenceScores.clear();
        matches.translationX = 0.f;
        matches.translationY = 0.f;

        if (imageA.empty() || imageB.empty()) return false;   // si image vide on arrête

        // Paramètres : va chercher la valeur dans le JSON. S'il n'y a rien, prend la valeur par défaut (500 points,
        // à changer)
        const int maximumFeatureCount = parameters.value("max_features", 500);
        const float loweRatio = parameters.value("lowe_ratio", 0.75f);
        const double contrastThreshold = parameters.value("contrast_threshold", 0.01);
        const double claheClipLimit = parameters.value("clahe_clip_limit", 2.0);
        if (maximumFeatureCount <= 0 || loweRatio <= 0.f || loweRatio >= 1.f
            || contrastThreshold <= 0.0 || claheClipLimit < 0.0) {
            throw std::invalid_argument(
                "SIFT parameters must have max_features > 0, lowe_ratio between 0 and 1, "
                "contrast_threshold > 0, and clahe_clip_limit >= 0");
        }

        const cv::Mat greyA = convertToGrey(imageA);
        const cv::Mat greyB = convertToGrey(imageB);

        // Les images transmises sont déjà les bandes d'overlap prédécoupées
        (void)context;
        const cv::Mat& cropA = greyA;
        const cv::Mat& cropB = greyB;

        // cv::createCLAHE : si demandé, on crée un algorithme qui va booster les micro-contrastes (les ombres/lumières)
        // pour aider SIFT à voir des détails.
        cv::Mat enhancedA, enhancedB;
        if (claheClipLimit > 0.0) {
            const auto clahe = cv::createCLAHE(claheClipLimit);
            clahe->apply(cropA, enhancedA);
            clahe->apply(cropB, enhancedB);
        } else {
            enhancedA = cropA;
            enhancedB = cropB;
        }

        // cv::SIFT (OpenCV) n'accepte QUE du CV_8U en entrée : c'est la SEULE étape où l'on quantifie, tout en fin de
        // chaîne. La lecture, le niveau de gris et le CLAHE ci-dessus restent en 16 bits. On étire la dynamique réelle
        // conjointe vers [0, 255] avec les mêmes coefficients pour A et B afin de préserver la comparabilité des
        // descripteurs, sans écraser les images 16 bits sombres ou infrarouges (Apollo).
        cv::Mat inputA, inputB;
        double minimumA = 0.0, maximumA = 0.0, minimumB = 0.0, maximumB = 0.0;
        cv::minMaxLoc(enhancedA, &minimumA, &maximumA);
        cv::minMaxLoc(enhancedB, &minimumB, &maximumB);
        const double jointMinimum = std::min(minimumA, minimumB), jointRange = std::max(maximumA, maximumB) - jointMinimum;
        const double scale = (jointRange > 0.0) ? (255.0 / jointRange) : (255.0 / 65535.0);
        enhancedA.convertTo(inputA, CV_8U, scale, (jointRange > 0.0) ? (-jointMinimum * scale) : 0.0);
        enhancedB.convertTo(inputB, CV_8U, scale, (jointRange > 0.0) ? (-jointMinimum * scale) : 0.0);

        const auto siftDetector = cv::SIFT::create(
            maximumFeatureCount, 3, contrastThreshold, 10.0, 1.6);
        std::vector<cv::KeyPoint> keypointsA, keypointsB;
        cv::Mat descriptorsA, descriptorsB;

        // detectAndCompute : on lance SIFT. cv::noArray() signifie qu'on n'utilise pas de masque restrictif. OpenCV
        // remplit keypointsA (les positions X, Y des points) et descriptorsA (l'empreinte mathématique de chaque point).
        siftDetector->detectAndCompute(inputA, cv::noArray(), keypointsA, descriptorsA);
        siftDetector->detectAndCompute(inputB, cv::noArray(), keypointsB, descriptorsB);
        if (descriptorsA.empty() || descriptorsB.empty()
            || descriptorsA.rows < 2 || descriptorsB.rows < 2) {
            return false;
        }

        // BFMatcher : Brute Force Matcher. Il compare chaque point de B avec tous les points de A pour trouver ceux qui
        // se ressemblent le plus. knnMatch(..., 2) : il garde les 2 meilleurs candidats de A pour chaque point de B.
        cv::BFMatcher bruteForceMatcher(cv::NORM_L2);   // Il faudra peut-être changer ça par FLANN pour optimiser ?
        std::vector<std::vector<cv::DMatch>> nearestMatches;
        bruteForceMatcher.knnMatch(descriptorsB, descriptorsA, nearestMatches, 2);

        // Le filtre de Lowe (candidates[0].distance >= loweRatio * candidates[1]...) : si le meilleur match (le n°0)
        // ne se démarque pas assez du deuxième meilleur (le n°1), on le rejette car il est ambigu.
        for (const auto& candidates : nearestMatches) {
            if (candidates.size() < 2
                || candidates[0].distance >= loweRatio * candidates[1].distance) {
                continue;
            }

            const cv::DMatch& bestMatch = candidates[0];
            matches.pointsInA.push_back(keypointsA[bestMatch.trainIdx].pt);   // coordonnées des points valides
            matches.pointsInB.push_back(keypointsB[bestMatch.queryIdx].pt);
            matches.confidenceScores.push_back(1.f - bestMatch.distance / candidates[1].distance);
        }

        // Moins de 4 points trouvés : on efface tout et on renvoie false, pas assez de points d'intérêt
        if (matches.pointsInA.size() < 4) {
            matches.pointsInA.clear();
            matches.pointsInB.clear();
            matches.confidenceScores.clear();
            return false;
        }

        // reserve() : au lieu que la liste demande de la mémoire à chaque point ajouté, on réserve d'un coup la taille
        // exacte nécessaire.
        std::vector<float> translationsX, translationsY;
        translationsX.reserve(matches.pointsInA.size());
        translationsY.reserve(matches.pointsInA.size());
        for (size_t pointIndex = 0; pointIndex < matches.pointsInA.size(); ++pointIndex) {
            translationsX.push_back(matches.pointsInA[pointIndex].x - matches.pointsInB[pointIndex].x);
            translationsY.push_back(matches.pointsInA[pointIndex].y - matches.pointsInB[pointIndex].y);
        }

        // Médiane sur X et sur Y : estimation de la translation de B vers A
        matches.translationX = computeMedian(translationsX);
        matches.translationY = computeMedian(translationsY);
        return true;
    }

}
