#include "algorithms/stitching/sift/SiftMatcherAlgorithm.h"
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <stdexcept>

namespace stitching::matching {

bool runSiftMatching(const cv::Mat& a, const cv::Mat& b,    //& = on ne copie pas 'image en memeoire et const = on ne la modifie pas
                     const PairContext& ctx, const Json& params, Matches& out) {  //un & mais pas de const. C'est parce que c'est notre variable de sortie. On va la remplir avec nos résultats.
    out.pointsInA.clear();
    out.pointsInB.clear();
    out.confidenceScores.clear();
    out.translationX = 0.f;
    out.translationY = 0.f;   // avant de commencer : vide les listes et on remet dx dy à 0 au cas ou out contiendrait les vieux resultats

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


    // Conversion en niveaux de gris, SANS PERTE : tout est ramene en CV_16UC1 (8 bits -> x257, 16 bits inchange).
    // Les images attendues sont des tuiles .tif 16 bits lues par ImagePretreatment::read16.
    const auto toGray = [](const cv::Mat& image) {
        cv::Mat img16;
        switch (image.depth()) {
        case CV_16U:
            img16 = image;
            break;
        case CV_8U:
            image.convertTo(img16, CV_MAKETYPE(CV_16U, image.channels()), 257.0);
            break;
        default:
            throw std::invalid_argument("SIFT expects 8- or 16-bit integer images");
        }

        cv::Mat gray;
        switch (img16.channels()) {
        case 1:
            gray = img16;
            break;
        case 3:
            cv::cvtColor(img16, gray, cv::COLOR_BGR2GRAY);
            break;
        case 4:
            cv::cvtColor(img16, gray, cv::COLOR_BGRA2GRAY);
            break;
        default:
            throw std::invalid_argument("SIFT expects 1-, 3-, or 4-channel images");
        }
        return gray;
    };

    const cv::Mat grayA = toGray(a);// utilisation de la fonction pour les imaages a et b
    const cv::Mat grayB = toGray(b);


    // Les images transmises sont déjà les bandes d'overlap prédécoupées
    (void)ctx;
    const cv::Mat& cropA = grayA;
    const cv::Mat& cropB = grayB;

    cv::Mat enhancedA, enhancedB;
    if (claheClipLimit > 0.0) { //cv::createCLAHE : Si demandé, on crée un algorithme qui va booster les micro-contrastes (les ombres/lumières) pour aider SIFT à voir des détails.
        const auto clahe = cv::createCLAHE(claheClipLimit);
        clahe->apply(cropA, enhancedA);
        clahe->apply(cropB, enhancedB);
    } else {
        enhancedA = cropA;
        enhancedB = cropB;
    }

    // cv::SIFT (OpenCV) n'accepte QUE du CV_8U en entree : c'est la SEULE etape ou l'on quantifie, tout en fin de chaine.
    // La lecture, le niveau de gris et le CLAHE ci-dessus restent en 16 bits. On étire la dynamique réelle conjointe
    // vers [0, 255] avec les mêmes coefficients pour A et B afin de préserver la comparabilité des descripteurs,
    // sans écraser les images 16 bits sombres ou infrarouges (Apollo).
    cv::Mat inputA, inputB;
    double minA = 0.0, maxA = 0.0, minB = 0.0, maxB = 0.0;
    cv::minMaxLoc(enhancedA, &minA, &maxA);
    cv::minMaxLoc(enhancedB, &minB, &maxB);
    const double minAB = std::min(minA, minB), span = std::max(maxA, maxB) - minAB;
    const double alpha = (span > 0.0) ? (255.0 / span) : (255.0 / 65535.0);
    enhancedA.convertTo(inputA, CV_8U, alpha, (span > 0.0) ? (-minAB * alpha) : 0.0);
    enhancedB.convertTo(inputB, CV_8U, alpha, (span > 0.0) ? (-minAB * alpha) : 0.0);

    const auto sift = cv::SIFT::create(
        maxFeatures, 3, contrastThreshold, 10.0, 1.6);
    std::vector<cv::KeyPoint> keypointsA, keypointsB;
    cv::Mat descriptorsA, descriptorsB;

    //sift->detectAndCompute : On lance SIFT. cv::noArray() signifie qu'on n'utilise pas de masque restrictif. OpenCV va remplir keypointsA (les positions X,Y des points) et descriptorsA (l'empreinte mathématique de chaque point).
    sift->detectAndCompute(inputA, cv::noArray(), keypointsA, descriptorsA);
    sift->detectAndCompute(inputB, cv::noArray(), keypointsB, descriptorsB);
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
        out.pointsInA.push_back(keypointsA[match.trainIdx].pt); // ajout des coordonnees des pts valides dans listes de sorties
        out.pointsInB.push_back(keypointsB[match.queryIdx].pt);
        out.confidenceScores.push_back(1.f - match.distance / candidates[1].distance);
    }

    if (out.pointsInA.size() < 4) { //SI moins que 4 points trouvés, on efface tout et on retourne faux car pas assez de points d'interets
        out.pointsInA.clear();
        out.pointsInB.clear();
        out.confidenceScores.clear();
        return false;
    }



    std::vector<float> translationsX, translationsY;
    translationsX.reserve(out.pointsInA.size()); //.reserve() : optimisation C++. Au lieu que la liste demande de la nouvelle mémoire à l'ordinateur à chaque point ajouté, on réserve d'un coup la taille exacte nécessaire.
    translationsY.reserve(out.pointsInA.size());
    for (size_t i = 0; i < out.pointsInA.size(); ++i) {
        translationsX.push_back(out.pointsInA[i].x - out.pointsInB[i].x);
        translationsY.push_back(out.pointsInA[i].y - out.pointsInB[i].y);
    }


    // std::nth_element : Une fonction ultra-rapide de la librairie standard C++. Plutôt que de trier complètement la liste (ce qui prend du temps), elle réorganise juste les données pour placer la médiane (la valeur du milieu)
    //  à sa position exacte, et met tout ce qui est plus petit à gauche, et tout ce qui est plus grand à droite.
    //On l'applique sur X et Y, on enregistre dans out.dx et out.dy, et on renvoie true pour signaler que le module a terminé son travail avec succès.
    const auto median = [](std::vector<float>& values) {
        const auto middle = values.begin() + values.size() / 2;
        std::nth_element(values.begin(), middle, values.end());
        return *middle;
    };
    out.translationX = median(translationsX);
    out.translationY = median(translationsY);
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