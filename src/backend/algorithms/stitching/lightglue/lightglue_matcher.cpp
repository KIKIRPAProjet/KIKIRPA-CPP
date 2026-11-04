// src/backend/algorithms/stitching/lightglue/lightglue_matcher.cpp
#include "algorithms/stitching/lightglue/lightglue_matcher.h"

#include <onnxruntime_cxx_api.h>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>     // Pour chronométrer certaines étapes (surtout l'inférence du modèle)
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>     // Pour la gestion dynamique du nombre de cœurs disponibles
#include <vector>

#ifdef __APPLE__ // Permet de ne pas planter si le code tourne sur une machine non Apple
#include <coreml_provider_factory.h>
#endif

#ifndef STITCHING_PROJECT_ROOT
#define STITCHING_PROJECT_ROOT "."
#endif

namespace LightGlueNs {

    namespace {

        // Seules les correspondances dont le score atteint ce seuil sont conservées.
        constexpr float MINIMUM_CONFIDENCE_SCORE = 0.9f;

        // Prépare une image au format attendu par le modèle ONNX.
        cv::Mat preprocessImage(const cv::Mat& image, const std::string& imageName, float& scaleX, float& scaleY,
                                bool isGreyscale) {
            std::cout << "[DEBUG] preprocessing de l'image " << imageName << " : début\n";

            // Vérifier que l'image contient bien des données.
            if (image.empty()) {
                throw std::runtime_error("LightGlue: " + imageName + " is empty.");
            }

            // Redimensionnement : RACO/ALIKED+LightGlue nécessite des dimensions multiples de 32.
            // Cette contrainte reste également valide pour les modèles SuperPoint/DISK.
            const int maximumEdge = 3200;
            scaleX = 1.0f;
            scaleY = 1.0f;
            const int maximumDimension = std::max(image.cols, image.rows);
            cv::Mat resizedImage = image;

            if (maximumDimension > maximumEdge) {
                const float scale = static_cast<float>(maximumEdge) / maximumDimension;
                const int resizedWidth =
                    std::max(32, static_cast<int>(std::round(image.cols * scale / 32.0f)) * 32);
                const int resizedHeight =
                    std::max(32, static_cast<int>(std::round(image.rows * scale / 32.0f)) * 32);
                scaleX = static_cast<float>(resizedWidth) / image.cols;
                scaleY = static_cast<float>(resizedHeight) / image.rows;
                cv::resize(image, resizedImage,
                           cv::Size(resizedWidth, resizedHeight), 0.0, 0.0,
                           cv::INTER_AREA);
            }

            std::cout << "[DEBUG] " << imageName
                      << " | Taille originale : " << image.cols << "x" << image.rows
                      << " | ScaleX : " << scaleX << ", ScaleY : " << scaleY
                      << " | Nouvelle taille : " << resizedImage.cols << "x" << resizedImage.rows << std::endl;

            // Gestion des canaux de couleur (le modèle attend une image avec 1 canal Grayscale ou 3 canaux RGB).
            cv::Mat finalImage;
            if (isGreyscale) {
                // Le modèle exige du niveaux de gris (ex: SuperPoint)
                if (resizedImage.channels() == 3) {
                    cv::cvtColor(resizedImage, finalImage, cv::COLOR_BGR2GRAY);
                } else if (resizedImage.channels() == 4) {
                    cv::cvtColor(resizedImage, finalImage, cv::COLOR_BGRA2GRAY);
                } else {
                    finalImage = resizedImage;   // Déjà 1 canal
                }
            } else {
                // Le modèle exige de la couleur RGB (ex: DISK, ALIKED)
                if (resizedImage.channels() == 1) {
                    cv::cvtColor(resizedImage, finalImage, cv::COLOR_GRAY2BGR);
                } else if (resizedImage.channels() == 4) {
                    cv::cvtColor(resizedImage, finalImage, cv::COLOR_BGRA2BGR);
                } else if (resizedImage.channels() == 3) {
                    finalImage = resizedImage;
                }
            }

            std::cout << "[DEBUG] preprocessing de l'image " << imageName << " : fin\n";

            // Convertir l'image en float, normaliser les valeurs et passer au format NCHW.
            finalImage.convertTo(finalImage, CV_32F, (finalImage.depth() == CV_16U) ? (1.0 / 65535.0) : (1.0 / 255.0));
            return cv::dnn::blobFromImage(finalImage, 1.0, cv::Size(), cv::Scalar(), !isGreyscale, false);
        }

