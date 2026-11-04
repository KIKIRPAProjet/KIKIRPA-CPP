// src/backend/algorithms/stitching/lightglue/lightglue_matcher.cpp
#include "algorithms/stitching/lightglue/lightglue_matcher.h"

#include "core/data/algorithm_types.h"

#include <nlohmann/json.hpp>
#include <onnxruntime_c_api.h>
#include <onnxruntime_cxx_api.h>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/dnn/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>     // Pour la gestion dynamique du nombre de cœurs disponibles
#include <vector>

#if defined(__APPLE__) && __has_include(<coreml_provider_factory.h>)
#include <coreml_provider_factory.h>
#define STITCHING_HAS_COREML_PROVIDER 1
#endif

#ifndef STITCHING_PROJECT_ROOT
#define STITCHING_PROJECT_ROOT "."
#endif

namespace LightGlueNs {

    namespace {

        // Seules les correspondances dont le score atteint ce seuil sont conservées.
        constexpr float MINIMUM_CONFIDENCE_SCORE = 0.9f;

        // RACO/ALIKED+LightGlue exige des dimensions multiples de 32, même sans réduction. Cette contrainte reste
        // valide pour SuperPoint/DISK.
        constexpr int MAXIMUM_EDGE = 3200;
        constexpr int SIZE_MULTIPLE = 32;

        // Au-delà, les threads passent plus de temps à se synchroniser qu'à calculer.
        constexpr unsigned int MAXIMUM_THREAD_COUNT = 16U;

        constexpr double MAXIMUM_VALUE_8_BITS = 255.0;
        constexpr double MAXIMUM_VALUE_16_BITS = 65535.0;
        constexpr float MIDDLE_VALUE_COUNT = 2.0f;
        constexpr std::size_t COORDINATES_PER_POINT = 2;

        // Modèles RACO/ALIKED : une entrée batch, trois sorties.
        constexpr std::size_t BATCH_OUTPUT_COUNT = 3;
        constexpr std::size_t VALUES_PER_BATCH_MATCH = 3;

        // Modèles SuperPoint/DISK : deux entrées, six sorties, dans cet ordre.
        //  - matches0 : à l'index i, l'index du point de l'image B apparié au point i de l'image A, ou -1 ;
        //  - mscores0 : le score de chaque élément de matches0 ;
        //  - kpts0, kpts1 : les coordonnées de tous les points détectés, à plat (x0, y0, x1, y1...).
        // matches1 et mscores1 décrivent les mêmes paires depuis l'image B : seul leur type est vérifié.
        constexpr std::size_t MATCHES0 = 0;
        constexpr std::size_t MATCHES1 = 1;
        constexpr std::size_t MSCORES0 = 2;
        constexpr std::size_t MSCORES1 = 3;
        constexpr std::size_t KPTS0 = 4;
        constexpr std::size_t KPTS1 = 5;
        constexpr std::size_t PAIR_OUTPUT_COUNT = 6;

        // Une bande prête pour le modèle et les échelles qui permettent de revenir à ses coordonnées d'origine.
        struct PreparedImage {
            cv::Mat blob;
            float scaleX = 1.0f;
            float scaleY = 1.0f;
        };

        cv::Size computeModelSize(const cv::Mat& image) {
            const float reduction = std::min(
                1.0f, static_cast<float>(MAXIMUM_EDGE) / static_cast<float>(std::max(image.cols, image.rows)));
            const auto roundToMultiple = [reduction](int length) {
                const float scaledLength = static_cast<float>(length) * reduction / static_cast<float>(SIZE_MULTIPLE);
                return std::max(SIZE_MULTIPLE, static_cast<int>(std::round(scaledLength)) * SIZE_MULTIPLE);
            };
            return {roundToMultiple(image.cols), roundToMultiple(image.rows)};
        }

