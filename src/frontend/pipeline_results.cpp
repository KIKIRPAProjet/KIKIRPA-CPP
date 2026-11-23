// src/frontend/pipeline_results.cpp
#include "pipeline_results.h"

#include "core/io/unicode_path.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QColor>
#include <QFileInfo>
#include <QObject>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace FrontendNs {

    namespace {

        using PairKey = std::pair<int, int>;
        using FrontendUtils::PairAlignmentInfo;
        using FrontendUtils::PairStatus;

        constexpr int SEAM_ALPHA = 235;
        const QColor SEAM_COLOR(255, 59, 48, SEAM_ALPHA);

        // Verdict de l'appariement pour une paire, lu dans le résultat d'un step.
        struct PairVerdict {
            std::string state;   // "fiable", "ambigu", "non_verifie" ou "rejete" (phase-correlation)
            double ncc = -1.0;   // -1 : non mesuré
        };

        struct StepFindings {
            std::map<PairKey, PairVerdict> verdicts;
            std::map<PairKey, std::string> failureReasons;
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
                PairVerdict verdict{entry.at("etat").get<std::string>()};
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

        StepFindings readStepFindings(const std::vector<nlohmann::json> &stepResults) {
            StepFindings findings;
            for (const nlohmann::json &result : stepResults) {
                if (!result.is_object()) continue;
                readVerdicts(result, findings);
                readFailures(result, findings);
            }
            return findings;
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

        PairAlignmentInfo matchedPair(int index, const DataNs::PairMatches &pairMatches, const DataNs::TileList &tiles,
                                      const StepFindings &findings, double residualNorm) {
            const DataNs::TileClass *tileA = findTile(tiles, pairMatches.tileIdA);
            const DataNs::TileClass *tileB = findTile(tiles, pairMatches.tileIdB);
            const auto verdict = findings.verdicts.find({pairMatches.tileIdA, pairMatches.tileIdB});
            const double ncc = verdict != findings.verdicts.end() && verdict->second.ncc >= 0.0 ? verdict->second.ncc : 1.0;
            PairAlignmentInfo info = FrontendUtils::pairInfoFromBackend(
                index, tileName(tileA, pairMatches.tileIdA), tileName(tileB, pairMatches.tileIdB), pairMatches, nullptr,
                ncc, residualNorm);
            info.tileIndexA = pairMatches.tileIdA;
            info.tileIndexB = pairMatches.tileIdB;
            attachTiles(info, tileA, tileB);
            if (verdict != findings.verdicts.end()) applyVerdict(info, verdict->second);
            else softenWithoutVerdict(info);
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
            cv::Mat mask = cv::Mat::zeros(labels.size(), CV_8UC1);
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
            results.append({tile->getId(), tileName(tile.get(), tile->getId()),
                            QString::fromStdString(IONs::pathToUtf8(tile->getSource())),
                            QSize(tile->getLevel(0).width, tile->getLevel(0).height), tile->getGridRow(),
                            tile->getGridColumn()});
        }
        return results;
    }

    QVector<FrontendUtils::PairAlignmentInfo> buildPairInfos(const DataNs::TileList &tiles,
                                                             const std::vector<DataNs::PairMatches> &matches,
                                                             const std::vector<nlohmann::json> &stepResults,
                                                             const std::vector<DataNs::PairResidual> &residuals) {
        const StepFindings findings = readStepFindings(stepResults);
        const std::map<PairKey, double> norms = residualNorms(residuals);
        QVector<PairAlignmentInfo> pairs;
        for (const DataNs::PairMatches &pairMatches : matches) {
            const auto norm = norms.find({pairMatches.tileIdA, pairMatches.tileIdB});
            pairs.append(matchedPair(static_cast<int>(pairs.size()), pairMatches, tiles, findings,
                                     norm != norms.end() ? norm->second : 0.0));
        }
        for (const auto &[key, reason] : findings.failureReasons) {
            const bool alreadyListed = std::any_of(pairs.begin(), pairs.end(), [&key = key](const PairAlignmentInfo &pair) {
                return pair.tileIndexA == key.first && pair.tileIndexB == key.second;
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
            }
        }
    }

    QImage buildSeamOverlay(const std::vector<DataNs::TileTerritory> &territories, const QSize &previewSize,
                            double previewScale) {
        if (territories.empty() || previewSize.isEmpty() || !(previewScale > 0.0)) return {};
        cv::Mat labels(previewSize.height(), previewSize.width(), CV_32SC1, cv::Scalar(-1));
        for (const DataNs::TileTerritory &territory : territories)
            for (std::size_t polygonIndex = 0; polygonIndex < territory.polygons.size(); ++polygonIndex)
                cv::fillPoly(labels, scaledContours(territory, polygonIndex, previewScale), cv::Scalar(territory.tileIndex));
        cv::Mat mask = seamMask(labels);
        cv::dilate(mask, mask, cv::Mat());   // 3 × 3 : la couture reste visible une fois l'aperçu réduit à l'écran
        if (cv::countNonZero(mask) == 0) return {};
        QImage overlay(previewSize, QImage::Format_ARGB32);
        overlay.fill(Qt::transparent);
        for (int row = 0; row < mask.rows; ++row)
            for (int column = 0; column < mask.cols; ++column)
                if (mask.at<std::uint8_t>(row, column) != 0) overlay.setPixelColor(column, row, SEAM_COLOR);
        return overlay;
    }

}