        // Médiane ; pour un nombre pair de valeurs, moyenne des deux valeurs du milieu. La médiane limite l'effet des
        // mauvaises correspondances.
        float computeMedian(std::vector<float>& values) {
            const size_t middleIndex = values.size() / 2;
            std::nth_element(values.begin(), values.begin() + middleIndex, values.end());
            if (values.size() % 2 == 0) {
                const float upper = values[middleIndex];
                std::nth_element(values.begin(), values.begin() + middleIndex - 1, values.end());
                return (values[middleIndex - 1] + upper) / 2.0f;
            }
            return values[middleIndex];
        }

    }

    // Initialisation du matcher LightGlue
    // Cherche le fichier du modèle ONNX sélectionné et initialise la session ONNX
    LightGlueMatcherClass::LightGlueMatcherClass(const Json& initialParameters) : parameters(initialParameters) {
        std::cout << "[DEBUG] Initialisation du LightGlueMatcherClass : début\n";

        // Lire le chemin du modèle passé par la configuration du pipeline
        const auto configuredPath = this->parameters.value("model_path", std::string{});
        if (configuredPath.empty()) {
            throw std::invalid_argument("LightGlue model_path is missing");
        }

        // Essayer le chemin configuré en premier pour que les chemins absolus et les chemins relatifs au répertoire de
        // travail de l'appelant continuent de fonctionner
        std::filesystem::path modelPath = configuredPath;
        if (!modelPath.is_absolute() &&
            !std::filesystem::is_regular_file(modelPath)) {
            const auto workingDirectory = std::filesystem::current_path();
            const std::array<std::filesystem::path, 3> candidatePaths = {
                workingDirectory.parent_path() / modelPath,
                std::filesystem::path(STITCHING_PROJECT_ROOT) / modelPath,
                workingDirectory / "build" / modelPath,
            };
            for (const auto& candidatePath : candidatePaths) {
                if (std::filesystem::is_regular_file(candidatePath)) {
                    modelPath = candidatePath;
                    break;
                }
            }
        }

        // Erreur explicite si le modèle n'est pas trouvé, pour éviter des erreurs plus tard
        if (!std::filesystem::is_regular_file(modelPath)) {
            throw std::invalid_argument(
                "LightGlue model not found: " + configuredPath +
                " (working directory: " +
                std::filesystem::current_path().string() + ")");
        }

        // Conserver le chemin résolu pour la future session ONNX Runtime
        this->parameters["model_path"] = modelPath.string();
        std::cout << "LightGlue model: " << modelPath.string() << '\n';

        // Initialisation du modèle ONNX Runtime
        Ort::SessionOptions sessionOptions;

        // sessionOptions.SetIntraOpNumThreads(0) (laisser ONNX Runtime décider du nombre de threads) ne fonctionne pas
        // correctement (du moins sur Mac) : il utilise moins de 2 cœurs alors que d'autres sont disponibles.

        // Détection dynamique du matériel disponible.
        const unsigned int hardwareCoreCount = std::thread::hardware_concurrency();

        // Si la détection échoue (renvoie 0), on sécurise à 1.
        // Bonne pratique : on garde 1 cœur libre pour le système (OS) afin d'éviter que l'ordinateur ne se fige.
        unsigned int threadCount = (hardwareCoreCount > 1) ? (hardwareCoreCount - 1) : 1;

        // Plafonner à 16 : au-delà, les threads passent plus de temps à se synchroniser qu'à calculer (overhead)
        threadCount = std::min(threadCount, 16u);

        sessionOptions.SetIntraOpNumThreads(threadCount);

        // Empêcher les threads de se marcher sur les pieds
        sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

        // Activer l'Arena Allocator (limite l'utilisation du kernel_task)
        sessionOptions.EnableCpuMemArena();

        // Optimisation maximale du graphe
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Activation de l'accélération matérielle Apple Silicon -> seulement si le code tourne sur une machine Apple
        #ifdef __APPLE__
        // Les exports RACO/ALIKED sont optimisés pour TensorRT ; l'Execution Provider CoreML ne supporte pas
        // correctement certains opérateurs dynamiques de ce graphe. SuperPoint/DISK conservent l'accélération CoreML.
        const bool isRacoAlikedModel =
            configuredPath.find("raco_aliked") != std::string::npos;
        if (!isRacoAlikedModel) {
            uint32_t coreMlFlags = 0;
            (void)OrtSessionOptionsAppendExecutionProvider_CoreML(
                sessionOptions, coreMlFlags);
        }
        #endif

        // Instancie un objet Ort::Session (le moteur d'inférence d'ONNX) et l'enveloppe dans un pointeur intelligent
        // (unique_ptr) : la mémoire allouée pour la session est libérée automatiquement, sans fuite, quand l'objet
        // LightGlueMatcherClass est détruit. La session est gardée dans le champ session, pour la méthode match().
        this->session = std::make_unique<Ort::Session>(this->onnxEnvironment, modelPath.c_str(), sessionOptions);

        std::cout << "[DEBUG] Initialisation du LightGlueMatcherClass : fin\n";
    }

