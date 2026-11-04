
 // include/backend/algorithms/seamline/dynamic_programming_dijkstra/solver.h
#pragma once

#include "algorithms/seamline/dynamic_programming_dijkstra/export.h"

#include <opencv2/core.h>

#include <limits>
#include <string>
#include <vector>

namespace DynamicProgrammingDijkstraNs {

    // Les trois méthodes de résolution, dans l'ordre du tuple Python METHODS.
    enum class Method {
        DP,                 // Chemin monotone
        Dijkstra8,          // Plus court chemin avec 8 voisins
        DPDijkstraLocal     // DP suivie de Dijkstra dans un corridor
    };

    // Renvoie le nom de la méthode utilisé dans les paramètres et résultats
    // Lève std::invalid_argument si la méthode est inconnue
    KIRK_API std::string getMethodName(Method method);

    // Retrouve une méthode à partir de son nom exact
    // Lève std::invalid_argument si le nom est inconnu
    KIRK_API Method getMethodFromName(const std::string& methodName);

    // Renvoie les méthodes de résolution disponibles
    KIRK_API const std::vector<Method>& getAllMethods();

    // Résultat d'une résolution : chemin, coût et statistiques de performance
    struct SolveResult {
        Method method = Method::DP;
        std::vector<cv::Point> path;      // Points (x, y), du départ vers l'arrivée

        double objective = 0.0;          // Coût total du chemin
        double wall_s = 0.0;             // Durée totale en secondes

        long long expanded = 0;          // Cellules ou nœuds traités
        long long pushes = 0;            // Insertions dans le tas de Dijkstra
        long long max_heap = 0;          // Taille maximale du tas
        long long cells = 0;             // Nombre de pixels de la carte de coût

        // Statistiques du mode hybride DP puis Dijkstra local
        // NaN pour les autres méthodes
        double corridor_ratio =
            std::numeric_limits<double>::quiet_NaN();  // Fraction de la carte dans le corridor

        double seed_s =
            std::numeric_limits<double>::quiet_NaN();  // Durée de la DP initiale, en secondes

        double refine_s =
            std::numeric_limits<double>::quiet_NaN();  // Durée du raffinement, en secondes

        double mem_est_mb = 0.0;         // Estimation de la mémoire en Mio
    };

    // Calcule le coût total d'un chemin avec la règle de déplacement commune
    // Renvoie l'infini positif si le chemin est vide
    KIRK_API double computePathObjective(
        const cv::Mat& inputCost,
        const std::vector<cv::Point>& path
    );

    // Estime la mémoire des tableaux du solveur et du tas de Dijkstra
    // La taille d'une entrée du tas est estimée à 16 octets
    // Renvoie une estimation en Mio, et non la mémoire réellement utilisée
    KIRK_API double estimateSolverMemoryMiB(
        Method method,
        const cv::Size& imageSize,
        long long maxHeap = 0
    );

    // Résout une adjacence avec la méthode choisie
    // inputCost : carte de coût à un canal
    // startPoint et goalPoint : extrémités imposées, nullptr si libres
    // corridorRadius : rayon du corridor en pixels, utilisé par DPDijkstraLocal
    KIRK_API SolveResult solveEdge(
        const cv::Mat& inputCost,
        Method method,
        const cv::Point* startPoint = nullptr,
        const cv::Point* goalPoint = nullptr,
        int corridorRadius = 18
    );

}  // namespace DynamicProgrammingDijkstraNs
