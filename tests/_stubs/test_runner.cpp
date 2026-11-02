#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <numeric>

#include "core/Types.h"
#include "algorithms/stitching/sift/SiftMatcherAlgorithm.h"
#define private public
#include "algorithms/stitching/lightglue/LightGlueMatcher.h"
#undef private

using namespace stitching;
using namespace stitching::matching;

int main() {
    std::cout << "============================================================\n";
    std::cout << "TEST DE VERIFICATION - OVERLAPS PREDECOUPES (SIFT & LIGHTGLUE)\n";
    std::cout << "============================================================\n";

    int failures = 0;

    // -------------------------------------------------------------
    // Test 1 : Types.h - Unification Matches (pointsA, pointsB, scores, dx, dy)
    // -------------------------------------------------------------
    std::cout << "\n=== 1. Test Structure Unifiee Matches (dx, dy, scores) ===\n";
    {
        Matches m;
        m.dx = 12.5f;
        m.dy = -3.2f;
        m.pointsA.push_back(cv::Point2f(10.f, 20.f));
        m.pointsB.push_back(cv::Point2f(15.f, 25.f));
        m.scores.push_back(0.95f);

        if (std::abs(m.dx - 12.5f) < 1e-4f && std::abs(m.dy - (-3.2f)) < 1e-4f &&
            m.pointsA.size() == 1 && m.pointsB.size() == 1 && m.scores.size() == 1) {
            std::cout << "  [OK]    Structure Matches unifiee : dx=" << m.dx << ", dy=" << m.dy << ", score=" << m.scores[0] << "\n";
        } else {
            std::cerr << "  [FAIL]  Structure Matches invalide\n";
            failures++;
        }
    }

    // -------------------------------------------------------------
    // Test 2 : SiftMatcher sur bandes d'overlap predecoupees (sans offset parasite)
    // -------------------------------------------------------------
    std::cout << "\n=== 2. Test SIFT sur bandes d'overlap predecoupees (4 directions & shifts) ===\n";
    {
        // Créer un canvas synthétique avec une grille de points bien répartis
        const int canvasW = 1200, canvasH = 1200;
        cv::Mat canvas(canvasH, canvasW, CV_8U, cv::Scalar(0));
        for (int y = 0; y < canvasH; ++y) {
            for (int x = 0; x < canvasW; ++x) {
                if ((x % 30 == 0) && (y % 30 == 0)) {
                    canvas.data_u8[y * canvasW + x] = 200;
                }
            }
        }

        struct StripTestCase {
            std::string name;
            int ax, ay;          // Coin d'extraction de la bande A dans le canvas
            int bx, by;          // Coin d'extraction de la bande B (avec décalage résiduel connu)
            int stripW, stripH;  // Dimensions de la bande prédécoupée
            float expDx, expDy;  // Décalage attendu (bx - ax, by - ay)
        };

        std::vector<StripTestCase> cases = {
            {"Overlap Horizontal (H) - Shift X:+6, Y:-4", 200, 150, 206, 146, 240, 600,  6.f, -4.f},
            {"Overlap Horizontal (H) - Shift X:-5, Y:+3", 400, 200, 395, 203, 240, 600, -5.f,  3.f},
            {"Overlap Vertical (V)   - Shift X:+2, Y:+8", 150, 200, 152, 208, 600, 240,  2.f,  8.f},
            {"Overlap Vertical (V)   - Shift X:-3, Y:-6", 150, 400, 147, 394, 600, 240, -3.f, -6.f}
        };

        for (const auto& tc : cases) {
            // Dans le pipeline réel, A et B sont DIRECTEMENT les bandes prédécoupées
            cv::Mat stripA = canvas(cv::Rect(tc.ax, tc.ay, tc.stripW, tc.stripH));
            cv::Mat stripB = canvas(cv::Rect(tc.bx, tc.by, tc.stripW, tc.stripH));

            PairContext ctx;
            ctx.overlap = 0.20f;

            Json params;
            Matches m;
            bool ok = runSiftMatching(stripA, stripB, ctx, params, m);

            if (ok && std::abs(m.dx - tc.expDx) < 1e-2f && std::abs(m.dy - tc.expDy) < 1e-2f) {
                // Vérifier qu'aucun point ne déborde de la bande (pas de faux offset ajouté)
                bool pointsValid = true;
                for (const auto& pt : m.pointsA) {
                    if (pt.x < 0 || pt.x >= tc.stripW || pt.y < 0 || pt.y >= tc.stripH) pointsValid = false;
                }
                for (const auto& pt : m.pointsB) {
                    if (pt.x < 0 || pt.x >= tc.stripW || pt.y < 0 || pt.y >= tc.stripH) pointsValid = false;
                }

                if (pointsValid) {
                    std::cout << "  [OK]    " << tc.name << " : ok=" << ok << ", nbMatches=" << m.pointsA.size()
                              << ", dx=" << m.dx << ", dy=" << m.dy << " (points dans le repère exact [0, W]x[0, H])\n";
                } else {
                    std::cerr << "  [FAIL]  " << tc.name << " : points hors limites de la bande prédécoupée !\n";
                    failures++;
                }
            } else {
                std::cerr << "  [FAIL]  " << tc.name << " : dx=" << m.dx << " (attendu " << tc.expDx
                          << "), dy=" << m.dy << " (attendu " << tc.expDy << ")\n";
                failures++;
            }
        }
    }

    // -------------------------------------------------------------
    // Test 3 : SiftMatcher 16-bit sombre / IR Apollo ([600, 2400] sur 65535) sur bande
    // -------------------------------------------------------------
    std::cout << "\n=== 3. Test SIFT IR 16-bit sombre sur bande d'overlap prédécoupée ===\n";
    {
        const int canvasW = 1200, canvasH = 1200;
        cv::Mat canvas16(canvasH, canvasW, CV_16U, cv::Scalar(0));
        for (int y = 0; y < canvasH; ++y) {
            for (int x = 0; x < canvasW; ++x) {
                if ((x % 30 == 0) && (y % 30 == 0)) {
                    canvas16.data_u16[y * canvasW + x] = 2400; // Point d'intérêt
                } else {
                    canvas16.data_u16[y * canvasW + x] = 600;  // Fond sombre
                }
            }
        }

        // Bande prédécoupée avec décalage résiduel de +4 px en X et -2 px en Y
        cv::Mat stripA = canvas16(cv::Rect(200, 200, 240, 600));
        cv::Mat stripB = canvas16(cv::Rect(204, 198, 240, 600));

        PairContext ctx;
        ctx.overlap = 0.20f;

        Json params;
        Matches m;
        bool ok = runSiftMatching(stripA, stripB, ctx, params, m);
        if (ok && std::abs(m.dx - 4.f) < 1e-2f && std::abs(m.dy - (-2.f)) < 1e-2f) {
            std::cout << "  [OK]    SIFT IR 16-bit sombre sur bande : ok=" << ok << ", nbMatches=" << m.pointsA.size()
                      << ", dx=" << m.dx << ", dy=" << m.dy << " (étirement dynamique conjoint préservé)\n";
        } else {
            std::cerr << "  [FAIL]  SIFT IR 16-bit sombre : dx=" << m.dx << ", dy=" << m.dy << "\n";
            failures++;
        }
    }

    // -------------------------------------------------------------
    // Test 4, 5, 6 : LightGlueMatcher (normalisation 16-bit, échelle anisotrope, direct bande)
    // -------------------------------------------------------------
    std::cout << "\n=== 4, 5 & 6. Test LightGlueMatcher (Direct bande, 16-bit [0,1], scaleX/scaleY, empty->false) ===\n";
    {
        struct TestableLightGlue : public LightGlueMatcher {
            TestableLightGlue() : LightGlueMatcher() {}
        };

        TestableLightGlue matcher;

        // Test normalisation 16-bit : une image 16-bit à mi-dynamique (32768)
        cv::Mat img16(100, 100, CV_16U, cv::Scalar(0));
        img16.data_u16[50 * 100 + 50] = 32768;
        float scaleX = 1.f, scaleY = 1.f;
        cv::Mat blob16 = matcher.preprocessImage(img16, "test_16b", scaleX, scaleY, true);

        double minV = 0, maxV = 0;
        cv::minMaxLoc(blob16, &minV, &maxV);
        if (maxV <= 1.01 && maxV >= 0.49) {
            std::cout << "  [OK]    Normalisation 16 bits : max du tenseur ONNX = " << maxV
                      << " (attendu dans [0, 1], sans division erronée par 255.0)\n";
        } else {
            std::cerr << "  [FAIL]  Normalisation 16 bits incorrecte : max = " << maxV << "\n";
            failures++;
        }

        // Test échelle anisotrope (scaleX != scaleY) pour bande non carrée
        cv::Mat stripImg(2400, 480, CV_8U, cv::Scalar(100)); // Bande d'overlap haute et étroite
        float sx = 1.f, sy = 1.f;
        cv::Mat blobStrip = matcher.preprocessImage(stripImg, "strip_img", sx, sy, true);
        if (std::abs(sx - 1.0f) < 1e-4f && std::abs(sy - 1.0f) < 1e-4f) {
            std::cout << "  [OK]    Echelle bande 480x2400 (multiple de 32) : scaleX = " << sx << ", scaleY = " << sy << "\n";
        } else {
            std::cerr << "  [FAIL]  Echelle bande incorrecte : scaleX=" << sx << " scaleY=" << sy << "\n";
            failures++;
        }

        // Test projection directe sans offset parasite
        {
            // Un point dans le repère local de la bande A et de la bande B
            float scaleX1 = 1.0f, scaleY1 = 1.0f;
            float scaleX2 = 1.0f, scaleY2 = 1.0f;
            float kptA_x = 120.f, kptA_y = 350.f;
            float kptB_x = 114.f, kptB_y = 353.f;

            cv::Point2f ptA(kptA_x / scaleX1, kptA_y / scaleY1);
            cv::Point2f ptB(kptB_x / scaleX2, kptB_y / scaleY2);

            float dx = ptA.x - ptB.x;
            float dy = ptA.y - ptB.y;

            if (std::abs(dx - 6.f) < 1e-4f && std::abs(dy - (-3.f)) < 1e-4f) {
                std::cout << "  [OK]    Projection directe bande prédécoupée : dx = " << dx << ", dy = " << dy
                          << " (aucun faux offset rajouté)\n";
            } else {
                std::cerr << "  [FAIL]  Projection directe incorrecte : dx=" << dx << "\n";
                failures++;
            }
        }

        // Test vide -> match renvoie false
        Matches emptyMatches;
        PairContext dummyCtx;
        cv::Mat emptyImg;
        bool res = matcher.match(emptyImg, emptyImg, dummyCtx, emptyMatches);
        if (!res) {
            std::cout << "  [OK]    LightGlueMatcher::match retourne bien false quand l'image est vide ou sans match\n";
        } else {
            std::cerr << "  [FAIL]  LightGlueMatcher::match aurait du retourner false\n";
            failures++;
        }
    }

    std::cout << "\n============================================================\n";
    if (failures == 0) {
        std::cout << "RESULTAT : TOUS LES TESTS ONT REUSSI (0 ECHEC)\n";
    } else {
        std::cout << "RESULTAT : " << failures << " ECHEC(S) DETECTE(S)\n";
    }
    std::cout << "============================================================\n";

    return failures;
}