    bool LightGlueMatcherClass::match(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                                      DataNs::Matches& matches) {
        std::cout << "[DEBUG] match LightGlue : début\n";

        // Chronomètre pour mesurer le temps d'exécution de la méthode match()
        const auto startTime = std::chrono::steady_clock::now();

        // On supprime les anciens résultats si la sortie est réutilisée.
        matches.pointsInA.clear();
        matches.pointsInB.clear();
        matches.confidenceScores.clear();
        matches.translationX = 0.0f;
        matches.translationY = 0.0f;

        if (imageA.empty() || imageB.empty()) {
            return false;
        }

        // Test de la session lors de l'exécution -> si la session n'est pas initialisée, on génère une erreur, sinon
        // on continue.
        if (!this->session) {
            throw std::runtime_error("LightGlue: the ONNX session is not initialized.");
        }

        // Les images transmises sont déjà les bandes d'overlap prédécoupées
        (void)context;

        // Détection automatique : le modèle attend du niveaux de gris (1 canal) ou du RGB (3 canaux)
        bool useGreyscale = false;
        Ort::TypeInfo typeInfo = this->session->GetInputTypeInfo(0);
        auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
        std::vector<int64_t> inputDimensions = tensorInfo.GetShape();

        // Si la dimension des canaux (index 1) est 1, alors c'est SuperPoint (grayscale)
        if (inputDimensions.size() >= 2 && inputDimensions[1] == 1) {
            useGreyscale = true;
            std::cout << "[DEBUG] Modèle " << (useGreyscale ? "Grayscale" : "RGB") << " détecté.\n";
        }

        // Préparer les deux bandes de recouvrement dans le même format pour le modèle.
        float scaleXA = 1.0f, scaleYA = 1.0f, scaleXB = 1.0f, scaleYB = 1.0f;
        cv::Mat blobA = preprocessImage(imageA, "image A", scaleXA, scaleYA, useGreyscale);
        cv::Mat blobB = preprocessImage(imageB, "image B", scaleXB, scaleYB, useGreyscale);

        // Les modèles RACO/ALIKED+LightGlue exportés récemment utilisent une entrée batch unique et trois sorties :
        // keypoints, matches, mscores. Les modèles SuperPoint/DISK historiques restent traités plus bas avec leurs
        // deux entrées image0/image1 et leurs six sorties.
        if (this->session->GetInputCount() == 1) {
            const auto inputName = this->session->GetInputNameAllocated(
                0, Ort::AllocatorWithDefaultOptions{});
            const std::array<const char*, 3> outputNames = {
                "keypoints", "matches", "mscores"
            };

            if (this->session->GetOutputCount() != outputNames.size()) {
                throw std::runtime_error(
                    "LightGlue: a batch model must return keypoints, matches and mscores.");
            }

            if (blobA.size[1] != blobB.size[1] || blobA.size[2] != blobB.size[2] || blobA.size[3] != blobB.size[3]) {
                throw std::runtime_error(
                    "LightGlue: both strips must have the same size after preprocessing in batch mode.");
            }

            std::vector<float> batchData;
            batchData.reserve(blobA.total() + blobB.total());
            batchData.insert(batchData.end(), blobA.ptr<float>(),
                             blobA.ptr<float>() + blobA.total());
            batchData.insert(batchData.end(), blobB.ptr<float>(),
                             blobB.ptr<float>() + blobB.total());

            const std::vector<int64_t> batchShape = {
                2, blobA.size[1], blobA.size[2], blobA.size[3]
            };
            auto memoryInfo = Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator, OrtMemTypeDefault);
            auto inputTensor = Ort::Value::CreateTensor<float>(
                memoryInfo, batchData.data(), batchData.size(),
                batchShape.data(), batchShape.size());

            const std::array<const char*, 1> inputNames = {inputName.get()};
            auto outputTensors = this->session->Run(
                Ort::RunOptions{nullptr},
                inputNames.data(), &inputTensor, 1,
                outputNames.data(), outputNames.size());

            const auto keypointsInfo =
                outputTensors[0].GetTensorTypeAndShapeInfo();
            const auto matchesInfo =
                outputTensors[1].GetTensorTypeAndShapeInfo();
            const auto scoresInfo =
                outputTensors[2].GetTensorTypeAndShapeInfo();

            if (keypointsInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                matchesInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 ||
                scoresInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                throw std::runtime_error(
                    "LightGlue: unexpected RACO/ALIKED output types.");
            }

            const auto keypointsShape = keypointsInfo.GetShape();
            const auto matchesShape = matchesInfo.GetShape();
            const auto scoresShape = scoresInfo.GetShape();
            if (keypointsShape.size() != 3 || keypointsShape[0] != 2 ||
                keypointsShape[2] != 2 || matchesShape.size() != 2 ||
                matchesShape[1] != 3 || scoresShape.size() != 1 ||
                matchesShape[0] != scoresShape[0]) {
                throw std::runtime_error(
                    "LightGlue: unexpected RACO/ALIKED output shapes.");
            }

            const size_t keypointCount = static_cast<size_t>(keypointsShape[1]);
            const float* keypoints = outputTensors[0].GetTensorData<float>();
            const int64_t* matchTriples = outputTensors[1].GetTensorData<int64_t>();
            const float* scores = outputTensors[2].GetTensorData<float>();

            for (size_t matchIndex = 0; matchIndex < static_cast<size_t>(matchesShape[0]); ++matchIndex) {
                const int64_t batchIndex = matchTriples[matchIndex * 3];
                const int64_t keypointIndexA = matchTriples[matchIndex * 3 + 1];
                const int64_t keypointIndexB = matchTriples[matchIndex * 3 + 2];

                // Pour une paire, le batch entrelacé est [image A, image B].
                if (batchIndex != 0) {
                    throw std::runtime_error(
                        "LightGlue: unexpected RACO/ALIKED batch index.");
                }
                if (keypointIndexA < 0 || keypointIndexB < 0 ||
                    static_cast<size_t>(keypointIndexA) >= keypointCount ||
                    static_cast<size_t>(keypointIndexB) >= keypointCount) {
                    throw std::runtime_error(
                        "LightGlue: RACO/ALIKED keypoint index out of range.");
                }
                if (scores[matchIndex] < MINIMUM_CONFIDENCE_SCORE) {
                    continue;
                }

                const size_t offsetA = static_cast<size_t>(keypointIndexA) * 2;
                const size_t offsetB = (keypointCount +
                                        static_cast<size_t>(keypointIndexB)) * 2;
                matches.pointsInA.emplace_back(
                    keypoints[offsetA] / scaleXA,
                    keypoints[offsetA + 1] / scaleYA);
                matches.pointsInB.emplace_back(
                    keypoints[offsetB] / scaleXB,
                    keypoints[offsetB + 1] / scaleYB);
                matches.confidenceScores.push_back(scores[matchIndex]);
            }

            if (matches.pointsInA.empty()) {
                return false;
            }

            std::vector<float> translationsX, translationsY;
            for (size_t pointIndex = 0; pointIndex < matches.pointsInA.size(); ++pointIndex) {
                translationsX.push_back(matches.pointsInA[pointIndex].x - matches.pointsInB[pointIndex].x);
                translationsY.push_back(matches.pointsInA[pointIndex].y - matches.pointsInB[pointIndex].y);
            }
            matches.translationX = computeMedian(translationsX);
            matches.translationY = computeMedian(translationsY);

            std::cout << "[DEBUG] Modèle RACO/ALIKED+LightGlue exécuté\n";
            const auto endTime = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = endTime - startTime;
            std::cout << "[DEBUG] Temps total exécution match() : "
                      << elapsed.count() << " secondes\n";
            return true;
        }