        // Le modèle attend une image avec 1 canal (ex : SuperPoint) ou 3 canaux (ex : DISK, ALIKED).
        cv::Mat convertChannels(const cv::Mat& image, bool isGreyscale) {
            cv::Mat converted = image;
            if (isGreyscale && image.channels() == 3) {
                cv::cvtColor(image, converted, cv::COLOR_BGR2GRAY);
            } else if (isGreyscale && image.channels() == 4) {
                cv::cvtColor(image, converted, cv::COLOR_BGRA2GRAY);
            } else if (!isGreyscale && image.channels() == 1) {
                cv::cvtColor(image, converted, cv::COLOR_GRAY2BGR);
            } else if (!isGreyscale && image.channels() == 4) {
                cv::cvtColor(image, converted, cv::COLOR_BGRA2BGR);
            }
            return converted;
        }

        void checkInputImage(const cv::Mat& image) {
            if (image.empty()) {
                throw std::runtime_error("LightGlue: image must not be empty");
            }
            if (image.dims != 2 || (image.depth() != CV_8U && image.depth() != CV_16U) ||
                (image.channels() != 1 && image.channels() != 3 && image.channels() != 4)) {
                throw std::invalid_argument("LightGlue: expected a 2D 8/16-bit image with 1, 3 or 4 channels");
            }
        }

        // Prépare une image au format attendu par le modèle ONNX.
        PreparedImage preprocessImage(const cv::Mat& image, bool isGreyscale) {
            checkInputImage(image);

            const cv::Size modelSize = computeModelSize(image);
            cv::Mat resizedImage = image;
            if (modelSize != image.size()) {
                cv::resize(image, resizedImage, modelSize, 0.0, 0.0, cv::INTER_AREA);
            }

            // Convertir l'image en float, normaliser les valeurs et passer au format NCHW.
            cv::Mat finalImage = convertChannels(resizedImage, isGreyscale);
            const double maximumValue = image.depth() == CV_16U ? MAXIMUM_VALUE_16_BITS : MAXIMUM_VALUE_8_BITS;
            finalImage.convertTo(finalImage, CV_32F, 1.0 / maximumValue);

            PreparedImage prepared;
            prepared.blob = cv::dnn::blobFromImage(finalImage, 1.0, cv::Size(), cv::Scalar(), !isGreyscale, false);
            prepared.scaleX = static_cast<float>(modelSize.width) / static_cast<float>(image.cols);
            prepared.scaleY = static_cast<float>(modelSize.height) / static_cast<float>(image.rows);
            return prepared;
        }

        // Si la dimension des canaux (index 1) est 1, le modèle attend du niveaux de gris (ex : SuperPoint).
        bool modelExpectsGreyscale(const Ort::Session& session) {
            const Ort::TypeInfo typeInfo = session.GetInputTypeInfo(0);
            const std::vector<int64_t> inputDimensions = typeInfo.GetTensorTypeAndShapeInfo().GetShape();
            return inputDimensions.size() >= 2 && inputDimensions[1] == 1;
        }

        // Essaie le chemin configuré en premier pour que les chemins absolus et les chemins relatifs au répertoire de
        // travail de l'appelant continuent de fonctionner.
        std::filesystem::path resolveModelPath(const std::string& configuredPath) {
            std::filesystem::path modelPath = configuredPath;
            if (!modelPath.is_absolute() && !std::filesystem::is_regular_file(modelPath)) {
                const auto workingDirectory = std::filesystem::current_path();
                const std::array<std::filesystem::path, 3> candidatePaths = {
                    workingDirectory.parent_path() / modelPath,
                    std::filesystem::path(STITCHING_PROJECT_ROOT) / modelPath,
                    workingDirectory / "build" / modelPath,
                };
                const auto found = std::find_if(candidatePaths.begin(), candidatePaths.end(), [](const auto& candidate) {
                    return std::filesystem::is_regular_file(candidate);
                });
                if (found != candidatePaths.end()) {
                    modelPath = *found;
                }
            }

            // Erreur explicite si le modèle n'est pas trouvé, pour éviter des erreurs plus tard
            if (!std::filesystem::is_regular_file(modelPath)) {
                throw std::invalid_argument("LightGlue model not found: " + configuredPath + " (working directory: " +
                                            std::filesystem::current_path().string() + ")");
            }
            return modelPath;
        }

