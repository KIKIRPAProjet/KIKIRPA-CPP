#include "algorithms/matching/LightGlueMatcher.h"
#include <onnxruntime_cxx_api.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <coreml_provider_factory.h>

#ifndef STITCHING_PROJECT_ROOT
#define STITCHING_PROJECT_ROOT "."
#endif

namespace stitching::matching {

// Initialisation du matcher LightGlue
// Cherche le fichier du modèle ONNX sélectionné et initialise la session ONNX
LightGlueMatcher::LightGlueMatcher(const Json& params) : params(params) {
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
    session_options.SetIntraOpNumThreads(0); // Laisser ONNX Runtime décider du nombre de threads intra-opérationnels
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL); // Activer toutes les optimisations du graphe pour de meilleures performances

    // Activation de l'accélération matérielle Apple Silicon
    uint32_t coreml_flags = 0;
    Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(session_options, coreml_flags));
    
    // Instancie un objet Ort::Session (le moteur d'inférence d'ONNX) et l'enveloppe dans un pointeur 'intelligent' (unique_ptr). Assure que la mémoire allouée pour la session est libérée automatiquement et sans fuite de mémoire quand l'objet LightGlueMatcher sera détruit.
    // this->session permet de stocker la session ONNX finalisée dans l'attribut session de la classe. Permet d'y accéder dans la méthode match().
    this->session = std::make_unique<Ort::Session>(this->env, modelPath.c_str(), session_options);
}
  

bool LightGlueMatcher::match(const cv::Mat& img1, const cv::Mat& img2, const PairContext&, Matches&) {
    // Test de la session lors de l'exécution -> si la session n'est pas initialisée, on génère une erreur, sinon on continue.
    if (!this->session) {
        throw std::runtime_error("Erreur : La session ONNX n'est pas initialisée.");
    }

    // Vérification que les images ne sont pas vides avant le prétraitement
    if (img1.empty() || img2.empty()) {
        throw std::runtime_error("LightGlueMatcher: img1 ou img2 est vide avant le prétraitement.");
    }

    cv::Mat colorImg1 = img1;
    cv::Mat colorImg2 = img2;

    // Nettoyage des canaux pour img1
    if (img1.channels() == 1) {
        cv::cvtColor(img1, colorImg1, cv::COLOR_GRAY2BGR);
    } else if (img1.channels() == 4) {
        cv::cvtColor(img1, colorImg1, cv::COLOR_BGRA2BGR);
    }

    // Nettoyage des canaux pour img2
    if (img2.channels() == 1) {
        cv::cvtColor(img2, colorImg2, cv::COLOR_GRAY2BGR);
    } else if (img2.channels() == 4) {
        cv::cvtColor(img2, colorImg2, cv::COLOR_BGRA2BGR);
    }

    // Prétraitement des images
    // On ne peut pas donner les cv::Mat directement au modèle. On doit les convertir en tenseurs, généralement au format flottant float32 et réorganisés

    // Conversion de HWC (hauteur, largeur, canaux) en BGR à NCHW (batch, canaux, hauteur, largeur) en RGB avec des valeurs normalisées entre 0 et 1
    cv::Mat blob1 = cv::dnn::blobFromImage(colorImg1, 1.0 / 255.0, cv::Size(), cv::Scalar(), true, false);
    cv::Mat blob2 = cv::dnn::blobFromImage(colorImg2, 1.0 / 255.0, cv::Size(), cv::Scalar(), true, false);

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

    // Exécution du modèle
    auto output_tensors = this->session->Run(
        Ort::RunOptions{nullptr},
        input_names, inputs.data(), inputs.size(),
        output_names, 6 // Nombre d'outputs attendus
    );

    // PRINT DES RÉSULTATS

    // Extraction des informations pour output_tensors[0] (matches0)
    auto matches0_info = output_tensors[0].GetTensorTypeAndShapeInfo();
    auto matches0_shape = matches0_info.GetShape();
    size_t num_elements = matches0_info.GetElementCount();

    // LightGlue renvoie des indices sous forme d'entiers 64 bits (int64_t)
    int64_t* matches0_data = output_tensors[0].GetTensorMutableData<int64_t>();

    // Affichage de la dimension (Shape)
    std::cout << "Shape matches0 : [";
    for (size_t i = 0; i < matches0_shape.size(); ++i) {
        std::cout << matches0_shape[i] << (i == matches0_shape.size() - 1 ? "" : ", ");
    }
    std::cout << "]\n";

    // Affichage d'un échantillon (limité à 10 pour ne pas saturer la console)
    std::cout << "Aperçu des 10 premières valeurs de matches0 : ";
    size_t limit = std::min<size_t>(10, num_elements);
    for (size_t i = 0; i < limit; ++i) {
        std::cout << matches0_data[i] << " ";
    }
    std::cout << "\n";

    // Pour mscores0 (index 2), le type est généralement float
    auto scores0_info = output_tensors[2].GetTensorTypeAndShapeInfo();
    float* scores0_data = output_tensors[2].GetTensorMutableData<float>();
    
    std::cout << "Aperçu des 5 premiers scores : ";
    size_t limit_scores = std::min<size_t>(5, scores0_info.GetElementCount());
    for (size_t i = 0; i < limit_scores; ++i) {
        std::cout << scores0_data[i] << " ";
    }
    std::cout << "\n";

    // Extraction des coordonnées des points-clés (float)
    auto kpts0_type = output_tensors[4].GetTensorTypeAndShapeInfo().GetElementType();
    
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

    std::cout << "--- Liste des correspondances valides ---\n";
    
    int valid_matches_count = 0;
    
    for (size_t i = 0; i < num_elements; ++i) {
        int64_t match_idx = matches0_data[i];
        
        if (match_idx > -1) {
            float x0, y0, x1, y1;
            
            // Extraction sécurisée
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
            
            float score = scores0_data[i];
            
            std::cout << "Match " << valid_matches_count << " : "
                      << "Img1(" << x0 << ", " << y0 << ") -> "
                      << "Img2(" << x1 << ", " << y1 << ") "
                      << "[Score: " << score << "]\n";
                      
            valid_matches_count++;
        }
    }
    
    // size_t num_input_nodes = this->session->GetInputCount();
    // std::cout << "[Test ONNX] Modèle actif. Nombre de tenseurs d'entrée attendus : " << num_input_nodes << "\n";

    return true;
}

} // namespace stitching::matching