        if (this->session->GetInputCount() != 2) {
            throw std::runtime_error(
                "LightGlue: unsupported number of ONNX inputs.");
        }

        // Lier la mémoire de ce blob à des tenseurs ONNX Runtime
        // Définir la forme (Shape) : [1, Canaux, Hauteur, Largeur]
        std::vector<int64_t> inputShapeA = {1, blobA.size[1], blobA.size[2], blobA.size[3]};
        std::vector<int64_t> inputShapeB = {1, blobB.size[1], blobB.size[2], blobB.size[3]};

        // Gère la mémoire RAM, nécessaire pour l'étape suivante (pour créer les tenseurs ONNX Runtime à partir des blobs
        // OpenCV)
        auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // Créer le tenseur pour l'image A
        Ort::Value tensorA = Ort::Value::CreateTensor<float>(
            memoryInfo,
            blobA.ptr<float>(), blobA.total(),
            inputShapeA.data(), inputShapeA.size()
        );

        // Créer le tenseur pour l'image B (idem)
        Ort::Value tensorB = Ort::Value::CreateTensor<float>(
            memoryInfo,
            blobB.ptr<float>(), blobB.total(),
            inputShapeB.data(), inputShapeB.size()
        );

        // Inférence

        // Noms des nœuds d'entrée/sortie (à adapter selon l'export ONNX -> vérifié via Netron)
        const char* inputNames[] = {"image0", "image1"};
        const char* outputNames[] = {"matches0", "matches1", "mscores0", "mscores1", "kpts0", "kpts1"};   // -> 6 outputs