        Ort::SessionOptions createSessionOptions(const std::string& configuredPath) {
            Ort::SessionOptions sessionOptions;

            // SetIntraOpNumThreads(0) (laisser ONNX Runtime décider) ne fonctionne pas correctement (du moins sur Mac) :
            // il utilise moins de 2 cœurs alors que d'autres sont disponibles. Si la détection échoue (0), on sécurise
            // à 1, et on garde 1 cœur libre pour le système afin d'éviter que l'ordinateur ne se fige.
            const unsigned int hardwareCoreCount = std::thread::hardware_concurrency();
            const unsigned int threadCount = (hardwareCoreCount > 1) ? (hardwareCoreCount - 1) : 1;
            sessionOptions.SetIntraOpNumThreads(static_cast<int>(std::min(threadCount, MAXIMUM_THREAD_COUNT)));

            sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);   // Évite que les threads se gênent
            sessionOptions.EnableCpuMemArena();                               // Limite l'utilisation du kernel_task
            sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            // Accélération matérielle Apple Silicon. Les exports RACO/ALIKED sont optimisés pour TensorRT ; CoreML ne
            // supporte pas correctement certains opérateurs dynamiques de ce graphe.
            #if defined(STITCHING_HAS_COREML_PROVIDER)
            if (configuredPath.find("raco_aliked") == std::string::npos) {
                uint32_t coreMlFlags = 0;
                Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(sessionOptions, coreMlFlags));
            }
            #else
            (void)configuredPath;
            #endif

