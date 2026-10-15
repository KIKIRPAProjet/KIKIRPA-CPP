#include "algorithms/stitching/lightglue/LightGlueMatcher.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono> // Pour chronométrer certaines étapes (surtout l'inférence du modèle)

#include <thread> // Pour la gestion dynamique du nombre de coeurs disponibles

#ifdef __APPLE__ // Permet de ne pas planter si le code tourne sur une machine non Apple
#include <coreml_provider_factory.h>
#endif

#ifndef STITCHING_PROJECT_ROOT
#define STITCHING_PROJECT_ROOT "."
#endif

namespace {

// Seules les correspondances dont le score atteint ce seuil sont conservées.
constexpr float kMinConfidenceScore = 0.9f;

} // namespace

namespace stitching::matching {

// Initialisation du matcher LightGlue
// Cherche le fichier du modèle ONNX sélectionné et initialise la session ONNX
LightGlueMatcher::LightGlueMatcher(const Json& params) : params(params) {
    std::cout << "[DEBUG] Initialisation du LightGlueMatcher : début\n";

    // Lire le chemin du modèle passé par la configuration du pipeline
    const auto configuredPath = this->params.value("model_path", std::string{});
    if (configuredPath.empty()) {
        throw std::invalid_argument("LightGlue model_path is missing");
    }

    // Essayer le chemin configuré en premier pour que les chemins absolus et les chemins relatifs au répertoire de travail de l'appelant continuent de fonctionner
    std::filesystem::path modelPath = configuredPath;
    if (!modelPath.is_absolute() &&
        !std::filesystem::is_regular_file(modelPath)) {
        const auto workingDirectory = std::filesystem::current_path();
        const std::array<std::filesystem::path, 3> candidates = {
            workingDirectory.parent_path() / modelPath,
            std::filesystem::path(STITCHING_PROJECT_ROOT) / modelPath,
            workingDirectory / "build" / modelPath,
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::is_regular_file(candidate)) {
                modelPath = candidate;
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
    this->params["model_path"] = modelPath.string();
    std::cout << "LightGlue model: " << modelPath.string() << '\n';

    // Initialisation du modèle ONNX Runtime
    Ort::SessionOptions session_options;

    // session_options.SetIntraOpNumThreads(0); // Laisser ONNX Runtime décider du nombre de threads intra-opérationnels -> ne fonctionne pas correctement (du moins sur Mac), il utilise moins de 2 coeurs alors que d'autres son dispo.

    // Détection dynamique du matériel dispo.
    unsigned int hardware_cores = std::thread::hardware_concurrency();

    // Si la détection échoue (renvoie 0), on sécurise à 1.
    // Bonne pratique : on garde 1 cœur libre pour le système (OS) afin d'éviter que l'ordi freeze.
    unsigned int num_threads = (hardware_cores > 1) ? (hardware_cores - 1) : 1;

    // Plafonner à 16 : au-delà, les threads passent plus de temps à se synchroniser qu'à calculer (overhead)
    num_threads = std::min(num_threads, 16u);

    session_options.SetIntraOpNumThreads(num_threads);

    // Empêcher les threads de se marcher sur les pieds
    session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

    // Activer l'Arena Allocator (limite l'utilisation du kernel_task)
    session_options.EnableCpuMemArena();

    // Optimisation maximale du graphe
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // Activation de l'accélération matérielle Apple Silicon -> seulement si le code tourne sur une machine Apple
    #ifdef __APPLE__
    // Les exports RACO/ALIKED sont optimisés pour TensorRT; l'Execution
    // Provider CoreML ne supporte pas correctement certains opérateurs
    // dynamiques de ce graphe. SuperPoint/DISK conservent l'accélération CoreML.
    const bool isRacoAlikedModel =
        configuredPath.find("raco_aliked") != std::string::npos;
    if (!isRacoAlikedModel) {
        uint32_t coreml_flags = 0;
        (void)OrtSessionOptionsAppendExecutionProvider_CoreML(
            session_options, coreml_flags);
    }
    #endif
    
    // Instancie un objet Ort::Session (le moteur d'inférence d'ONNX) et l'enveloppe dans un pointeur 'intelligent' (unique_ptr). Assure que la mémoire allouée pour la session est libérée automatiquement et sans fuite de mémoire quand l'objet LightGlueMatcher sera détruit.
    // this->session permet de stocker la session ONNX finalisée dans l'attribut session de la classe. Permet d'y accéder dans la méthode match().
    this->session = std::make_unique<Ort::Session>(this->env, modelPath.c_str(), session_options);

    std::cout << "[DEBUG] Initialisation du LightGlueMatcher : fin\n";
}
  

cv::Mat LightGlueMatcher::preprocessImage(const cv::Mat& image, const std::string& imageName, float& scale, bool grayscale) const {
    std::cout << "[DEBUG] preprocessing de l'image " << imageName << " : début\n";

    // Vérifier que l'image contient bien des données.
    if (image.empty()) {
        throw std::runtime_error("LightGlueMatcher: " + imageName + " est vide.");
    }

    // Ajout d'un redimensionnement (13/10/26)
    // RACO/ALIKED+LightGlue nécessite des dimensions multiples de 32.
    // Cette contrainte reste également valide pour les modèles SuperPoint/DISK.
    int max_edge = 3200;
    scale = 1.0f;
    int max_dim = std::max(image.cols, image.rows);
    cv::Mat resizedImage = image;
    
    if (max_dim > max_edge) {
        scale = static_cast<float>(max_edge) / max_dim;
        const int resized_width =
            std::max(32, static_cast<int>(std::round(image.cols * scale / 32.0f)) * 32);
        const int resized_height =
            std::max(32, static_cast<int>(std::round(image.rows * scale / 32.0f)) * 32);
        scale = static_cast<float>(resized_width) / image.cols;
        cv::resize(image, resizedImage,
                   cv::Size(resized_width, resized_height), 0.0, 0.0,
                   cv::INTER_AREA);
    }

    // Comment fonctionne le redimensionnement :
    // max_dim prend la valeur 5176 (le plus grand côté de l'image).
    // Comme 5176 est strictement supérieur à 1200 (max_edge), le programme entre dans la condition if.
    // Il calcule le nouveau ratio : scale = 1200.0 / 5176.0, ce qui donne environ 0.2318.
    // L'image est redimensionnée à 23% de sa taille d'origine (elle passe de 5176x5176 à 1200x1200) avant de rentrer dans le réseau de neurones.

    std::cout << "[DEBUG] " << imageName 
            << " | Taille originale : " << image.cols << "x" << image.rows 
            << " | Scale : " << scale 
            << " | Nouvelle taille : " << resizedImage.cols << "x" << resizedImage.rows << std::endl;


    // Gestion intelligente des canaux de couleur (le modèle attend une image avec trois canaux BGR.)
    // Pourquoi on applique un grayscale ou non ? Car si on utilise le modèle Superpoint, on a besoin d'une image en niveaux de gris (1 canal), alors que pour les modèles DISK ou ALIKED, on a besoin d'une image en couleur (3 canaux).
    cv::Mat finalImage;
    if (grayscale) {
        // Le modèle exige du Niveaux de Gris (ex: SuperPoint)
        if (resizedImage.channels() == 3) {
            cv::cvtColor(resizedImage, finalImage, cv::COLOR_BGR2GRAY);
        } else if (resizedImage.channels() == 4) {
            cv::cvtColor(resizedImage, finalImage, cv::COLOR_BGRA2GRAY);
        } else {
            finalImage = resizedImage; // Déjà 1 canal
        }
    } else {
        // Le modèle exige de la Couleur RGB (ex: DISK, ALIKED)
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
    return cv::dnn::blobFromImage(finalImage, 1.0 / 255.0, cv::Size(), cv::Scalar(), !grayscale, false);
}

bool LightGlueMatcher::match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) {
    std::cout << "[DEBUG] match LightGlue : début\n";

    // Ajout d'un chronomètre pour mesurer le temps d'exécution de la méthode match() (13/10/26)
    auto start_time = std::chrono::steady_clock::now();

    // Le contexte n'est pas utilisé par le modèle LightGlue.
    (void)ctx;

    // On supprime les anciens résultats si la sortie est réutilisée.
    out.pointsA.clear();
    out.pointsB.clear();
    out.scores.clear();
    out.dx = 0.0f;
    out.dy = 0.0f;

    // Test de la session lors de l'exécution -> si la session n'est pas initialisée, on génère une erreur, sinon on continue.
    if (!this->session) {
        throw std::runtime_error("Erreur : La session ONNX n'est pas initialisée.");
    }

    // Ajout de la détection automatique si le modèle attend du niveaux de gris (1 canal) ou du RGB (3 canaux) (13/10/26)
    bool use_grayscale = false;
    Ort::TypeInfo type_info = this->session->GetInputTypeInfo(0);
    auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
    std::vector<int64_t> input_dims = tensor_info.GetShape();

    // Si la dimension des canaux (index 1) est 1, alors c'est SuperPoint (grayscale)
    if (input_dims.size() >= 2 && input_dims[1] == 1) {
        use_grayscale = true;
        std::cout << "[DEBUG] Modèle " << (use_grayscale ? "Grayscale" : "RGB") << " détecté.\n";
    }

    // Préparer les deux images dans le même format pour le modèle.
    float scale1 = 1.0f, scale2 = 1.0f; // Ajout des ratios de redimensionnement pour chaque image (13/10/26)
    cv::Mat blob1 = preprocessImage(a, "image A", scale1, use_grayscale);
    cv::Mat blob2 = preprocessImage(b, "image B", scale2, use_grayscale);

    // Les modèles RACO/ALIKED+LightGlue exportés récemment utilisent une
    // entrée batch unique et trois sorties: keypoints, matches, mscores.
    // Les modèles SuperPoint/DISK historiques restent traités plus bas avec
    // leurs deux entrées image0/image1 et leurs six sorties.
    if (this->session->GetInputCount() == 1) {
        const auto input_name = this->session->GetInputNameAllocated(
            0, Ort::AllocatorWithDefaultOptions{});
        const std::array<const char*, 3> output_names = {
            "keypoints", "matches", "mscores"
        };

        if (this->session->GetOutputCount() != output_names.size()) {
            throw std::runtime_error(
                "LightGlueMatcher: un modèle batch doit renvoyer keypoints, matches et mscores.");
        }

        std::vector<float> batch_data;
        batch_data.reserve(blob1.total() + blob2.total());
        batch_data.insert(batch_data.end(), blob1.ptr<float>(),
                          blob1.ptr<float>() + blob1.total());
        batch_data.insert(batch_data.end(), blob2.ptr<float>(),
                          blob2.ptr<float>() + blob2.total());

        const std::vector<int64_t> batch_shape = {
            2, blob1.size[1], blob1.size[2], blob1.size[3]
        };
        auto memory_info = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);
        auto input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, batch_data.data(), batch_data.size(),
            batch_shape.data(), batch_shape.size());

        const std::array<const char*, 1> input_names = {input_name.get()};
        auto output_tensors = this->session->Run(
            Ort::RunOptions{nullptr},
            input_names.data(), &input_tensor, 1,
            output_names.data(), output_names.size());

        const auto keypoints_info =
            output_tensors[0].GetTensorTypeAndShapeInfo();
        const auto matches_info =
            output_tensors[1].GetTensorTypeAndShapeInfo();
        const auto scores_info =
            output_tensors[2].GetTensorTypeAndShapeInfo();

        if (keypoints_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            matches_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 ||
            scores_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw std::runtime_error(
                "LightGlueMatcher: types de sorties RACO/ALIKED inattendus.");
        }

        const auto keypoints_shape = keypoints_info.GetShape();
        const auto matches_shape = matches_info.GetShape();
        const auto scores_shape = scores_info.GetShape();
        if (keypoints_shape.size() != 3 || keypoints_shape[0] != 2 ||
            keypoints_shape[2] != 2 || matches_shape.size() != 2 ||
            matches_shape[1] != 3 || scores_shape.size() != 1 ||
            matches_shape[0] != scores_shape[0]) {
            throw std::runtime_error(
                "LightGlueMatcher: formes de sorties RACO/ALIKED inattendues.");
        }

        const size_t keypoint_count = static_cast<size_t>(keypoints_shape[1]);
        const float* keypoints = output_tensors[0].GetTensorData<float>();
        const int64_t* matches = output_tensors[1].GetTensorData<int64_t>();
        const float* scores = output_tensors[2].GetTensorData<float>();

        for (size_t i = 0; i < static_cast<size_t>(matches_shape[0]); ++i) {
            const int64_t batch_index = matches[i * 3];
            const int64_t index_a = matches[i * 3 + 1];
            const int64_t index_b = matches[i * 3 + 2];

            // Pour une paire, le batch entrelacé est [image A, image B].
            if (batch_index != 0) {
                throw std::runtime_error(
                    "LightGlueMatcher: index de batch RACO/ALIKED inattendu.");
            }
            if (index_a < 0 || index_b < 0 ||
                static_cast<size_t>(index_a) >= keypoint_count ||
                static_cast<size_t>(index_b) >= keypoint_count) {
                throw std::runtime_error(
                    "LightGlueMatcher: indice de keypoint RACO/ALIKED hors limites.");
            }
            if (scores[i] < kMinConfidenceScore) {
                continue;
            }

            const size_t point_a = static_cast<size_t>(index_a) * 2;
            const size_t point_b = (keypoint_count +
                                    static_cast<size_t>(index_b)) * 2;
            out.pointsA.emplace_back(
                keypoints[point_a] / scale1, keypoints[point_a + 1] / scale1);
            out.pointsB.emplace_back(
                keypoints[point_b] / scale2, keypoints[point_b + 1] / scale2);
            out.scores.push_back(scores[i]);
        }

        if (!out.pointsA.empty()) {
            std::vector<float> dx_list, dy_list;
            for (size_t i = 0; i < out.pointsA.size(); ++i) {
                dx_list.push_back(out.pointsA[i].x - out.pointsB[i].x);
                dy_list.push_back(out.pointsA[i].y - out.pointsB[i].y);
            }
            const auto median = [](std::vector<float>& values) {
                const size_t mid = values.size() / 2;
                std::nth_element(values.begin(), values.begin() + mid, values.end());
                if (values.size() % 2 == 0) {
                    const float upper = values[mid];
                    std::nth_element(values.begin(), values.begin() + mid - 1, values.end());
                    return (values[mid - 1] + upper) / 2.0f;
                }
                return values[mid];
            };
            out.dx = median(dx_list);
            out.dy = median(dy_list);
        }

        std::cout << "[DEBUG] Modèle RACO/ALIKED+LightGlue exécuté\n";
        const auto end_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;
        std::cout << "[DEBUG] Temps total exécution match() : "
                  << elapsed.count() << " secondes\n";
        return true;
    }

    if (this->session->GetInputCount() != 2) {
        throw std::runtime_error(
            "LightGlueMatcher: nombre d'entrées ONNX non supporté.");
    }

    // Lier la mémoire de ce blob à des tenseurs ONNX Runtime
    // Définir la forme (Shape) : [1, Canaux, Hauteur, Largeur]
    std::vector<int64_t> input_shape1 = {1, blob1.size[1], blob1.size[2], blob1.size[3]};
    std::vector<int64_t> input_shape2 = {1, blob2.size[1], blob2.size[2], blob2.size[3]};

    // Gère la mémoire RAM, nécessaire pour l'étape suivante (pour créer les tenseurs ONNX Runtime à partir des blobs OpenCV)
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // Créer le tenseur pour l'image 1
    Ort::Value tensor1 = Ort::Value::CreateTensor<float>(
        memory_info,
        blob1.ptr<float>(), blob1.total(),
        input_shape1.data(), input_shape1.size()
    );

    // Créer le tenseur pour l'image 2 (idem)
    Ort::Value tensor2 = Ort::Value::CreateTensor<float>(
        memory_info,
        blob2.ptr<float>(), blob2.total(),
        input_shape2.data(), input_shape2.size()
    );

    // Inférence

    // Noms des nœuds d'entrée/sortie (à adapter selon l'export ONNX -> j'ai vérifié via Netron)
    const char* input_names[] = {"image0", "image1"}; 
    const char* output_names[] = {"matches0", "matches1", "mscores0", "mscores1", "kpts0", "kpts1"};  // -> 6 outputs

    std::vector<Ort::Value> inputs;
    inputs.push_back(std::move(tensor1));
    inputs.push_back(std::move(tensor2));

    std::cout << "[DEBUG] match LightGlue : exécution du modèle\n";

    // Exécution du modèle
    auto output_tensors = this->session->Run(
        Ort::RunOptions{nullptr},
        input_names, inputs.data(), inputs.size(),
        output_names, 6 // Nombre d'outputs attendus
    );

    // On vérifie que le modèle a renvoyé toutes les sorties attendues.
    if (output_tensors.size() != 6) {
        throw std::runtime_error("LightGlueMatcher: le modèle doit renvoyer exactement 6 tenseurs.");
    }
    std::cout << "[DEBUG] match LightGlue : traitement des résultats\n";

    // Traitement des résultats obtenus

    // Notes sur les sorties :
    //  - output_tensors contient 6 tenseurs qui correspondent à : matches0, matches1, mscores0, mscores1, kpts0, kpts1 ;
    //  - output_tensors[0] (matches0) contient un tableau d'entiers (int64_t) représentant les indices des correspondances pour l'image 0 (matches0), à l'index i de ce tableau, on trouve l'index du point correspondant dans l'image 1 -> [-1, 12, 6, -1, 87] ;
    //  - output_tensors[1] (matches1) -> [-1, -1, 75, -1, 26] ;
    //  - output_tensors[2] (mscores0) contient les scores de confiance calculés par le réseau pour chaque paire de points -> [6.2772e-17, 0.999883, 0.999937, 0, 0.999909] ;
    //  - output_tensors[3] (mscores1) -> [0, 0, 0.997286, 0, 0.996805] ;
    //  - output_tensors[4] (kpts0) contient les coordonnées (x, y) de tous les points détectés dans l'image 0 -> [224, 229, 154, 32, 155] ;
    // - output_tensors[5] (kpts1) contient les coordonnées (x, y) de tous les points détectés dans l'image 1 -> [198, 229, 170, 228, 49].

    // Extraction des informations pour output_tensors[0] (matches0)
    auto matches0_info = output_tensors[0].GetTensorTypeAndShapeInfo();
    // Les indices de correspondance doivent être des entiers sur 64 bits.
    if (matches0_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        throw std::runtime_error("LightGlueMatcher: matches0 doit être un tenseur INT64.");
    }
    size_t num_elements = matches0_info.GetElementCount();
    const auto matches1_info = output_tensors[1].GetTensorTypeAndShapeInfo();
    // On vérifie aussi le type de la deuxième liste d'indices.
    if (matches1_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        throw std::runtime_error("LightGlueMatcher: matches1 doit être un tenseur INT64.");
    }

    // LightGlue renvoie des indices sous forme d'entiers 64 bits (int64_t)
    int64_t* matches0_data = output_tensors[0].GetTensorMutableData<int64_t>();

    auto scores0_info = output_tensors[2].GetTensorTypeAndShapeInfo();
    // Il faut un score FLOAT pour chaque élément de matches0.
    if (scores0_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        scores0_info.GetElementCount() != num_elements) {
        throw std::runtime_error(
            "LightGlueMatcher: mscores0 doit être un tenseur FLOAT contenant un score par point de matches0.");
    }
    float* scores0_data = output_tensors[2].GetTensorMutableData<float>();
    const auto scores1_info = output_tensors[3].GetTensorTypeAndShapeInfo();
    // Cette sortie n'est pas utilisée, mais son type doit rester compatible.
    if (scores1_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        throw std::runtime_error("LightGlueMatcher: mscores1 doit être un tenseur FLOAT.");
    }

    // Pourquoi on extrait que les données de scores0 et non également de scores1 ? 
    // IA : Quand LightGlue trouve une correspondance valide entre un Point A (dans img1) et un Point B (dans img2), il s'agit d'une seule et même paire. scores0[i] contient le score de confiance pour le Point A (qui a matché avec le Point B). scores1[match_idx] contient le score de confiance pour le Point B (qui a matché avec le Point A). Pour une paire validée, ces deux scores sont identiques (ou représentent exactement la même information de confiance pour cette liaison).

    // Extraction des coordonnées des points-clés (float)
    auto kpts0_info = output_tensors[4].GetTensorTypeAndShapeInfo();
    auto kpts1_info = output_tensors[5].GetTensorTypeAndShapeInfo();
    auto kpts0_type = kpts0_info.GetElementType();
    // Les deux listes de points doivent avoir le même type.
    if (kpts0_type != kpts1_info.GetElementType() ||
        (kpts0_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
         kpts0_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)) {
        throw std::runtime_error(
            "LightGlueMatcher: kpts0 et kpts1 doivent avoir le même type FLOAT ou INT64.");
    }

    const size_t kpts0_elements = kpts0_info.GetElementCount();
    const size_t kpts1_elements = kpts1_info.GetElementCount();
    // Chaque point contient deux valeurs : x et y.
    if (kpts0_elements % 2 != 0 || kpts1_elements % 2 != 0 ||
        kpts0_elements / 2 < num_elements) {
        throw std::runtime_error(
            "LightGlueMatcher: les dimensions des tenseurs kpts0/kpts1 sont incompatibles avec matches0.");
    }
    const size_t kpts1_count = kpts1_elements / 2;
    
    float* kpts0_f = nullptr; float* kpts1_f = nullptr;
    int64_t* kpts0_i = nullptr; int64_t* kpts1_i = nullptr;

    if (kpts0_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        kpts0_f = output_tensors[4].GetTensorMutableData<float>();
        kpts1_f = output_tensors[5].GetTensorMutableData<float>();
    } else if (kpts0_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        kpts0_i = output_tensors[4].GetTensorMutableData<int64_t>();
        kpts1_i = output_tensors[5].GetTensorMutableData<int64_t>();
    } else {
        throw std::runtime_error("Le type des tenseurs kpts0/kpts1 n'est ni float ni int64.");
    }

    // On associe maintenant toutes les coordonnées :
    //  - match_idx > -1 : Confirme que le point i de img1 a bien été matché.
    //  - Extraction $(x, y)$  : Les données des points sont stockées à plat en mémoire (ex: x0, y0, x1, y1...). Pour obtenir le point i, on lit les indices i * 2 (pour X) et i * 2 + 1 (pour Y). On fait la même chose pour img2 en utilisant match_idx.
    //  - Stockage : Les coordonnées sont converties en objets cv::Point2f et poussées dans out.pointsA et out.pointsB. À ce stade, pointsA[k] et pointsB[k] représentent les coordonnées exactes du k-ème match valide.

    std::cout << "[DEBUG] match LightGlue : association des coordonnées\n";
    
    for (size_t i = 0; i < num_elements; ++i) {
        int64_t match_idx = matches0_data[i];
        
        if (match_idx >= 0 && static_cast<uint64_t>(match_idx) < kpts1_count) {
            const float confidence_score = scores0_data[i];
            if (confidence_score < kMinConfidenceScore) {
                continue;
            }

            float x0, y0, x1, y1;
            
            // On lit les coordonnées seulement après avoir vérifié l'indice.
            if (kpts0_f) {
                x0 = kpts0_f[i * 2];
                y0 = kpts0_f[i * 2 + 1];
                x1 = kpts1_f[match_idx * 2];
                y1 = kpts1_f[match_idx * 2 + 1];
            } else {
                x0 = static_cast<float>(kpts0_i[i * 2]);
                y0 = static_cast<float>(kpts0_i[i * 2 + 1]);
                x1 = static_cast<float>(kpts1_i[match_idx * 2]);
                y1 = static_cast<float>(kpts1_i[match_idx * 2 + 1]);
            }
            
            // On enregistre les points correspondants dans les vecteurs de la structure de sortie
            // Ajout de la division par le scale pour replacer les points sur les images 5176x5176 (13/10/26)
            out.pointsA.push_back(cv::Point2f(x0 / scale1, y0 / scale1));
            out.pointsB.push_back(cv::Point2f(x1 / scale2, y1 / scale2));

            // On enregistre le score correspondant
            out.scores.push_back(confidence_score);
        } else if (match_idx >= 0) {
            throw std::runtime_error(
                "LightGlueMatcher: matches0 contient un indice hors limites dans kpts1.");
        }
    }

    // Les trois vecteurs doivent toujours contenir le même nombre de résultats.
    if (out.pointsA.size() != out.pointsB.size() ||
        out.pointsA.size() != out.scores.size()) {
        throw std::runtime_error("LightGlueMatcher: les vecteurs de sortie ont des tailles incohérentes.");
    }

    std::cout << "[DEBUG] match LightGlue : calcul de dx et dy\n";

    // Calcul du décalage (dx, dy) et enregistrement dans la structure de sortie.
    if (!out.pointsA.empty()) {
        std::vector<float> dx_list;
        std::vector<float> dy_list;
        
        // Calculer la différence pour chaque paire de points
        for (size_t i = 0; i < out.pointsA.size(); ++i) {
            dx_list.push_back(out.pointsA[i].x - out.pointsB[i].x);
            dy_list.push_back(out.pointsA[i].y - out.pointsB[i].y);
        }

        // La médiane limite l'effet des mauvaises correspondances.
        const auto median = [](std::vector<float>& values) {
            const size_t mid_idx = values.size() / 2;
            std::nth_element(values.begin(), values.begin() + mid_idx, values.end());
            if (values.size() % 2 == 0) {
                const float upper = values[mid_idx];
                std::nth_element(values.begin(), values.begin() + mid_idx - 1, values.end());
                return (values[mid_idx - 1] + upper) / 2.0f;
            }
            return values[mid_idx];
        };

        out.dx = median(dx_list);
        out.dy = median(dy_list);

        std::cout << "Dx : " << out.dx << ", Dy : " << out.dy << '\n';
    }

    // std::cout << "[DEBUG] match LightGlue : fin\n";

    auto end_time = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    std::cout << "[DEBUG] Temps total exécution match() : " << elapsed.count() << " secondes\n";

    return true;
}

} // namespace stitching::matching