        std::vector<Ort::Value> inputs;
        inputs.push_back(std::move(tensorA));
        inputs.push_back(std::move(tensorB));

        std::cout << "[DEBUG] match LightGlue : exécution du modèle\n";

        // Exécution du modèle
        auto outputTensors = this->session->Run(
            Ort::RunOptions{nullptr},
            inputNames, inputs.data(), inputs.size(),
            outputNames, 6   // Nombre d'outputs attendus
        );

        // On vérifie que le modèle a renvoyé toutes les sorties attendues.
        if (outputTensors.size() != 6) {
            throw std::runtime_error("LightGlue: the model must return exactly 6 tensors.");
        }
        std::cout << "[DEBUG] match LightGlue : traitement des résultats\n";

        // Traitement des résultats obtenus

        // Notes sur les sorties :
        //  - outputTensors contient 6 tenseurs qui correspondent à : matches0, matches1, mscores0, mscores1, kpts0, kpts1 ;
        //  - outputTensors[0] (matches0) contient un tableau d'entiers (int64_t) représentant les indices des
        //    correspondances pour l'image 0 : à l'index i de ce tableau, on trouve l'index du point correspondant dans
        //    l'image 1 -> [-1, 12, 6, -1, 87] ;
        //  - outputTensors[1] (matches1) -> [-1, -1, 75, -1, 26] ;
        //  - outputTensors[2] (mscores0) contient les scores de confiance calculés par le réseau pour chaque paire de
        //    points -> [6.2772e-17, 0.999883, 0.999937, 0, 0.999909] ;
        //  - outputTensors[3] (mscores1) -> [0, 0, 0.997286, 0, 0.996805] ;
        //  - outputTensors[4] (kpts0) contient les coordonnées (x, y) de tous les points détectés dans l'image 0
        //    -> [224, 229, 154, 32, 155] ;
        //  - outputTensors[5] (kpts1) contient les coordonnées (x, y) de tous les points détectés dans l'image 1
        //    -> [198, 229, 170, 228, 49].

