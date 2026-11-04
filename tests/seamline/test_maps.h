// Cartes de coût déterministes partagées par les tests des solveurs et des jonctions.
// Elles sont calculées à l'identique par le notebook Python : les valeurs attendues des tests
// « Parity* » ont été obtenues en exécutant les fonctions du notebook sur ces mêmes cartes.
#pragma once

#include <opencv2/core.hpp>

namespace testmaps {

    // Python : c[y, x] = 1 + ((y*a + x*b) % 11) * 0.37 + 0.5 * ((x*x + 3*y) % 5)
    inline cv::Mat regular(int h, int w, int a, int b) {
        cv::Mat c(h, w, CV_64F);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                c.at<double>(y, x) = 1 + double((y * a + x * b) % 11) * 0.37 + 0.5 * double((x * x + 3 * y) % 5);
        return c;
    }

    // Labyrinthe : fond cher (regular + 4) et un couloir bon marché (0,2) en forme de « Z » :
    // colonne 1 sur la moitié haute, ligne h/2, colonne w−2 sur la moitié basse.
    // La DP (monotone) ne peut pas suivre le couloir horizontal ; Dijkstra-8 oui.
    inline cv::Mat maze(int h, int w, int a, int b) {
        cv::Mat c = regular(h, w, a, b) + 4.0;
        c(cv::Range(0, h / 2 + 1), cv::Range(1, 2)).setTo(0.2);
        c(cv::Range(h / 2, h / 2 + 1), cv::Range(1, w - 1)).setTo(0.2);
        c(cv::Range(h / 2, h), cv::Range(w - 2, w - 1)).setTo(0.2);
        return c;
    }

}  // namespace testmaps
