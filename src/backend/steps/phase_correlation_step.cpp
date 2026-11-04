// src/backend/steps/phase_correlation_step.cpp
#include "steps/phase_correlation_step.h"

#include "algorithms/stitching/phase_correlation/phase_correlation_matcher.h"
#include "algorithms/stitching/phase_correlation/phase_correlation_types.h"
#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace StepsNs {

    using Json = nlohmann::json;

    namespace {

        constexpr double PYRAMID_REDUCTION = 0.5;   // chaque niveau de la pyramide divise les dimensions par 2
        constexpr float PIXEL_CENTER = 0.5f;

        // Auto-similarité : passe-haut (les grandes taches d'une texture normale se ressemblent à quelques px près, pas le
        // grain fin), puis écrêtage à ±3 écarts robustes (1,4826 × médiane des |valeurs| non vides).
        constexpr double HIGH_PASS_SIGMA = 4.0;
        constexpr float CLIP_DEVIATIONS = 3.0f;
        constexpr float ROBUST_DEVIATION_FACTOR = 1.4826f;
        constexpr int NEIGHBOURHOOD = 10;              // px : comme le voisinage exclu autour du pic de l'algorithme
        constexpr int MINIMUM_CENTER_SIZE = 16;        // px : côté minimal de la partie centrale de la bande
        constexpr double DEFAULT_MARGIN_FRACTION = 0.05;   // marge du rail par défaut, comme l'algorithme
        constexpr double MINIMUM_DEFAULT_MARGIN = 48.0;

        constexpr double JSON_ROUNDING = 1000.0;       // valeurs du JSON arrondies au millième
        constexpr std::size_t NUMBER_TEXT_SIZE = 32;

        // Ce qu'il faut pour mesurer les paires et noter leurs résultats.
        struct MeasureContext {
            const PhaseCorrelationInput& input;
            PhaseCorrelationNs::PhaseCorrelationMatcherClass& matcher;
            PhaseCorrelationOutput& output;
            Json& failures;
            Json& confidences;
        };

        std::string formatNumber(double number) {
            std::array<char, NUMBER_TEXT_SIZE> text{};
            std::snprintf(text.data(), text.size(), "%.2f", number);
            return text.data();
        }

        double computeMedian(std::vector<double> values) {
            const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
            std::nth_element(values.begin(), middle, values.end());
            return *middle;
        }

        // Bande de recouvrement nominal de A du côté de B, en gris float. Vide si son épaisseur n'a pas de sens.
        cv::Mat extractGreyBand(const cv::Mat& imageA, DataNs::Direction direction, float overlap) {
            const bool isBottom = direction == DataNs::Direction::Bottom;
            const int side = isBottom ? imageA.rows : imageA.cols;
            const int thickness = static_cast<int>(std::lround(overlap * static_cast<float>(side)));
            if (thickness <= 0 || thickness > side) return {};
            const cv::Rect bandRect = isBottom ? cv::Rect(0, imageA.rows - thickness, imageA.cols, thickness)
                                               : cv::Rect(imageA.cols - thickness, 0, thickness, imageA.rows);
            cv::Mat grey = imageA(bandRect);
            if (grey.channels() > 1) cv::cvtColor(grey, grey, grey.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
            cv::Mat band;
            grey.convertTo(band, CV_32F);
            return band;
        }

        // Un bord net (fond / peinture) aurait sinon un poids énorme, et un bord droit se superpose à lui-même quand on le
        // décale le long du bord.
        void highPassAndClip(cv::Mat& band, const cv::Mat& notEmpty) {
            cv::Mat blurred;
            cv::GaussianBlur(band, blurred, {0, 0}, HIGH_PASS_SIGMA);
            band -= blurred;

            std::vector<float> values;
            cv::Mat absoluteBand = cv::abs(band);
            for (int y = 0; y < band.rows; ++y) {
                const auto* isKept = notEmpty.ptr<uchar>(y);
                const auto* absolute = absoluteBand.ptr<float>(y);
                for (int x = 0; x < band.cols; ++x) {
                    if (isKept[x] != 0) values.push_back(absolute[x]);
                }
            }
            const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
            std::nth_element(values.begin(), middle, values.end());
            const float ceiling = CLIP_DEVIATIONS * ROBUST_DEVIATION_FACTOR * *middle;
            if (ceiling > 0) cv::min(cv::max(band, -ceiling), ceiling, band);
        }

        // Meilleure ressemblance de la bande avec elle-même, à un décalage non nul dans la marge. NaN si la bande est trop
        // étroite pour chercher une répétition.
        double computeBestShiftedCorrelation(const cv::Mat& band, double margin, int side) {
            const double railMargin = margin > 0 ? margin : std::max(DEFAULT_MARGIN_FRACTION * side, MINIMUM_DEFAULT_MARGIN);
            const int marginY = std::min(static_cast<int>(railMargin), (band.rows - MINIMUM_CENTER_SIZE) / 2);
            const int marginX = std::min(static_cast<int>(railMargin), (band.cols - MINIMUM_CENTER_SIZE) / 2);
            if (marginY <= NEIGHBOURHOOD || marginX <= NEIGHBOURHOOD) return NAN;

            const cv::Mat center = band(cv::Rect(marginX, marginY, band.cols - 2 * marginX, band.rows - 2 * marginY));
            cv::Mat map;   // (2·marginY + 1) × (2·marginX + 1), décalage nul au centre
            cv::matchTemplate(band, center, map, cv::TM_CCOEFF_NORMED);
            const int excludedSize = 2 * NEIGHBOURHOOD + 1;
            map(cv::Rect(marginX - NEIGHBOURHOOD, marginY - NEIGHBOURHOOD, excludedSize, excludedSize)).setTo(-1);
            double maximum = 0.0;
            cv::minMaxLoc(map, nullptr, &maximum);
            return maximum;
        }

        // Tous les indicateurs mesurables d'une paire, sans décision.
        PairConfidence measureIndicators(const DataNs::Matches& matches, const cv::Mat& imageA,
                                         const ConfidenceContext& context) {
            PairConfidence confidence;
            confidence.direction = context.direction;
            if (!matches.pointsInA.empty()) {
                std::vector<double> gaps;
                const cv::Point2f translation(matches.translationX, matches.translationY);
                for (std::size_t index = 0; index < matches.pointsInA.size(); ++index) {
                    const cv::Point2f gap = matches.pointsInA[index] - matches.pointsInB[index] - translation;
                    gaps.push_back(std::hypot(gap.x, gap.y));
                }
                confidence.ncc = computeMedian(std::vector<double>(matches.confidenceScores.begin(), matches.confidenceScores.end()));
                confidence.dispersion = computeMedian(gaps);
            }
            const double autosim = PhaseCorrelationStepClass::selfSimilarity(imageA, context.direction, context.overlap,
                                                                             context.parameters.railMargin);
            if (!std::isnan(autosim)) confidence.autosim = autosim;
            return confidence;
        }

        // Pas de point : soit la zone commune est trop petite pour un patch (rien à vérifier, la paire est gardée), soit
        // elle est assez grande mais aucun point n'a été jugé fiable (bruit, zone sans texture) : rejet. Taille minimale :
        // celle qu'exige PreparationClass::extractOverlap pour chercher des points.
        std::string findMissingPointsReason(const DataNs::Matches& matches, const cv::Mat& imageA,
                                            const ConfidenceContext& context) {
            const auto& parameters = context.parameters;
            const bool checksPoints = context.control.nccMin > 0 || context.control.dispersionMax > 0;
            if (!checksPoints || parameters.maximumPointCount <= 0) return {};
            const int patchMargin = parameters.patchSize / 2 + static_cast<int>(std::ceil(parameters.maximumLocalDeviation)) + 1;
            const int minimum = parameters.patchSize + 2 * patchMargin;
            const int width = imageA.cols - static_cast<int>(std::lround(std::abs(matches.translationX)));
            const int height = imageA.rows - static_cast<int>(std::lround(std::abs(matches.translationY)));
            if (width < minimum || height < minimum) return {};
            return "confiance : aucun point local fiable alors que la zone commune (" + std::to_string(width) + " x " +
                   std::to_string(height) + " px) permet d'en placer : bruit ou zone sans texture, translation non vérifiable";
        }

        // Points locaux (fond sombre, bruit) : le pic peut être franc mais décalé, les patchs le révèlent.
        std::string findPointsReason(const PairConfidence& confidence, const ConfidenceControl& control) {
            if (control.nccMin > 0 && confidence.ncc && *confidence.ncc < control.nccMin) {
                return "confiance : patchs locaux peu ressemblants (NCC médiane " + formatNumber(*confidence.ncc) + " < nccMin " +
                       formatNumber(control.nccMin) + ") : fond sombre ou bruit, la translation peut être décalée de plusieurs px";
            }
            if (control.dispersionMax > 0 && confidence.dispersion && *confidence.dispersion > control.dispersionMax) {
                return "confiance : points locaux en désaccord avec la translation (écart médian " +
                       formatNumber(*confidence.dispersion) + " px > dispersionMax " + formatNumber(control.dispersionMax) +
                       " px) : fond sombre ou bruit dans une partie de la bande";
            }
            return {};
        }

        std::string checkInput(const PhaseCorrelationInput& input) {
            if (!(input.overlap > 0.f && input.overlap < 1.f)) {
                return "overlap must be in ]0, 1[, got " + std::to_string(input.overlap);
            }
            if (input.level < 0 || input.level >= DataNs::LEVEL_COUNT) {
                return "niveau must be in [0, " + std::to_string(DataNs::LEVEL_COUNT - 1) + "], got " + std::to_string(input.level);
            }
            return {};
        }

        // Mesure sur les images du niveau analysé, contrôles de confiance, puis conversion en pleine résolution.
        PairConfidence measurePair(const MeasureContext& context, const cv::Mat& imageA, const cv::Mat& imageB,
                                   DataNs::PairMatches& pair) {
            const PhaseCorrelationInput& input = context.input;
            PairConfidence confidence;
            if (context.matcher.match(imageA, imageB, DataNs::PairContext{pair.direction, input.overlap}, pair.featureMatches)) {
                const ConfidenceContext confidenceContext{pair.direction, input.overlap, input.parameters, input.confiance};
                confidence = PhaseCorrelationStepClass::checkConfidence(pair.featureMatches, imageA, confidenceContext);
                PhaseCorrelationStepClass::scaleToFullResolution(pair.featureMatches, input.level);
                if (confidence.dispersion) *confidence.dispersion *= static_cast<double>(1 << input.level);
                const DataNs::Matches& matches = pair.featureMatches;
                for (std::size_t index = 0; index < matches.pointsInA.size(); ++index) {
                    confidence.points.push_back({matches.pointsInA[index], matches.pointsInB[index], matches.confidenceScores[index]});
                }
            } else {
                confidence.state = ConfidenceState::Rejected;
                confidence.reason = context.matcher.getFailureReason();
            }
            confidence.tileIdA = pair.tileIdA;
            confidence.tileIdB = pair.tileIdB;
            confidence.direction = pair.direction;
            return confidence;
        }

        // Une paire qui échoue (mesure impossible ou contrôle) est notée avec sa raison, sans arrêter le step.
        void recordPair(const MeasureContext& context, DataNs::PairMatches& pair, PairConfidence& confidence) {
            context.confidences.push_back(PhaseCorrelationStepClass::toJson(confidence));
            if (confidence.reason.empty()) {
                context.output.pairs.push_back(std::move(pair));
            } else {
                context.failures.push_back({{"idA", pair.tileIdA}, {"idB", pair.tileIdB}, {"reason", confidence.reason}});
            }
            context.output.confidence.push_back(std::move(confidence));
        }

        // Mesure une tuile avec ses voisines de droite et du dessous. Au plus deux images en mémoire : la tuile (lue une
        // seule fois) et sa voisine. Renvoie l'erreur qui arrête le step, vide sinon.
        std::string measureTile(const DataNs::TileClass& tile, const MeasureContext& context) {
            const std::array<std::pair<DataNs::TileClass*, DataNs::Direction>, 2> neighbours = {
                std::pair{tile.getRight(), DataNs::Direction::Right}, std::pair{tile.getBottom(), DataNs::Direction::Bottom}};
            cv::Mat tileImage;
            for (const auto& [neighbour, direction] : neighbours) {
                if (neighbour == nullptr) continue;
                if (tileImage.empty()) tileImage = PhaseCorrelationStepClass::readPixels(tile, context.input.level);
                if (tileImage.empty()) return "cannot read " + tile.getSource().string();
                const cv::Mat neighbourImage = PhaseCorrelationStepClass::readPixels(*neighbour, context.input.level);
                if (neighbourImage.empty()) return "cannot read " + neighbour->getSource().string();

                DataNs::PairMatches pair{tile.getId(), neighbour->getId(), direction, {}};
                PairConfidence confidence = measurePair(context, tileImage, neighbourImage, pair);
                recordPair(context, pair, confidence);
            }
            return {};
        }

    }

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
        for (int levelIndex = 0; levelIndex < level && !image.empty(); ++levelIndex) {
            cv::resize(image, image, cv::Size(), PYRAMID_REDUCTION, PYRAMID_REDUCTION, cv::INTER_AREA);
        }
        return image;
    }

    void PhaseCorrelationStepClass::scaleToFullResolution(DataNs::Matches& matches, int level) {
        if (level == 0) return;
        const auto scale = static_cast<float>(1 << level);   // 2^level
        // Le pixel p du niveau réduit moyenne les pixels [p·scale, (p + 1)·scale[ : son centre est en (p + 0,5)·scale − 0,5.
        const cv::Point2f center(PIXEL_CENTER, PIXEL_CENTER);
        auto convertPoint = [scale, center](cv::Point2f& point) { point = (point + center) * scale - center; };
        for (auto& point : matches.pointsInA) convertPoint(point);
        for (auto& point : matches.pointsInB) convertPoint(point);
        matches.translationX *= scale;
        matches.translationY *= scale;
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) : fraction et longueur en px, documentées dans le header
    double PhaseCorrelationStepClass::selfSimilarity(const cv::Mat& imageA, DataNs::Direction direction, float overlap,
                                                     double margin) {
        cv::Mat band = extractGreyBand(imageA, direction, overlap);
        if (band.empty()) return NAN;
        const cv::Mat notEmpty = (band != 0);   // noir exact = hors de l'œuvre
        if (cv::countNonZero(notEmpty) < static_cast<int>(band.total() / 4)) return NAN;
        highPassAndClip(band, notEmpty);
        const int side = direction == DataNs::Direction::Bottom ? imageA.rows : imageA.cols;
        return computeBestShiftedCorrelation(band, margin, side);
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
        auto rounded = [](double number) { return std::round(number * JSON_ROUNDING) / JSON_ROUNDING; };
        auto value = [&rounded](const std::optional<double>& number) {
            return number && std::isfinite(*number) ? Json(rounded(*number)) : Json();
        };
        Json points = Json::array();
        for (const PointConfidence& point : confidence.points) {
            points.push_back({{"a", {rounded(point.pointInA.x), rounded(point.pointInA.y)}},
                              {"b", {rounded(point.pointInB.x), rounded(point.pointInB.y)}},
                              {"ncc", rounded(point.ncc)}});
        }
        return {{"idA", confidence.tileIdA}, {"idB", confidence.tileIdB},
                {"dir", confidence.direction == DataNs::Direction::Bottom ? "Bottom" : "Right"},
                {"etat", stateName(confidence.state)}, {"ncc", value(confidence.ncc)},
                {"dispersion", value(confidence.dispersion)}, {"autosim", value(confidence.autosim)}, {"points", points}};
    }

    PairConfidence PhaseCorrelationStepClass::checkConfidence(const DataNs::Matches& matches, const cv::Mat& imageA,
                                                              const ConfidenceContext& context) {
        PairConfidence confidence = measureIndicators(matches, imageA, context);
        const bool hasPoints = !matches.pointsInA.empty();
        const ConfidenceControl& control = context.control;

        std::string reason = hasPoints ? findPointsReason(confidence, control) : findMissingPointsReason(matches, imageA, context);
        if (!reason.empty()) {
            confidence.state = ConfidenceState::Rejected;
            confidence.reason = std::move(reason);
            return confidence;
        }
        // Motif répété de période inférieure à la marge : plusieurs positions de B sont possibles
        if (control.autosimMax < 1 && confidence.autosim && *confidence.autosim >= control.autosimMax) {
            confidence.state = ConfidenceState::Ambiguous;
            confidence.reason = "confiance : motif répété dans la bande de recouvrement (auto-similarité " +
                                formatNumber(*confidence.autosim) + " >= autosimMax " + formatNumber(control.autosimMax) +
                                ") : mesure ambiguë, elle peut être fausse d'une période";
            return confidence;
        }
        confidence.state = hasPoints ? ConfidenceState::Reliable : ConfidenceState::Unverified;
        return confidence;
    }

    // Paramètres relus dans la configuration : un JSON chargé après la construction remplace les valeurs par défaut. Un
    // paramètre absent (JSON écrit avant son ajout) garde la valeur par défaut de PhaseCorrelationInput.
    void PhaseCorrelationStepClass::readInput(PhaseCorrelationInput& input) const {
        auto readParameter = [this](const char* parameterName, auto& value) {
            try {
                value = getParameter(parameterName).get<std::decay_t<decltype(value)>>();
            } catch (const std::out_of_range&) {   // NOLINT(bugprone-empty-catch) : paramètre absent, valeur par défaut gardée
            }
        };
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
    }

    // Lit et contrôle les paramètres. false : cause dans result["error"].
    bool PhaseCorrelationStepClass::loadInput(PhaseCorrelationInput& input) {
        try {
            readInput(input);
        } catch (const Json::exception& exception) {
            result["error"] = std::string("invalid option type: ") + exception.what();
            return false;
        }
        const std::string inputError = checkInput(input);
        if (!inputError.empty()) {
            result["error"] = inputError;
            return false;
        }
        if (!dataStore || !dataStore->has("tiles")) {
            result["error"] = "no tiles: run the import step first";
            return false;
        }
        return true;
    }

    // Étapes : 1. lire et contrôler les paramètres ; 2. créer l'algorithme ; 3. pour chaque tuile et chacune de ses
    // voisines (droite, dessous), lire les deux images, mesurer, ramener en pleine résolution ; 4. publier les résultats.
    bool PhaseCorrelationStepClass::execute() {
        output = {};
        result = Json::object();

        PhaseCorrelationInput input;
        if (!loadInput(input)) return false;

        // L'algorithme valide ses réglages à la construction (std::invalid_argument si l'un est incohérent).
        std::optional<PhaseCorrelationNs::PhaseCorrelationMatcherClass> matcher;
        try {
            matcher.emplace(input.parameters);
        } catch (const std::invalid_argument& exception) {
            result["error"] = std::string("invalid parameters: ") + exception.what();
            return false;
        }

        return measureAllTiles(input, *matcher);
    }

    bool PhaseCorrelationStepClass::measureAllTiles(const PhaseCorrelationInput& input,
                                                    PhaseCorrelationNs::PhaseCorrelationMatcherClass& matcher) {
        Json failures = Json::array();
        Json confidences = Json::array();
        const MeasureContext context{input, matcher, output, failures, confidences};
        for (const auto& tile : dataStore->get<DataNs::TileList>("tiles")) {
            const std::string error = measureTile(*tile, context);
            if (!error.empty()) {
                result["error"] = error;
                return false;
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