        // Extraction des informations pour outputTensors[0] (matches0)
        auto matches0Info = outputTensors[0].GetTensorTypeAndShapeInfo();
        // Les indices de correspondance doivent être des entiers sur 64 bits.
        if (matches0Info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
            throw std::runtime_error("LightGlue: matches0 must be an INT64 tensor.");
        }
        size_t matchElementCount = matches0Info.GetElementCount();
        const auto matches1Info = outputTensors[1].GetTensorTypeAndShapeInfo();
        // On vérifie aussi le type de la deuxième liste d'indices.
        if (matches1Info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
            throw std::runtime_error("LightGlue: matches1 must be an INT64 tensor.");
        }

        // LightGlue renvoie des indices sous forme d'entiers 64 bits (int64_t)
        int64_t* matches0Data = outputTensors[0].GetTensorMutableData<int64_t>();

        auto scores0Info = outputTensors[2].GetTensorTypeAndShapeInfo();
        // Il faut un score FLOAT pour chaque élément de matches0.
        if (scores0Info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            scores0Info.GetElementCount() != matchElementCount) {
            throw std::runtime_error(
                "LightGlue: mscores0 must be a FLOAT tensor with one score per matches0 entry.");
        }
        float* scores0Data = outputTensors[2].GetTensorMutableData<float>();
        const auto scores1Info = outputTensors[3].GetTensorTypeAndShapeInfo();
        // Cette sortie n'est pas utilisée, mais son type doit rester compatible.
        if (scores1Info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw std::runtime_error("LightGlue: mscores1 must be a FLOAT tensor.");
        }

        // Pourquoi on n'extrait que les données de scores0, et pas celles de scores1 ? Quand LightGlue trouve une
        // correspondance valide entre un point A (image A) et un point B (image B), il s'agit d'une seule et même paire.
        // scores0[i] contient le score de confiance pour le point A (qui a été apparié au point B), scores1[matchedIndex]
        // celui du point B (apparié au point A). Pour une paire validée, ces deux scores sont identiques (ou représentent
        // exactement la même information de confiance pour cette liaison).

        // Extraction des coordonnées des points-clés (float)
        auto keypoints0Info = outputTensors[4].GetTensorTypeAndShapeInfo();
        auto keypoints1Info = outputTensors[5].GetTensorTypeAndShapeInfo();
        auto keypointsType = keypoints0Info.GetElementType();
        // Les deux listes de points doivent avoir le même type.
        if (keypointsType != keypoints1Info.GetElementType() ||
            (keypointsType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
             keypointsType != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)) {
            throw std::runtime_error(
                "LightGlue: kpts0 and kpts1 must have the same type, FLOAT or INT64.");
        }

        const size_t keypoints0ElementCount = keypoints0Info.GetElementCount();
        const size_t keypoints1ElementCount = keypoints1Info.GetElementCount();
        // Chaque point contient deux valeurs : x et y.
        if (keypoints0ElementCount % 2 != 0 || keypoints1ElementCount % 2 != 0 ||
            keypoints0ElementCount / 2 < matchElementCount) {
            throw std::runtime_error(
                "LightGlue: kpts0/kpts1 tensor sizes are incompatible with matches0.");
        }
        const size_t keypoints1Count = keypoints1ElementCount / 2;

        float* keypoints0Float = nullptr; float* keypoints1Float = nullptr;
        int64_t* keypoints0Integer = nullptr; int64_t* keypoints1Integer = nullptr;

