#include "algorithms/matching/SiftMatcherAlgorithm.h"
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <stdexcept>

namespace stitching::matching {

bool runSiftMatching(const cv::Mat& a, const cv::Mat& b,    //& = on ne copie pas 'image en memeoire et const = on ne la modifie pas
                     const PairContext& ctx, const Json& params, Matches& out) {  //un & mais pas de const. C'est parce que c'est notre variable de sortie. On va la remplir avec nos résultats.
    out.pointsA.clear();
    out.pointsB.clear();
    out.scores.clear();
    out.dx = 0.f;
    out.dy = 0.f;   // avant de commencer : vide les listes et on remet dx dy à 0 au cas ou out contiendrait les vieux resultats

    if (a.empty() || b.empty()) return false; // si image vide on arrete



    // PARAMETRES
    const int maxFeatures = params.value("max_features", 500); //Va chercher la valeur dans le JSON. S'il n'y a rien, il prend 500 par défaut (a changer)
    const float ratio = params.value("lowe_ratio", 0.75f);
    const double contrastThreshold = params.value("contrast_threshold", 0.01);
    const double claheClipLimit = params.value("clahe_clip_limit", 2.0);
    if (maxFeatures <= 0 || ratio <= 0.f || ratio >= 1.f
        || contrastThreshold <= 0.0 || claheClipLimit < 0.0) {
        throw std::invalid_argument(
            "SIFT parameters must have max_features > 0, lowe_ratio between 0 and 1, "
            "contrast_threshold > 0, and clahe_clip_limit >= 0");
    }


    // COnversion en niveaux de gris 
    const auto toGray = [](const cv::Mat& image) { //fonction lambda en C++. C'est une mini-fonction jetable créée à l'intérieur de fonction principale.
        cv::Mat gray;
        switch (image.channels()) {  //regarde combien de canaux a l'image (1 pour gris, 3 pour couleur classique BGR, 4 pour BGR avec transparence) 
                                     //et applique la bonne conversion OpenCV (cv::cvtColor)
        case 1:
            gray = image;
            break;
        case 3:
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
            break;
        case 4:
            cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
            break;
        default:
            throw std::invalid_argument("SIFT expects 1-, 3-, or 4-channel images");
        }
        return gray;
    };

    const cv::Mat grayA = toGray(a);// utilisation de la fonction pour les imaages a et b
    const cv::Mat grayB = toGray(b);


    // Decoupage de la zone (ROI)
    cv::Rect roiA(0, 0, grayA.cols, grayA.rows); //cv::Rect : Définit un rectangle (origine X, origine Y, largeur, hauteur). Par défaut, il prend toute l'image
    cv::Rect roiB(0, 0, grayB.cols, grayB.rows);
    cv::Point2f offsetA(0.f, 0.f);
    cv::Point2f offsetB(0.f, 0.f);

    const float overlap = std::clamp(ctx.overlap, 0.f, 1.f);  //std::clamp S'assure que la valeur de overlap reste strictement bloquée entre 0.0 et 1.0.
    if (overlap > 0.f && ctx.dir == Direction::LeftRight) {
        const int width = std::min({  //std::min : Calcule la largeur de la zone de superposition, en s'assurant qu'elle ne dépasse ni la taille de l'image A, ni celle de la B.
            static_cast<int>(grayB.cols * overlap), grayA.cols, grayB.cols
        });

        //ce bloc découpe virtuellement le bord droit de A et le bord gauche de B, et sauvegarde le décalage (offsetA, offsetB) pour pouvoir retrouver les coordonnées globales à la fin.
        if (width > 0) {
            roiA = cv::Rect(grayA.cols - width, 0, width, grayA.rows);
            roiB = cv::Rect(0, 0, width, grayB.rows);
            offsetA.x = static_cast<float>(grayA.cols - width);
        }
    } else if (overlap > 0.f) {
        const int height = std::min({
            static_cast<int>(grayB.rows * overlap), grayA.rows, grayB.rows
        });
        if (height > 0) {
            roiA = cv::Rect(0, grayA.rows - height, grayA.cols, height);
            roiB = cv::Rect(0, 0, grayB.cols, height);
            offsetA.y = static_cast<float>(grayA.rows - height);
        }
    }



    const cv::Mat cropA = grayA(roiA);
    const cv::Mat cropB = grayB(roiB);

    cv::Mat enhancedA, enhancedB;
    if (claheClipLimit > 0.0) { //cv::createCLAHE : Si demandé, on crée un algorithme qui va booster les micro-contrastes (les ombres/lumières) pour aider SIFT à voir des détails.
        const auto clahe = cv::createCLAHE(claheClipLimit);
        clahe->apply(cropA, enhancedA);
        clahe->apply(cropB, enhancedB);
    } else {
        enhancedA = cropA;
        enhancedB = cropB;
    }

    const auto sift = cv::SIFT::create(
        maxFeatures, 3, contrastThreshold, 10.0, 1.6);
    std::vector<cv::KeyPoint> keypointsA, keypointsB;
    cv::Mat descriptorsA, descriptorsB;

    //sift->detectAndCompute : On lance SIFT. cv::noArray() signifie qu'on n'utilise pas de masque restrictif. OpenCV va remplir keypointsA (les positions X,Y des points) et descriptorsA (l'empreinte mathématique de chaque point).
    sift->detectAndCompute(enhancedA, cv::noArray(), keypointsA, descriptorsA);
    sift->detectAndCompute(enhancedB, cv::noArray(), keypointsB, descriptorsB);
    if (descriptorsA.empty() || descriptorsB.empty()
        || descriptorsA.rows < 2 || descriptorsB.rows < 2) {
        return false;
    }


    //BFMatcher : Brute Force Matcher. Il va bêtement comparer chaque point de B avec tous les points de A pour trouver ceux qui se ressemblent le plus.
    // knnMatch(..., 2) : Il garde les 2 meilleurs candidats de A pour chaque point de B.
    cv::BFMatcher matcher(cv::NORM_L2); //Il faudra peut etre changer ca par FLANN pour optimiser ??
    std::vector<std::vector<cv::DMatch>> nearest;
    matcher.knnMatch(descriptorsB, descriptorsA, nearest, 2);



    for (const auto& candidates : nearest) { //Le filtre de Lowe (candidates[0].distance >= ratio * candidates[1]...) : C'est le tri sévère. Si le meilleur match (le n°0) ne se démarque pas assez du deuxième meilleur (le n°1), 
                                             //on le rejette car il est ambigu (continue passe au point suivant sans l'enregistrer).
        if (candidates.size() < 2
            || candidates[0].distance >= ratio * candidates[1].distance) {
            continue;
        }

        const cv::DMatch& match = candidates[0];
        out.pointsA.push_back(keypointsA[match.trainIdx].pt + offsetA); //ajout des coordonees des pts valides dans listes de sorties
        out.pointsB.push_back(keypointsB[match.queryIdx].pt + offsetB); //+offsetA : On réajoute le décalage qu'on avait enlevé lors du découpage (ROI) pour que les coordonnées redeviennent celles de l'image globale.
        out.scores.push_back(1.f - match.distance / candidates[1].distance); //1.f - ... : On transforme la distance d'erreur (où 0 est parfait) en un score de confiance (où 1 est parfait) 1.f précise a C++ que c'est un float
    }

    if (out.pointsA.size() < 4) { //SI moins que 4 points trouvés, on efface tout et on retourne faux car pas assez de points d'interets
        out.pointsA.clear();
        out.pointsB.clear();
        out.scores.clear();
        return false;
    }



    std::vector<float> translationsX, translationsY;
    translationsX.reserve(out.pointsA.size()); //.reserve() : optimisation C++. Au lieu que la liste demande de la nouvelle mémoire à l'ordinateur à chaque point ajouté, on réserve d'un coup la taille exacte nécessaire.
    translationsY.reserve(out.pointsA.size());
    for (size_t i = 0; i < out.pointsA.size(); ++i) {
        translationsX.push_back(out.pointsA[i].x - out.pointsB[i].x);
        translationsY.push_back(out.pointsA[i].y - out.pointsB[i].y);
    }


    // std::nth_element : Une fonction ultra-rapide de la librairie standard C++. Plutôt que de trier complètement la liste (ce qui prend du temps), elle réorganise juste les données pour placer la médiane (la valeur du milieu)
    //  à sa position exacte, et met tout ce qui est plus petit à gauche, et tout ce qui est plus grand à droite.
    //On l'applique sur X et Y, on enregistre dans out.dx et out.dy, et on renvoie true pour signaler que le module a terminé son travail avec succès.
    const auto median = [](std::vector<float>& values) {
        const auto middle = values.begin() + values.size() / 2;
        std::nth_element(values.begin(), middle, values.end());
        return *middle;
    };
    out.dx = median(translationsX);
    out.dy = median(translationsY);
    return true;
}

} // namespace stitching::matching





