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
  

bool LightGlueMatcher::match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) {
    // Test de la session lors de l'exécution -> si la session n'est pas initialisée, on génère une erreur, sinon on continue.
    if (!this->session) {
        throw std::runtime_error("Erreur : La session ONNX n'est pas initialisée.");
    }

    // Vérification que les images ne sont pas vides avant le prétraitement
    if (a.empty() || b.empty()) {
        throw std::runtime_error("LightGlueMatcher: a ou b est vide avant le prétraitement.");
    }

    cv::Mat colorImg1 = a;
    cv::Mat colorImg2 = b;

    // Nettoyage des canaux pour a
    if (a.channels() == 1) {
        cv::cvtColor(a, colorImg1, cv::COLOR_GRAY2BGR);
    } else if (a.channels() == 4) {
        cv::cvtColor(a, colorImg1, cv::COLOR_BGRA2BGR);
    }

    // Nettoyage des canaux pour b
    if (b.channels() == 1) {
        cv::cvtColor(b, colorImg2, cv::COLOR_GRAY2BGR);
    } else if (b.channels() == 4) {
        cv::cvtColor(b, colorImg2, cv::COLOR_BGRA2BGR);
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
    auto matches0_shape = matches0_info.GetShape();
    size_t num_elements = matches0_info.GetElementCount();

    // LightGlue renvoie des indices sous forme d'entiers 64 bits (int64_t)
    int64_t* matches0_data = output_tensors[0].GetTensorMutableData<int64_t>();

    auto scores0_info = output_tensors[2].GetTensorTypeAndShapeInfo();
    float* scores0_data = output_tensors[2].GetTensorMutableData<float>();

    // Pourquoi on extrait que les données de scores0 et non également de scores1 ? 
    // IA : Quand LightGlue trouve une correspondance valide entre un Point A (dans img1) et un Point B (dans img2), il s'agit d'une seule et même paire. scores0[i] contient le score de confiance pour le Point A (qui a matché avec le Point B). scores1[match_idx] contient le score de confiance pour le Point B (qui a matché avec le Point A). Pour une paire validée, ces deux scores sont identiques (ou représentent exactement la même information de confiance pour cette liaison).

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

    // On associe maintenant toutes les coordonnées :
    //  - match_idx > -1 : Confirme que le point i de img1 a bien été matché.
    //  - Extraction $(x, y)$  : Les données des points sont stockées à plat en mémoire (ex: x0, y0, x1, y1...). Pour obtenir le point i, on lit les indices i * 2 (pour X) et i * 2 + 1 (pour Y). On fait la même chose pour img2 en utilisant match_idx.
    //  - Stockage : Les coordonnées sont converties en objets cv::Point2f et poussées dans out.pointsA et out.pointsB. À ce stade, pointsA[k] et pointsB[k] représentent les coordonnées exactes du k-ème match valide.
    
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
            
            // On enregistre les points correspondants dans les vecteurs de la structure de sortie
            out.pointsA.push_back(cv::Point2f(x0, y0));
            out.pointsB.push_back(cv::Point2f(x1, y1));

            // On enregistre le score correspondant
            out.scores.push_back(scores0_data[i]);
        }
    }

    // Calcul du décalage (dx, dy) et enregistrement dans la stucture de sortie 
    if (!out.pointsA.empty()) {
        std::vector<float> dx_list;
        std::vector<float> dy_list;
        
        // Calculer la différence pour chaque paire de points
        for (size_t i = 0; i < out.pointsA.size(); ++i) {
            dx_list.push_back(out.pointsB[i].x - out.pointsA[i].x);
            dy_list.push_back(out.pointsB[i].y - out.pointsA[i].y);
        }

        // Trouver la médiane
        size_t mid_idx = dx_list.size() / 2;
        std::nth_element(dx_list.begin(), dx_list.begin() + mid_idx, dx_list.end());
        std::nth_element(dy_list.begin(), dy_list.begin() + mid_idx, dy_list.end());
        
        // Sauvegarder dans la structure 'out'
        out.dx = dx_list[mid_idx];
        out.dy = dy_list[mid_idx];
    }

    return true;
}

} // namespace stitching::matching