        if (keypointsType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            keypoints0Float = outputTensors[4].GetTensorMutableData<float>();
            keypoints1Float = outputTensors[5].GetTensorMutableData<float>();
        } else if (keypointsType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
            keypoints0Integer = outputTensors[4].GetTensorMutableData<int64_t>();
            keypoints1Integer = outputTensors[5].GetTensorMutableData<int64_t>();
        } else {
            throw std::runtime_error("LightGlue: kpts0/kpts1 tensors are neither float nor int64.");
        }

        // On associe maintenant toutes les coordonnées :
        //  - matchedIndex > -1 : confirme que le point i de l'image A a bien été apparié ;
        //  - extraction (x, y) : les données des points sont stockées à plat en mémoire (x0, y0, x1, y1...). Pour
        //    obtenir le point i, on lit les indices i * 2 (pour X) et i * 2 + 1 (pour Y). On fait la même chose pour
        //    l'image B en utilisant matchedIndex ;
        //  - stockage : les coordonnées sont converties en cv::Point2f et ajoutées à matches.pointsInA et
        //    matches.pointsInB. pointsInA[k] et pointsInB[k] sont alors les coordonnées exactes du k-ème match valide.

        std::cout << "[DEBUG] match LightGlue : association des coordonnées\n";

        for (size_t keypointIndex = 0; keypointIndex < matchElementCount; ++keypointIndex) {
            int64_t matchedIndex = matches0Data[keypointIndex];

            if (matchedIndex >= 0 && static_cast<uint64_t>(matchedIndex) < keypoints1Count) {
                const float confidenceScore = scores0Data[keypointIndex];
                if (confidenceScore < MINIMUM_CONFIDENCE_SCORE) {
                    continue;
                }

                float xA, yA, xB, yB;

                // On lit les coordonnées seulement après avoir vérifié l'indice.
                if (keypoints0Float) {
                    xA = keypoints0Float[keypointIndex * 2];
                    yA = keypoints0Float[keypointIndex * 2 + 1];
                    xB = keypoints1Float[matchedIndex * 2];
                    yB = keypoints1Float[matchedIndex * 2 + 1];
                } else {
                    xA = static_cast<float>(keypoints0Integer[keypointIndex * 2]);
                    yA = static_cast<float>(keypoints0Integer[keypointIndex * 2 + 1]);
                    xB = static_cast<float>(keypoints1Integer[matchedIndex * 2]);
                    yB = static_cast<float>(keypoints1Integer[matchedIndex * 2 + 1]);
                }

                matches.pointsInA.push_back(cv::Point2f(xA / scaleXA, yA / scaleYA));
                matches.pointsInB.push_back(cv::Point2f(xB / scaleXB, yB / scaleYB));

                // On enregistre le score correspondant
                matches.confidenceScores.push_back(confidenceScore);
            } else if (matchedIndex >= 0) {
                throw std::runtime_error(
                    "LightGlue: matches0 contains an index out of range in kpts1.");
            }
        }

        // Les trois vecteurs doivent toujours contenir le même nombre de résultats.
        if (matches.pointsInA.size() != matches.pointsInB.size() ||
            matches.pointsInA.size() != matches.confidenceScores.size()) {
            throw std::runtime_error("LightGlue: output vectors have inconsistent sizes.");
        }

        if (matches.pointsInA.empty()) {
            return false;
        }

        std::cout << "[DEBUG] match LightGlue : calcul de dx et dy\n";

        // Calcul du décalage (translationX, translationY) et enregistrement dans la structure de sortie.
        std::vector<float> translationsX;
        std::vector<float> translationsY;

        // Calculer la différence pour chaque paire de points
        for (size_t pointIndex = 0; pointIndex < matches.pointsInA.size(); ++pointIndex) {
            translationsX.push_back(matches.pointsInA[pointIndex].x - matches.pointsInB[pointIndex].x);
            translationsY.push_back(matches.pointsInA[pointIndex].y - matches.pointsInB[pointIndex].y);
        }

        matches.translationX = computeMedian(translationsX);
        matches.translationY = computeMedian(translationsY);

        std::cout << "Dx : " << matches.translationX << ", Dy : " << matches.translationY << '\n';

        const auto endTime = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;
        std::cout << "[DEBUG] Temps total exécution match() : " << elapsed.count() << " secondes\n";

        return true;
    }

}