            return sessionOptions;
        }

        cv::Point2f restorePoint(float x, float y, const PreparedImage& image) {
            return {x / image.scaleX, y / image.scaleY};
        }

        std::vector<Ort::Value> runBatchModel(Ort::Session& session, const PreparedImage& imageA,
                                              const PreparedImage& imageB) {
            const auto inputName = session.GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions{});
            const std::array<const char*, BATCH_OUTPUT_COUNT> outputNames = {"keypoints", "matches", "mscores"};
            if (session.GetOutputCount() != outputNames.size()) {
                throw std::runtime_error("LightGlue: a batch model must return keypoints, matches and mscores.");
            }

            const cv::Mat& blobA = imageA.blob;
            const cv::Mat& blobB = imageB.blob;
            if (blobA.size[1] != blobB.size[1] || blobA.size[2] != blobB.size[2] || blobA.size[3] != blobB.size[3]) {
                throw std::runtime_error("LightGlue: both strips must have the same size after preprocessing in batch mode.");
            }

            std::vector<float> batchData(blobA.ptr<float>(), blobA.ptr<float>() + blobA.total());
            batchData.insert(batchData.end(), blobB.ptr<float>(), blobB.ptr<float>() + blobB.total());
            const std::array<int64_t, 4> batchShape = {2, blobA.size[1], blobA.size[2], blobA.size[3]};
            const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            const auto inputTensor = Ort::Value::CreateTensor<float>(memoryInfo, batchData.data(), batchData.size(),
                                                                     batchShape.data(), batchShape.size());

            const std::array<const char*, 1> inputNames = {inputName.get()};
            auto outputs = session.Run(Ort::RunOptions{nullptr}, inputNames.data(), &inputTensor, 1,
                                       outputNames.data(), outputNames.size());
            if (outputs.size() != outputNames.size()) {
                throw std::runtime_error("LightGlue: expected three batch outputs.");
            }
            return outputs;
        }

        void checkBatchOutputs(const std::vector<Ort::Value>& outputs) {
            const auto keypointsInfo = outputs[0].GetTensorTypeAndShapeInfo();
            const auto matchesInfo = outputs[1].GetTensorTypeAndShapeInfo();
            const auto scoresInfo = outputs[2].GetTensorTypeAndShapeInfo();
            if (keypointsInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                matchesInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 ||
                scoresInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                throw std::runtime_error("LightGlue: unexpected RACO/ALIKED output types.");
            }

            const auto keypointsShape = keypointsInfo.GetShape();
            const auto matchesShape = matchesInfo.GetShape();
            const auto scoresShape = scoresInfo.GetShape();
            if (keypointsShape.size() != 3 || keypointsShape[0] != 2 || keypointsShape[2] != 2 ||
                matchesShape.size() != 2 || matchesShape[1] != 3 || scoresShape.size() != 1 ||
                matchesShape[0] != scoresShape[0]) {
                throw std::runtime_error("LightGlue: unexpected RACO/ALIKED output shapes.");
            }
        }

        // Pour une paire, le batch entrelacé est [image A, image B] : les points de B suivent ceux de A.
        void appendBatchMatches(const std::vector<Ort::Value>& outputs, const PreparedImage& imageA,
                                const PreparedImage& imageB, DataNs::Matches& matches) {
            const auto keypointCount = static_cast<std::size_t>(outputs[0].GetTensorTypeAndShapeInfo().GetShape()[1]);
            const auto matchCount = static_cast<std::size_t>(outputs[1].GetTensorTypeAndShapeInfo().GetShape()[0]);
            const auto* keypoints = outputs[0].GetTensorData<float>();
            const auto* matchTriples = outputs[1].GetTensorData<int64_t>();
            const auto* scores = outputs[2].GetTensorData<float>();

            for (std::size_t matchIndex = 0; matchIndex < matchCount; ++matchIndex) {
                const int64_t* triple = matchTriples + matchIndex * VALUES_PER_BATCH_MATCH;
                if (triple[0] != 0) {
                    throw std::runtime_error("LightGlue: unexpected RACO/ALIKED batch index.");
                }
                if (triple[1] < 0 || triple[2] < 0 || static_cast<std::size_t>(triple[1]) >= keypointCount ||
                    static_cast<std::size_t>(triple[2]) >= keypointCount) {
                    throw std::runtime_error("LightGlue: RACO/ALIKED keypoint index out of range.");
                }
                if (scores[matchIndex] < MINIMUM_CONFIDENCE_SCORE) {
                    continue;
                }
                const float* pointA = keypoints + static_cast<std::size_t>(triple[1]) * COORDINATES_PER_POINT;
                const float* pointB = keypoints + (keypointCount + static_cast<std::size_t>(triple[2])) * COORDINATES_PER_POINT;
                matches.pointsInA.push_back(restorePoint(pointA[0], pointA[1], imageA));
                matches.pointsInB.push_back(restorePoint(pointB[0], pointB[1], imageB));
                matches.confidenceScores.push_back(scores[matchIndex]);
            }
        }

        std::vector<Ort::Value> runPairModel(Ort::Session& session, PreparedImage& imageA, PreparedImage& imageB) {
            const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            const auto createTensor = [&memoryInfo](cv::Mat& blob) {
                const std::array<int64_t, 4> shape = {1, blob.size[1], blob.size[2], blob.size[3]};
                return Ort::Value::CreateTensor<float>(memoryInfo, blob.ptr<float>(), blob.total(), shape.data(), shape.size());
            };
            std::vector<Ort::Value> inputs;
            inputs.push_back(createTensor(imageA.blob));
            inputs.push_back(createTensor(imageB.blob));

            // Noms des nœuds d'entrée/sortie (à adapter selon l'export ONNX -> vérifié via Netron)
            const std::array<const char*, 2> inputNames = {"image0", "image1"};
            const std::array<const char*, PAIR_OUTPUT_COUNT> outputNames = {"matches0", "matches1", "mscores0",
                                                                            "mscores1", "kpts0", "kpts1"};
            auto outputs = session.Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), inputs.size(),
                                       outputNames.data(), outputNames.size());
            if (outputs.size() != PAIR_OUTPUT_COUNT) {
                throw std::runtime_error("LightGlue: the model must return exactly 6 tensors.");
            }
            return outputs;
        }

        void checkPairOutputs(const std::vector<Ort::Value>& outputs) {
            const auto elementType = [&outputs](std::size_t output) {
                return outputs[output].GetTensorTypeAndShapeInfo().GetElementType();
            };
            const auto elementCount = [&outputs](std::size_t output) {
                return outputs[output].GetTensorTypeAndShapeInfo().GetElementCount();
            };

            if (elementType(MATCHES0) != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 ||
                elementType(MATCHES1) != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
                throw std::runtime_error("LightGlue: matches0 and matches1 must be INT64 tensors.");
            }
            if (elementType(MSCORES0) != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                elementType(MSCORES1) != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                elementCount(MSCORES0) != elementCount(MATCHES0)) {
                throw std::runtime_error("LightGlue: mscores must be FLOAT tensors with one score per matches0 entry.");
            }
            if (elementType(KPTS0) != elementType(KPTS1) || (elementType(KPTS0) != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
                                                             elementType(KPTS0) != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)) {
                throw std::runtime_error("LightGlue: kpts0 and kpts1 must have the same type, FLOAT or INT64.");
            }
            if (elementCount(KPTS0) % COORDINATES_PER_POINT != 0 || elementCount(KPTS1) % COORDINATES_PER_POINT != 0 ||
                elementCount(KPTS0) / COORDINATES_PER_POINT < elementCount(MATCHES0)) {
                throw std::runtime_error("LightGlue: kpts0/kpts1 tensor sizes are incompatible with matches0.");
            }
        }

        cv::Point2f readKeypoint(const Ort::Value& keypoints, std::size_t pointIndex, const PreparedImage& image) {
            const std::size_t offset = pointIndex * COORDINATES_PER_POINT;
            if (keypoints.GetTensorTypeAndShapeInfo().GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                const auto* values = keypoints.GetTensorData<float>();
                return restorePoint(values[offset], values[offset + 1], image);
            }
            const auto* values = keypoints.GetTensorData<int64_t>();
            return restorePoint(static_cast<float>(values[offset]), static_cast<float>(values[offset + 1]), image);
        }

        void appendPairMatches(const std::vector<Ort::Value>& outputs, const PreparedImage& imageA,
                               const PreparedImage& imageB, DataNs::Matches& matches) {
            const std::size_t matchElementCount = outputs[MATCHES0].GetTensorTypeAndShapeInfo().GetElementCount();
            const std::size_t keypoints1Count =
                outputs[KPTS1].GetTensorTypeAndShapeInfo().GetElementCount() / COORDINATES_PER_POINT;
            const auto* matchedIndexes = outputs[MATCHES0].GetTensorData<int64_t>();
            const auto* scores = outputs[MSCORES0].GetTensorData<float>();

            for (std::size_t keypointIndex = 0; keypointIndex < matchElementCount; ++keypointIndex) {
                const int64_t matchedIndex = matchedIndexes[keypointIndex];
                if (matchedIndex < 0) {
                    continue;
                }
                if (static_cast<std::size_t>(matchedIndex) >= keypoints1Count) {
                    throw std::runtime_error("LightGlue: matches0 contains an index out of range in kpts1.");
                }
                if (scores[keypointIndex] < MINIMUM_CONFIDENCE_SCORE) {
                    continue;
                }
                matches.pointsInA.push_back(readKeypoint(outputs[KPTS0], keypointIndex, imageA));
                matches.pointsInB.push_back(readKeypoint(outputs[KPTS1], static_cast<std::size_t>(matchedIndex), imageB));
                matches.confidenceScores.push_back(scores[keypointIndex]);
            }
        }

        // Lance le modèle sur les deux bandes et ajoute les correspondances assez sûres à matches.
        void collectMatches(Ort::Session& session, PreparedImage& imageA, PreparedImage& imageB,
                            DataNs::Matches& matches) {
            if (session.GetInputCount() == 1) {
                const auto outputs = runBatchModel(session, imageA, imageB);
                checkBatchOutputs(outputs);
                appendBatchMatches(outputs, imageA, imageB, matches);
            } else if (session.GetInputCount() == 2) {
                const auto outputs = runPairModel(session, imageA, imageB);
                checkPairOutputs(outputs);
                appendPairMatches(outputs, imageA, imageB, matches);
            } else {
                throw std::runtime_error("LightGlue: unsupported number of ONNX inputs.");
            }
        }

        // Médiane ; pour un nombre pair de valeurs, moyenne des deux valeurs du milieu. La médiane limite l'effet des
        // mauvaises correspondances.
        float computeMedian(std::vector<float>& values) {
            const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
            std::nth_element(values.begin(), middle, values.end());
            if (values.size() % 2 == 0) {
                const float upper = *middle;
                std::nth_element(values.begin(), middle - 1, values.end());
                return (*(middle - 1) + upper) / MIDDLE_VALUE_COUNT;
            }
            return *middle;
        }

        void computeTranslation(DataNs::Matches& matches) {
            std::vector<float> translationsX;
            std::vector<float> translationsY;
            for (std::size_t pointIndex = 0; pointIndex < matches.pointsInA.size(); ++pointIndex) {
                translationsX.push_back(matches.pointsInA[pointIndex].x - matches.pointsInB[pointIndex].x);
                translationsY.push_back(matches.pointsInA[pointIndex].y - matches.pointsInB[pointIndex].y);
            }
            matches.translationX = computeMedian(translationsX);
            matches.translationY = computeMedian(translationsY);
        }

    }

    struct LightGlueMatcherClass::SessionState {
        Json parameters;
        // Permet de ne pas recharger le modèle à chaque paire d'images
        Ort::Env onnxEnvironment{ORT_LOGGING_LEVEL_WARNING, "LightGlue"};
        std::unique_ptr<Ort::Session> session;
    };

    LightGlueMatcherClass::~LightGlueMatcherClass() = default;

    LightGlueMatcherClass::LightGlueMatcherClass() : LightGlueMatcherClass(Json::object()) {}

    // Cherche le fichier du modèle ONNX sélectionné et initialise la session ONNX
    LightGlueMatcherClass::LightGlueMatcherClass(const Json& initialParameters)
        : state(std::make_unique<SessionState>()) {
        state->parameters = initialParameters;

        // Lire le chemin du modèle passé par la configuration du pipeline
        const auto configuredPath = state->parameters.value("model_path", std::string{});
        if (configuredPath.empty()) {
            throw std::invalid_argument("LightGlue model_path is missing");
        }

        const std::filesystem::path modelPath = resolveModelPath(configuredPath);
        state->parameters["model_path"] = modelPath.string();

        // La session est gardée dans le pointeur intelligent, pour la méthode match() : sa mémoire est libérée
        // automatiquement quand l'objet LightGlueMatcherClass est détruit.
        const Ort::SessionOptions sessionOptions = createSessionOptions(configuredPath);
        state->session = std::make_unique<Ort::Session>(state->onnxEnvironment, modelPath.c_str(), sessionOptions);
    }

    bool LightGlueMatcherClass::match(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                                      DataNs::Matches& matches) {
        // On supprime les anciens résultats si la sortie est réutilisée.
        matches = DataNs::Matches{};

        if (imageA.empty() || imageB.empty()) {
            return false;
        }
        if (!state->session) {
            throw std::runtime_error("LightGlue: the ONNX session is not initialized.");
        }

        // Les images transmises sont déjà les bandes d'overlap prédécoupées
        (void)context;

        Ort::Session& session = *state->session;
        const bool useGreyscale = modelExpectsGreyscale(session);
        PreparedImage preparedA = preprocessImage(imageA, useGreyscale);
        PreparedImage preparedB = preprocessImage(imageB, useGreyscale);

        collectMatches(session, preparedA, preparedB, matches);
        if (matches.pointsInA.empty()) {
            return false;
        }
        computeTranslation(matches);
        return true;
    }

}
