// include/backend/algorithms/stitching/lightglue/lightglue_matcher.h
#pragma once

#include "algorithms/stitching/feature_matcher.h"
#include "core/data/algorithm_types.h"

#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>

#include <memory>

namespace LightGlueNs {

    using Json = nlohmann::json;

    // Appariement par un modèle LightGlue exporté en ONNX (SuperPoint/DISK à deux entrées, ou RACO/ALIKED en batch),
    // sur des bandes de recouvrement déjà découpées. Compilé seulement si ONNX Runtime et le module dnn d'OpenCV sont
    // disponibles (voir cmake/backend.cmake).
    //
    // Réglages (JSON du constructeur) : model_path, chemin du modèle ONNX (absolu, ou relatif au répertoire de travail,
    //   à son parent, à la racine du projet ou à build/). std::invalid_argument s'il manque ou si le fichier n'existe pas.
    // Sortie : correspondances de score ≥ 0.9, translation = médiane de pointsInA − pointsInB ; false si aucune.
    class LightGlueMatcherClass : public StitchingNs::FeatureMatcherClass {

        public:
            explicit LightGlueMatcherClass(const Json& initialParameters = {});

            bool match(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                       DataNs::Matches& matches) override;

        private:
            Json parameters;
            // Permet de ne pas recharger le modèle à chaque paire d'images
            Ort::Env onnxEnvironment{ORT_LOGGING_LEVEL_WARNING, "LightGlue"};
            std::unique_ptr<Ort::Session> session;

    };

}
