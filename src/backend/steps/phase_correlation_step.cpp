// src/backend/steps/phase_correlation_step.cpp
#include "steps/phase_correlation_step.h"

#include "core/config/pipeline_config.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace StepsNs {

    using Json = nlohmann::json;

    // Déclare l'identité du step : son nom, ses 15 paramètres (valeurs par défaut = celles de l'entrée), la donnée
    // qu'il lit (tuiles) et celle qu'il produit (appariements). Le pipeline s'en sert pour vérifier l'enchaînement.
    PhaseCorrelationStepClass::PhaseCorrelationStepClass(const PhaseCorrelationInput& initialInput) {
        const auto& correlationParameters = initialInput.parameters;
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "phase-correlation";
        stepConfig.parameters = {{"float", "overlap", initialInput.overlap},
                                 {"int", "niveau", initialInput.level},
                                 {"float", "lissage", correlationParameters.smoothingSigma},
                                 {"float", "seuilRapport", correlationParameters.minimumPeakRatio},
                                 {"float", "lissageLocal", correlationParameters.localSmoothingSigma},
                                 {"float", "ecartMax", correlationParameters.maximumLocalDeviation},
                                 {"float", "espacementMin", correlationParameters.minimumPointSpacing},
                                 {"float", "marge", correlationParameters.railMargin},
                                 {"float", "fractionValideMin", correlationParameters.minimumValidFraction},
                                 {"int", "nbPoints", correlationParameters.maximumPointCount},
                                 {"int", "fenetre", correlationParameters.patchSize},
                                 {"int", "facteurDft", correlationParameters.upsamplingFactor},
                                 {"float", "nccMin", initialInput.confiance.nccMin},
                                 {"float", "dispersionMax", initialInput.confiance.dispersionMax},
                                 {"float", "autosimMax", initialInput.confiance.autosimMax}};
        stepConfig.inputs = {{"Tiles", "tiles"}};
        stepConfig.outputs = {{"PairMatches", "matches"}};
        configJson = stepConfig;
    }

    cv::Mat PhaseCorrelationStepClass::readPixels(const DataNs::TileClass& tile, int level) {
        cv::Mat image = cv::imread(tile.getSource().string(), cv::IMREAD_UNCHANGED);   // IMREAD_UNCHANGED garde les 16 bits
        // Mêmes réductions successives que createPyramidalTiledTiff : le résultat sera celui du niveau du TIFF.
        for (int levelIndex = 0; levelIndex < level && !image.empty(); ++levelIndex)
            cv::resize(image, image, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
        return image;
    }

    void PhaseCorrelationStepClass::scaleToFullResolution(DataNs::Matches& matches, int level) {
        if (level == 0) return;
        const float scale = static_cast<float>(1 << level);   // 2^level
        // Le pixel p du niveau réduit moyenne les pixels [p·scale, (p + 1)·scale[ : son centre est en (p + 0,5)·scale − 0,5.
        auto convertPoint = [scale](cv::Point2f& point) { point = (point + cv::Point2f(0.5f, 0.5f)) * scale - cv::Point2f(0.5f, 0.5f); };
        for (auto& point : matches.pointsInA) convertPoint(point);
        for (auto& point : matches.pointsInB) convertPoint(point);
        matches.translationX *= scale;
        matches.translationY *= scale;
    }

    double PhaseCorrelationStepClass::selfSimilarity(const cv::Mat& imageA, DataNs::Direction direction, float overlap,
                                                     double margin) {
        // Bande de A du côté de B, en gris float
        const bool bottom = direction == DataNs::Direction::Bottom;
        const int side = bottom ? imageA.rows : imageA.cols;
        const int thickness = static_cast<int>(std::lround(overlap * side));
        if (thickness <= 0 || thickness > side) return NAN;
        const cv::Rect bandRect = bottom ? cv::Rect(0, imageA.rows - thickness, imageA.cols, thickness)
                                         : cv::Rect(imageA.cols - thickness, 0, thickness, imageA.rows);
        cv::Mat gray = imageA(bandRect), band, blurred;
        if (gray.channels() > 1) cv::cvtColor(gray, gray, gray.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        gray.convertTo(band, CV_32F);
        const cv::Mat notEmpty = (band != 0);   // noir exact = hors de l'œuvre
        if (cv::countNonZero(notEmpty) < static_cast<int>(band.total() / 4)) return NAN;

        // Passe-haut : les grandes taches d'une texture normale se ressemblent à quelques px près, pas le grain fin
        cv::GaussianBlur(band, blurred, {0, 0}, 4.0);
        band -= blurred;
        // Écrêtage à ±3 écarts robustes (1,4826 × médiane des |valeurs| non vides) : un bord net (fond / peinture)
        // aurait sinon un poids énorme, et un bord droit se superpose à lui-même quand on le décale le long du bord
        std::vector<float> values;
        for (int y = 0; y < band.rows; ++y)
            for (int x = 0; x < band.cols; ++x)
                if (notEmpty.at<uchar>(y, x)) values.push_back(std::abs(band.at<float>(y, x)));
        std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
        const float ceiling = 3 * 1.4826f * values[values.size() / 2];
        if (ceiling > 0) cv::min(cv::max(band, -ceiling), ceiling, band);

        // Décalages cherchés : la marge du rail (même règle par défaut que Preparation::margeRail), hors ±10 px
        constexpr int NEIGHBOURHOOD = 10;   // px : comme le voisinage exclu autour du pic (Algorithme::RAYON_PIC)
        const double railMargin = margin > 0 ? margin : std::max(0.05 * side, 48.0);
        const int my = std::min(static_cast<int>(railMargin), (band.rows - 16) / 2);
        const int mx = std::min(static_cast<int>(railMargin), (band.cols - 16) / 2);
        if (my <= NEIGHBOURHOOD || mx <= NEIGHBOURHOOD) return NAN;   // bande trop étroite pour chercher une répétition
        const cv::Mat centre = band(cv::Rect(mx, my, band.cols - 2 * mx, band.rows - 2 * my));
        cv::Mat map;   // (2·my + 1) × (2·mx + 1), décalage nul au centre
        cv::matchTemplate(band, centre, map, cv::TM_CCOEFF_NORMED);
        map(cv::Rect(mx - NEIGHBOURHOOD, my - NEIGHBOURHOOD, 2 * NEIGHBOURHOOD + 1, 2 * NEIGHBOURHOOD + 1)).setTo(-1);
        double maximum;
        cv::minMaxLoc(map, nullptr, &maximum);
        return maximum;
    }

    const char* PhaseCorrelationStepClass::stateName(ConfidenceState state) {
        switch (state) {
            case ConfidenceState::Reliable:   return "fiable";
            case ConfidenceState::Unverified: return "non_verifie";
            case ConfidenceState::Ambiguous:  return "ambigu";
            case ConfidenceState::Rejected:   return "rejete";
        }
        return "";
    }

    Json PhaseCorrelationStepClass::toJson(const PairConfidence& confidence) {
        auto rounded = [](double x) { return std::round(x * 1000) / 1000; };
        auto value = [&](const std::optional<double>& x) { return x && std::isfinite(*x) ? Json(rounded(*x)) : Json(); };
        Json points = Json::array();
        for (const PointConfidence& point : confidence.points)
            points.push_back({{"a", {rounded(point.pointInA.x), rounded(point.pointInA.y)}},
                              {"b", {rounded(point.pointInB.x), rounded(point.pointInB.y)}},
                              {"ncc", rounded(point.ncc)}});
        return {{"idA", confidence.tileIdA}, {"idB", confidence.tileIdB},
                {"dir", confidence.direction == DataNs::Direction::Bottom ? "Bottom" : "Right"},
                {"etat", stateName(confidence.state)}, {"ncc", value(confidence.ncc)},
                {"dispersion", value(confidence.dispersion)}, {"autosim", value(confidence.autosim)}, {"points", points}};
    }

    PairConfidence PhaseCorrelationStepClass::checkConfidence(const DataNs::Matches& matches, const cv::Mat& imageA,
                                                              DataNs::Direction direction, float overlap,
                                                              const PhaseCorrelationNs::Parameters& parameters,
                                                              const ConfidenceControl& control) {
        auto number = [](double x) {
            char text[32];
            std::snprintf(text, sizeof text, "%.2f", x);
            return std::string(text);
        };
        auto median = [](std::vector<double> x) {
            std::nth_element(x.begin(), x.begin() + x.size() / 2, x.end());
            return x[x.size() / 2];
        };

        // 1. Tous les indicateurs mesurables
        PairConfidence confidence;
        confidence.direction = direction;
        const bool hasPoints = !matches.pointsInA.empty();
        if (hasPoints) {
            std::vector<double> gaps;
            for (size_t i = 0; i < matches.pointsInA.size(); ++i) {
                const cv::Point2f gap = matches.pointsInA[i] - matches.pointsInB[i]
                                        - cv::Point2f(matches.translationX, matches.translationY);
                gaps.push_back(std::hypot(gap.x, gap.y));
            }
            confidence.ncc = median(std::vector<double>(matches.confidenceScores.begin(), matches.confidenceScores.end()));
            confidence.dispersion = median(gaps);
        }
        const double autosim = selfSimilarity(imageA, direction, overlap, parameters.railMargin);
        if (!std::isnan(autosim)) confidence.autosim = autosim;

        // 2. Décision
        auto reject = [&confidence](ConfidenceState state, std::string reason) {
            confidence.state = state;
            confidence.reason = std::move(reason);
            return confidence;
        };
        // Points locaux (fond sombre, bruit) : le pic peut être franc mais décalé, les patchs le révèlent
        const bool checkPoints = control.nccMin > 0 || control.dispersionMax > 0;
        if (!hasPoints && checkPoints && parameters.maximumPointCount > 0) {
            // Pas de point : soit la zone commune est trop petite pour un patch (rien à vérifier, la paire est gardée),
            // soit elle est assez grande mais aucun point n'a été jugé fiable (bruit, zone sans texture) : rejet.
            // Taille minimale : celle qu'exige PreparationClass::extractOverlap pour chercher des points.
            const int patchMargin = parameters.patchSize / 2 + static_cast<int>(std::ceil(parameters.maximumLocalDeviation)) + 1;
            const int minimum = parameters.patchSize + 2 * patchMargin;
            const int width = imageA.cols - static_cast<int>(std::lround(std::abs(matches.translationX)));
            const int height = imageA.rows - static_cast<int>(std::lround(std::abs(matches.translationY)));
            if (width >= minimum && height >= minimum)
                return reject(ConfidenceState::Rejected, "confiance : aucun point local fiable alors que la zone commune (" +
                              std::to_string(width) + " x " + std::to_string(height) +
                              " px) permet d'en placer : bruit ou zone sans texture, translation non vérifiable");
        }
        if (hasPoints && control.nccMin > 0 && *confidence.ncc < control.nccMin)
            return reject(ConfidenceState::Rejected, "confiance : patchs locaux peu ressemblants (NCC médiane " +
                          number(*confidence.ncc) + " < nccMin " + number(control.nccMin) +
                          ") : fond sombre ou bruit, la translation peut être décalée de plusieurs px");
        if (hasPoints && control.dispersionMax > 0 && *confidence.dispersion > control.dispersionMax)
            return reject(ConfidenceState::Rejected, "confiance : points locaux en désaccord avec la translation (écart médian " +
                          number(*confidence.dispersion) + " px > dispersionMax " + number(control.dispersionMax) +
                          " px) : fond sombre ou bruit dans une partie de la bande");
        // Motif répété de période inférieure à la marge : plusieurs positions de B sont possibles
        if (control.autosimMax < 1 && confidence.autosim && *confidence.autosim >= control.autosimMax)
            return reject(ConfidenceState::Ambiguous, "confiance : motif répété dans la bande de recouvrement (auto-similarité " +
                          number(*confidence.autosim) + " >= autosimMax " + number(control.autosimMax) +
                          ") : mesure ambiguë, elle peut être fausse d'une période");
        confidence.state = hasPoints ? ConfidenceState::Reliable : ConfidenceState::Unverified;
        return confidence;
    }

    // Étapes : 1. lire et contrôler les paramètres ; 2. créer l'algorithme ; 3. pour chaque tuile et chacune de ses
    // voisines (droite, dessous), lire les deux images, mesurer, ramener en pleine résolution ; 4. publier les résultats.
    bool PhaseCorrelationStepClass::execute() {
        output = {};
        result = Json::object();

        // Paramètres relus dans la configuration : un JSON chargé après la construction remplace les valeurs par
        // défaut. Un paramètre absent (JSON écrit avant son ajout) garde la valeur par défaut de PhaseCorrelationInput.
        PhaseCorrelationInput input;
        auto readParameter = [this](const char* parameterName, auto& value) {
            try {
                value = getParameter(parameterName).get<std::decay_t<decltype(value)>>();
            } catch (const std::out_of_range&) {}
        };
        try {
            auto& correlationParameters = input.parameters;
            readParameter("overlap", input.overlap);
            readParameter("niveau", input.level);
            readParameter("lissage", correlationParameters.smoothingSigma);
            readParameter("seuilRapport", correlationParameters.minimumPeakRatio);
            readParameter("lissageLocal", correlationParameters.localSmoothingSigma);
            readParameter("ecartMax", correlationParameters.maximumLocalDeviation);
            readParameter("espacementMin", correlationParameters.minimumPointSpacing);
            readParameter("marge", correlationParameters.railMargin);
            readParameter("fractionValideMin", correlationParameters.minimumValidFraction);
            readParameter("nbPoints", correlationParameters.maximumPointCount);
            readParameter("fenetre", correlationParameters.patchSize);
            readParameter("facteurDft", correlationParameters.upsamplingFactor);
            readParameter("nccMin", input.confiance.nccMin);
            readParameter("dispersionMax", input.confiance.dispersionMax);
            readParameter("autosimMax", input.confiance.autosimMax);
        } catch (const Json::exception& exception) {
            result["error"] = std::string("invalid option type: ") + exception.what();
            return false;
        }

        // Contrôles : recouvrement dans ]0, 1[, niveau existant, tuiles présentes dans le DataStoreClass.
        if (!(input.overlap > 0.f && input.overlap < 1.f)) {
            result["error"] = "overlap must be in ]0, 1[, got " + std::to_string(input.overlap);
            return false;
        }
        if (input.level < 0 || input.level >= DataNs::LEVEL_COUNT) {
            result["error"] = "niveau must be in [0, " + std::to_string(DataNs::LEVEL_COUNT - 1) + "], got "
                              + std::to_string(input.level);
            return false;
        }
        if (!dataStore || !dataStore->has("tiles")) {
            result["error"] = "no tiles: run the import step first";
            return false;
        }

        // L'algorithme valide ses réglages à la construction (std::invalid_argument si l'un est incohérent).
        std::optional<PhaseCorrelationNs::PhaseCorrelationMatcherClass> matcher;
        try {
            matcher.emplace(input.parameters);
        } catch (const std::invalid_argument& exception) {
            result["error"] = std::string("invalid parameters: ") + exception.what();
            return false;
        }

        // Au plus deux images en mémoire : la tuile courante (lue une seule fois pour ses deux voisines) et sa voisine.
        Json failures = Json::array(), confidences = Json::array();
        const float scale = static_cast<float>(1 << input.level);
        for (const auto& tile : dataStore->get<DataNs::TileList>("tiles")) {
            const std::pair<DataNs::TileClass*, DataNs::Direction> neighbours[] = {{tile->getRight(), DataNs::Direction::Right},
                                                                                    {tile->getBottom(), DataNs::Direction::Bottom}};
            cv::Mat tileImage;
            for (const auto& [neighbour, direction] : neighbours) {
                if (!neighbour) continue;
                if (tileImage.empty()) tileImage = readPixels(*tile, input.level);
                const cv::Mat neighbourImage = readPixels(*neighbour, input.level);
                if (tileImage.empty() || neighbourImage.empty()) {
                    result["error"] = "cannot read " + (tileImage.empty() ? tile.get() : neighbour)->getSource().string();
                    return false;
                }

                // Mesure sur les images du niveau analysé, contrôles de confiance, puis conversion en pleine résolution.
                // Une paire qui échoue (mesure impossible ou contrôle) est notée avec sa raison, sans arrêter le step.
                DataNs::PairMatches pair{tile->getId(), neighbour->getId(), direction, {}};
                PairConfidence confidence;
                if (matcher->match(tileImage, neighbourImage, DataNs::PairContext{direction, input.overlap}, pair.featureMatches)) {
                    confidence = checkConfidence(pair.featureMatches, tileImage, direction, input.overlap,
                                                 input.parameters, input.confiance);
                    scaleToFullResolution(pair.featureMatches, input.level);   // points et dispersion en pleine résolution
                    if (confidence.dispersion) *confidence.dispersion *= scale;
                    const DataNs::Matches& m = pair.featureMatches;
                    for (size_t i = 0; i < m.pointsInA.size(); ++i)
                        confidence.points.push_back({m.pointsInA[i], m.pointsInB[i], m.confidenceScores[i]});
                } else {
                    confidence.state = ConfidenceState::Rejected;
                    confidence.reason = matcher->getFailureReason();
                }
                confidence.tileIdA = pair.tileIdA;
                confidence.tileIdB = pair.tileIdB;
                confidence.direction = direction;
                confidences.push_back(toJson(confidence));
                if (confidence.reason.empty()) output.pairs.push_back(std::move(pair));
                else failures.push_back({{"idA", pair.tileIdA}, {"idB", pair.tileIdB}, {"reason", confidence.reason}});
                output.confidence.push_back(std::move(confidence));
            }
        }

        result["niveau"] = input.level;
        result["pairs"] = output.pairs.size();
        if (!failures.empty()) result["failures"] = failures;
        result["confidence"] = confidences;
        if (output.pairs.empty()) {
            result["error"] = "no pair could be measured";
            return false;
        }
        dataStore->replace("matches", output.pairs);   // pour les steps suivants (rejet des aberrants, placement global)
        return true;
    }

}