// 06/10 :
// Ce que contient out
// out comporte deux listes de points :

// out.pointsA[i] : un point dans l’image A ;
// out.pointsB[i] : le point correspondant dans l’image B.
// Les deux points de même indice forment une correspondance. 
// Par exemple, pointsA[0] correspond à pointsB[0]. Ce sont ces paires que 
// l’étape suivante transmet au rejet des mauvaises correspondances, notamment à RANSAC.
// Comment les correspondances sont obtenues
// Le matcher :

// Convertit les images en niveaux de gris si nécessaire.
// Limite la recherche à la zone de recouvrement estimée, selon ctx.dir et ctx.overlap.
// Détecte les points et descripteurs SIFT.
// Compare les descripteurs avec le plus proche voisin et le filtre de Lowe.
// Ajoute dans out les paires qui passent ce filtre.
// La méthode renvoie true si elle obtient au moins quatre correspondances. Sinon, elle 
// renvoie false ; si elle en a trouvé moins de quatre, les listes sont vidées.

// Les réglages par défaut sont 500 points caractéristiques maximum et un ratio de 
// Lowe de 0,75. On peut les fournir au constructeur via le JSON, 
// par exemple {"max_features": 800, "lowe_ratio": 0.7}.




// Ajout le 07/10 :
// scores[i] est le score de confiance de la paire pointsA[i] / pointsB[i], 
// calculé à partir du ratio de Lowe. Il varie de 0 à 1 ; plus il est élevé, 
// plus le meilleur descripteur se distingue du suivant.
// dx et dy sont les médianes des différences pointA - pointB, 
// donc une estimation de la translation de B vers A.

// L'ia : J’ai ajouté ces champs à Types.h, leur calcul dans SiftMatcher.cpp, 
// et leur inclusion dans les données JSON de l’étape de stitching dans StitchingStep.cpp. 
// Ils apparaîtront donc aussi dans le journal du pipeline.