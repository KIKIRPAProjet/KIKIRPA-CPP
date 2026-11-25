// src/frontend/pipeline_results.cpp
#include "pipeline_results.h"

#include "core/io/unicode_path.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QFileInfo>
#include <QCoreApplication>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace FrontendNs {

    namespace {

        using PairKey = std::pair<int, int>;
        using FrontendUtils::PairAlignmentInfo;
        using FrontendUtils::PairStatus;

        // Tolérance de simplification des coutures, en pixels de l'aperçu : efface les marches d'escalier du tracé
        // au pixel sans déplacer la ligne de façon visible.
        constexpr double SEAM_SIMPLIFICATION_PX = 0.75;

        // Verdict de l'appariement pour une paire, lu dans le résultat d'un step.
        struct PairVerdict {
            std::string state;   // "fiable", "ambigu", "non_verifie" ou "rejete" (phase-correlation)
            double ncc = -1.0;   // -1 : non mesuré
        };

        struct StepFindings {
            std::map<PairKey, PairVerdict> verdicts;
            std::map<PairKey, std::string> failureReasons;
            std::set<PairKey> correctedPairs;   // corrigées à la main (manual-matches)
        };

        PairKey pairKey(const nlohmann::json &entry) { return {entry.at("idA").get<int>(), entry.at("idB").get<int>()}; }

        bool hasPairIds(const nlohmann::json &entry) {
            return entry.is_object() && entry.contains("idA") && entry.contains("idB") &&
                   entry.at("idA").is_number_integer() && entry.at("idB").is_number_integer();
        }

        void readVerdicts(const nlohmann::json &result, StepFindings &findings) {
            if (!result.contains("confidence") || !result.at("confidence").is_array()) return;
            for (const nlohmann::json &entry : result.at("confidence")) {
                if (!hasPairIds(entry) || !entry.value("etat", nlohmann::json()).is_string()) continue;
                PairVerdict verdict{.state = entry.at("etat").get<std::string>()};
                if (entry.value("ncc", nlohmann::json()).is_number()) verdict.ncc = entry.at("ncc").get<double>();
                findings.verdicts[pairKey(entry)] = verdict;
            }
        }

        // Première raison d'échec rencontrée pour chaque paire, dans l'ordre des steps.
        void readFailures(const nlohmann::json &result, StepFindings &findings) {
            if (!result.contains("failures") || !result.at("failures").is_array()) return;
            for (const nlohmann::json &entry : result.at("failures")) {
                if (!hasPairIds(entry)) continue;
                const std::string reason = entry.value("reason", std::string());
                findings.failureReasons.emplace(pairKey(entry), reason);
            }
        }

        void readCorrectedPairs(const nlohmann::json &result, StepFindings &findings) {
            if (!result.contains("correctedPairs") || !result.at("correctedPairs").is_array()) return;
            for (const nlohmann::json &entry : result.at("correctedPairs"))
                if (hasPairIds(entry)) findings.correctedPairs.insert(pairKey(entry));
        }

        // Rempli en place plutôt que renvoyé : déplacer des std::map peut lever selon la bibliothèque standard (MSVC),
        // ce que bugprone-exception-escape signale.
        void readStepFindings(const std::vector<nlohmann::json> &stepResults, StepFindings &findings) {
            for (const nlohmann::json &result : stepResults) {
                if (!result.is_object()) continue;
                readVerdicts(result, findings);
                readFailures(result, findings);
                readCorrectedPairs(result, findings);
            }
        }

        const DataNs::TileClass *findTile(const DataNs::TileList &tiles, int tileId) {
            for (const auto &tile : tiles)
                if (tile && tile->getId() == tileId) return tile.get();
            return nullptr;
        }

        QString tileName(const DataNs::TileClass *tile, int tileId) {
            if (!tile) return QObject::tr("tuile %1").arg(tileId);
            return QFileInfo(QString::fromStdString(IONs::pathToUtf8(tile->getSource()))).completeBaseName();
        }

        // Images et tailles pleine résolution des deux tuiles, pour dessiner la paire et ses points.
        void attachTiles(PairAlignmentInfo &info, const DataNs::TileClass *tileA, const DataNs::TileClass *tileB) {
            if (tileA) {
                info.imagePathA = QString::fromStdString(IONs::pathToUtf8(tileA->getSource()));
                info.tileSizeA = QSize(tileA->getLevel(0).width, tileA->getLevel(0).height);
            }
            if (tileB) {
                info.imagePathB = QString::fromStdString(IONs::pathToUtf8(tileB->getSource()));
                info.tileSizeB = QSize(tileB->getLevel(0).width, tileB->getLevel(0).height);
            }
        }

        void applyVerdict(PairAlignmentInfo &info, const PairVerdict &verdict) {
            if (verdict.state == "fiable") {
                info.status = PairStatus::Reliable;
                info.statusReason = QObject::tr("corrélation fiable");
            } else if (verdict.state == "rejete") {
                info.status = PairStatus::Failed;
                info.statusReason = QObject::tr("appariement rejeté");
            } else {
                info.status = PairStatus::Dubious;
                info.statusReason = verdict.state == "ambigu" ? QObject::tr("pic de corrélation ambigu")
                                                              : QObject::tr("contrôle de confiance impossible");
            }
        }

        // Sans verdict (SIFT, LightGlue, ou step repris depuis un checkpoint) : une paire présente dans `matches` a servi
        // au positionnement. Peu de points la rend douteuse, pas en échec.
        void softenWithoutVerdict(PairAlignmentInfo &info) {
            if (info.status != PairStatus::Failed || info.totalMatches == 0) return;
            info.status = PairStatus::Dubious;
            info.statusReason = QObject::tr("peu de points (%1) : à vérifier").arg(info.totalMatches);
        }

        // NCC ou écart absents (SIFT, LightGlue, reprise) : laissés vides, affichés « non disponible ».
        PairAlignmentInfo matchedPair(int index, const DataNs::PairMatches &pairMatches, const DataNs::TileList &tiles,
                                      const StepFindings &findings, std::optional<double> residualNorm) {
            const DataNs::TileClass *tileA = findTile(tiles, pairMatches.tileIdA);
            const DataNs::TileClass *tileB = findTile(tiles, pairMatches.tileIdB);
            const auto verdict = findings.verdicts.find({pairMatches.tileIdA, pairMatches.tileIdB});
            std::optional<double> ncc;
            if (verdict != findings.verdicts.end() && verdict->second.ncc >= 0.0) ncc = verdict->second.ncc;
            PairAlignmentInfo info = FrontendUtils::pairInfoFromBackend(
                index, tileName(tileA, pairMatches.tileIdA), tileName(tileB, pairMatches.tileIdB), pairMatches, nullptr,
                ncc, residualNorm);
            info.tileIndexA = pairMatches.tileIdA;
            info.tileIndexB = pairMatches.tileIdB;
            attachTiles(info, tileA, tileB);
            if (verdict != findings.verdicts.end()) applyVerdict(info, verdict->second);
            else softenWithoutVerdict(info);
            // Le verdict de l'appariement porte sur les points remplacés : la correction de l'opérateur prime.
            if (findings.correctedPairs.find({pairMatches.tileIdA, pairMatches.tileIdB}) !=
                findings.correctedPairs.end()) {
                info.status = PairStatus::Validated;
                info.statusReason = QObject::tr("corrigée à la main");
                info.intervention = QObject::tr("manuelle (%1 point(s))").arg(info.totalMatches);
            }
            return info;
        }

        QString directionBetween(const DataNs::TileClass *tileA, const DataNs::TileClass *tileB) {
            if (tileA && tileB && tileA->getBottom() == tileB) return QObject::tr("vertical");
            return QObject::tr("horizontal");
        }

        PairAlignmentInfo failedPair(int index, const PairKey &key, const std::string &reason, const DataNs::TileList &tiles) {
            const DataNs::TileClass *tileA = findTile(tiles, key.first);
            const DataNs::TileClass *tileB = findTile(tiles, key.second);
            PairAlignmentInfo info;
            info.pairIndex = index;
            info.tileIndexA = key.first;
            info.tileIndexB = key.second;
            info.tileA = tileName(tileA, key.first);
            info.tileB = tileName(tileB, key.second);
            info.direction = directionBetween(tileA, tileB);
            info.status = PairStatus::Failed;
            info.statusReason = reason.empty() ? QObject::tr("appariement impossible") : QString::fromStdString(reason);
            info.intervention = QObject::tr("aucune");
            attachTiles(info, tileA, tileB);
            return info;
        }

        std::map<PairKey, double> residualNorms(const std::vector<DataNs::PairResidual> &residuals) {
            std::map<PairKey, double> norms;
            for (const DataNs::PairResidual &residual : residuals) norms[{residual.tileIdA, residual.tileIdB}] = residual.residualNorm;
            return norms;
        }

        // Pixels dont un voisin (droite ou dessous) appartient au territoire d'une autre tuile.
        cv::Mat seamMask(const cv::Mat &labels) {
            cv::Mat mask = cv::Mat::zeros(labels.size(), CV_8U);
            for (int row = 0; row < labels.rows; ++row) {
                for (int column = 0; column < labels.cols; ++column) {
                    const int label = labels.at<int>(row, column);
                    const bool rightSeam = column + 1 < labels.cols && label >= 0 && labels.at<int>(row, column + 1) >= 0 &&
                                           labels.at<int>(row, column + 1) != label;
                    const bool bottomSeam = row + 1 < labels.rows && label >= 0 && labels.at<int>(row + 1, column) >= 0 &&
                                            labels.at<int>(row + 1, column) != label;
                    if (rightSeam || bottomSeam) mask.at<std::uint8_t>(row, column) = 1;
                }
            }
            return mask;
        }

        // Contour extérieur et trous ensemble : fillPoly remplit en pair-impair, les trous restent vides.
        std::vector<std::vector<cv::Point>> scaledContours(const DataNs::TileTerritory &territory, std::size_t polygonIndex,
                                                           double scale) {
            std::vector<std::vector<cv::Point>> contours{territory.polygons[polygonIndex]};
            if (polygonIndex < territory.holes.size())
                contours.insert(contours.end(), territory.holes[polygonIndex].begin(), territory.holes[polygonIndex].end());
            for (auto &contour : contours)
                for (cv::Point &point : contour) point = cv::Point(cvRound(point.x * scale), cvRound(point.y * scale));
            return contours;
        }

    }

    QVector<ResultTile> buildResultTiles(const DataNs::TileList &tiles) {
        QVector<ResultTile> results;
        for (const auto &tile : tiles) {
            if (!tile) continue;
            results.append({.id = tile->getId(), .name = tileName(tile.get(), tile->getId()),
                            .imagePath = QString::fromStdString(IONs::pathToUtf8(tile->getSource())),
                            .size = QSize(tile->getLevel(0).width, tile->getLevel(0).height), .row = tile->getGridRow(),
                            .column = tile->getGridColumn()});
        }
        return results;
    }

    QVector<FrontendUtils::PairAlignmentInfo> buildPairInfos(const DataNs::TileList &tiles,
                                                             const std::vector<DataNs::PairMatches> &matches,
                                                             const std::vector<nlohmann::json> &stepResults,
                                                             const std::vector<DataNs::PairResidual> &residuals) {
        StepFindings findings;
        readStepFindings(stepResults, findings);
        const std::map<PairKey, double> norms = residualNorms(residuals);
        QVector<PairAlignmentInfo> pairs;
        for (const DataNs::PairMatches &pairMatches : matches) {
            const auto norm = norms.find({pairMatches.tileIdA, pairMatches.tileIdB});
            pairs.append(matchedPair(static_cast<int>(pairs.size()), pairMatches, tiles, findings,
                                     norm != norms.end() ? std::optional<double>(norm->second) : std::nullopt));
        }
        for (const auto &[key, reason] : findings.failureReasons) {
            const PairKey currentKey = key;
            const bool alreadyListed = std::any_of(pairs.cbegin(), pairs.cend(), [currentKey](const PairAlignmentInfo &pair) {
                return pair.tileIndexA == currentKey.first && pair.tileIndexB == currentKey.second;
            });
            if (!alreadyListed) pairs.append(failedPair(static_cast<int>(pairs.size()), key, reason, tiles));
        }
        return pairs;
    }

    void attachOverlapEstimates(QVector<PairAlignmentInfo> &pairs, const DataNs::OverlapEstimates &estimates) {
        const auto toQRect = [](const cv::Rect &area) { return QRect(area.x, area.y, area.width, area.height); };
        for (PairAlignmentInfo &pair : pairs) {
            for (const DataNs::PairOverlapEstimate &estimate : estimates) {
                if (estimate.tileIdA != pair.tileIndexA || estimate.tileIdB != pair.tileIndexB) continue;
                pair.overlapInA = toQRect(estimate.commonAreaInA);
                pair.overlapInB = toQRect(estimate.commonAreaInB);
                if (pair.totalMatches == 0) {
                    pair.dx = estimate.estimate.translationBInA.x;
                    pair.dy = estimate.estimate.translationBInA.y;
                }
            }
        }
    }

    namespace {

        // Raisons connues, reconnues par un mot-clé de leur texte (anglais du backend ou français de l'interface). La
        // première qui correspond l'emporte : les plus précises d'abord.
        struct ReasonText {
            const char *keyword;
            const char *sentence;
        };
        constexpr std::array<ReasonText, 15> REASON_TEXTS = {{
            {.keyword = "different sizes", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Les deux tuiles n'ont pas la même taille : elles ne peuvent pas être comparées.")},
            {.keyword = "cannot read", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Une des deux images n'a pas pu être lue.")},
            {.keyword = "not enough common content", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "La zone commune contient trop peu de détails (fond noir, reflets, zone uniforme) ou elle est trop petite.")},
            {.keyword = "outside the rail margin", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Le décalage trouvé est trop loin du recouvrement prévu par la grille.")},
            {.keyword = "touches the border", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Le décalage trouvé est trop loin du recouvrement prévu par la grille.")},
            {.keyword = "ambiguous correlation peak", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Aucun décalage net : la zone commune est peu texturée ou répétitive.")},
            {.keyword = "motif répété", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Le décalage est incertain : la zone commune contient un motif répété.")},
            {.keyword = "pic de corrélation ambigu", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Le décalage est incertain : plusieurs positions conviennent presque aussi bien.")},
            {.keyword = "aucun point local fiable", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Aucun point de contrôle fiable dans la zone commune (zone sans texture ou bruitée).")},
            {.keyword = "patchs locaux peu ressemblants", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Les deux tuiles se ressemblent peu une fois superposées (fond sombre ou bruit).")},
            {.keyword = "contrôle NCC faible", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Les deux tuiles se ressemblent peu une fois superposées.")},
            {.keyword = "points locaux en désaccord", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Les points de contrôle ne sont pas d'accord avec le décalage mesuré.")},
            {.keyword = "score de confiance faible", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Les points trouvés sont peu sûrs.")},
            {.keyword = "peu de points", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Peu de points communs ont été trouvés.")},
            {.keyword = "nombre de points insuffisant", QT_TRANSLATE_NOOP("PairOperatorText", .sentence = "Trop peu de points communs ont été trouvés.")},
        }};

        QString translatedReason(const char *sentence) { return QCoreApplication::translate("PairOperatorText", sentence); }

        // Phrase de la raison connue, sinon celle par défaut de l'état.
        QString reasonSentence(const PairAlignmentInfo &pair) {
            for (const ReasonText &reason : REASON_TEXTS)
                if (pair.statusReason.contains(QString::fromUtf8(reason.keyword), Qt::CaseInsensitive))
                    return translatedReason(reason.sentence);
            if (pair.status == PairStatus::Failed) return QObject::tr("Ces deux tuiles n'ont pas pu être reliées.");
            if (pair.status == PairStatus::Dubious) return QObject::tr("La mesure de cette paire est incertaine.");
            return {};
        }

        QString technicalDetails(const PairAlignmentInfo &pair) {
            QStringList lines;
            if (!pair.statusReason.isEmpty()) lines << QObject::tr("Raison donnée par le calcul : %1").arg(pair.statusReason);
            if (pair.totalMatches > 0)
                lines << QObject::tr("Confiance moyenne des points (0 à 1) : %1")
                             .arg(QString::number(pair.meanConfidence, 'f', 2).replace('.', ','));
            lines << QObject::tr("NCC, ressemblance de la zone commune (-1 à 1) : %1").arg(FrontendUtils::formatMeasure(pair.nccScore, 2));
            return lines.join(QLatin1Char('\n'));
        }

    }

    PairOperatorText describePairForOperator(const PairAlignmentInfo &pair) {
        PairOperatorText text{QString(), QString(), technicalDetails(pair)};
        switch (pair.status) {
        case PairStatus::Failed:
            text.state = QObject::tr("Échec");
            text.explanation = reasonSentence(pair) + QObject::tr(" Saisissez le décalage avec « Corriger… ».");
            break;
        case PairStatus::Dubious:
            text.state = QObject::tr("À vérifier");
            text.explanation = reasonSentence(pair) + QObject::tr(" Vérifiez la superposition, puis validez ou corrigez.");
            break;
        case PairStatus::Validated:
            text.state = QObject::tr("Validée");
            text.explanation = pair.statusReason.contains(QStringLiteral("main"))
                                   ? QObject::tr("Corrigée à la main : le calcul utilise le décalage saisi.")
                                   : QObject::tr("Validée par l'opérateur.");
            break;
        case PairStatus::Reliable:
            text.state = QObject::tr("Fiable");
            text.explanation = QObject::tr("Les deux tuiles se superposent correctement.");
            break;
        }
        return text;
    }

    ManualCorrection manualCorrectionFor(const PairAlignmentInfo &pair, const double dx, const double dy) {
        const QPointF offset(dx, dy);
        const QRectF common = QRectF(QPointF(0, 0), QSizeF(pair.tileSizeA)) & QRectF(offset, QSizeF(pair.tileSizeB));
        const QPointF pointInA = common.isEmpty() ? offset : common.center();
        const QPointF pointInB = pointInA - offset;
        return {pair.tileIndexA, pair.tileIndexB, {{pointInA.x(), pointInA.y()}}, {{pointInB.x(), pointInB.y()}}};
    }

    std::vector<ManualCorrection> withCorrection(std::vector<ManualCorrection> corrections, const ManualCorrection &correction) {
        const auto samePair = [&correction](const ManualCorrection &existing) {
            return (existing.tileIdA == correction.tileIdA && existing.tileIdB == correction.tileIdB) ||
                   (existing.tileIdA == correction.tileIdB && existing.tileIdB == correction.tileIdA);
        };
        corrections.erase(std::remove_if(corrections.begin(), corrections.end(), samePair), corrections.end());
        corrections.push_back(correction);
        return corrections;
    }

    // Le masque des coutures fait un pixel de large : son contour en fait l'aller-retour, qui se trace comme une ligne.
    SeamLines buildSeamLines(const std::vector<DataNs::TileTerritory> &territories, const QSize &previewSize,
                             double previewScale) {
        if (territories.empty() || previewSize.isEmpty() || !(previewScale > 0.0)) return {};
        cv::Mat labels(previewSize.height(), previewSize.width(), cv::traits::Type<int>::value, cv::Scalar(-1));
        for (const DataNs::TileTerritory &territory : territories)
            for (std::size_t polygonIndex = 0; polygonIndex < territory.polygons.size(); ++polygonIndex)
                cv::fillPoly(labels, scaledContours(territory, polygonIndex, previewScale), cv::Scalar(territory.tileIndex));
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(seamMask(labels), contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
        SeamLines seams{previewSize, {}};
        for (const std::vector<cv::Point> &contour : contours) {
            std::vector<cv::Point> simplified;
            cv::approxPolyDP(contour, simplified, SEAM_SIMPLIFICATION_PX, false);
            QPolygonF line;
            for (const cv::Point &point : simplified) line << QPointF(point.x + 0.5, point.y + 0.5);   // centre du pixel
            seams.lines.append(line);
        }
        return seams;
    }

}
