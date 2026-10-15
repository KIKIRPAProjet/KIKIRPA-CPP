// test_seamline.cpp
// Vérification du code seamline (seamlib) : images en gris à 1 canal, repli de la DP contrainte.
//
//   test_seamline
//       Code de retour 0 si tous les tests passent.

#include "algorithms/seamline/dp_dijkstra/cost_catalog.hpp"
#include "algorithms/seamline/dp_dijkstra/seam_finder.hpp"
#include "algorithms/seamline/dp_dijkstra/solver.hpp"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace stitching::tests {

constexpr double TOLERANCE_PARITE = 1e-4;  // écart maximal entre la carte gris et la carte gris recopié sur 3 canaux
constexpr int    HAUTEUR          = 400;   // px : overlaps de test
constexpr int    LARGEUR          = 120;
constexpr double ECART_GAUCHE     = 20000; // niveaux 16 bits ajoutés à B sur la moitié gauche

int nbEchecs = 0;

void verifier(bool condition, const std::string& message) {
    std::cout << (condition ? "  [ok]    " : "  [ECHEC] ") << message << '\n';
    if (!condition) ++nbEchecs;
}

// Texture 16 bits à 1 canal, reproductible (bruit flouté, comme un grain de peinture).
cv::Mat textureGris(int graine) {
    cv::RNG rng(graine);
    cv::Mat bruit(HAUTEUR, LARGEUR, CV_32F);
    rng.fill(bruit, cv::RNG::UNIFORM, 0.0, 65535.0);
    cv::GaussianBlur(bruit, bruit, cv::Size(0, 0), 2.0);
    cv::Mat texture;
    bruit.convertTo(texture, CV_16U);
    return texture;
}

bool toutFini(const cv::Mat& m) {
    return cv::checkRange(m);
}

void testerGris() {
    std::cout << "Images en gris (1 canal)\n";
    const cv::Mat a = textureGris(1), b = textureGris(2);

    const seamlib::CostMaps m = seamlib::compute_cost_maps(a, b);
    verifier(m.treated.size() == a.size() && m.treated.type() == CV_32F && toutFini(m.treated),
             "compute_cost_maps accepte 1 canal : carte CV_32F de la taille de l'overlap, valeurs finies");

    for (seamlib::CostFunction fn : seamlib::all_cost_functions()) {
        bool ok = false;
        try {
            ok = toutFini(seamlib::compute_cost(a, b, fn).treated);
        }
        catch (const std::exception&) {}
        verifier(ok, "catalogue en gris : " + seamlib::cost_function_name(fn));
    }

    // En Manhattan, une image grise et sa copie sur 3 canaux identiques donnent la même carte :
    // le chemin 1 canal ne change pas le comportement couleur.
    cv::Mat a3, b3;
    cv::merge(std::vector<cv::Mat>{a, a, a}, a3);
    cv::merge(std::vector<cv::Mat>{b, b, b}, b3);
    for (const char* normalisation : {"noise", "percentile"}) {
        seamlib::CostMapParams p;
        p.normalization = normalisation;
        const double ecart = cv::norm(seamlib::compute_cost_maps(a, b, p).treated,
                                      seamlib::compute_cost_maps(a3, b3, p).treated, cv::NORM_INF);
        verifier(ecart <= TOLERANCE_PARITE,
                 std::string("gris = gris recopié sur 3 canaux (") + normalisation + "), écart max " + std::to_string(ecart));
    }

    // B diffère fortement de A sur la moitié gauche : la seamline doit passer dans la moitié droite
    // (où le coût est nul partout ; la DP garde le premier minimum, donc elle longe la frontière).
    cv::Mat bDecale;
    a.convertTo(bDecale, CV_32F);
    bDecale(cv::Rect(0, 0, LARGEUR / 2, HAUTEUR)) += ECART_GAUCHE;
    bDecale.convertTo(bDecale, CV_16U);
    seamlib::SeamFinder finder;
    const std::vector<cv::Point> seam = finder.compute(a, bDecale);
    bool aDroite = seam.size() == static_cast<size_t>(HAUTEUR);
    for (const cv::Point& pt : seam) aDroite = aDroite && pt.x >= LARGEUR / 2;
    verifier(aDroite, "seamline en gris : elle évite la moitié où A et B diffèrent");

    bool refuse = false;
    try {
        cv::Mat b3canaux;
        cv::merge(std::vector<cv::Mat>{b, b, b}, b3canaux);
        seamlib::compute_cost_maps(a, b3canaux);
    }
    catch (const std::exception&) { refuse = true; }
    verifier(refuse, "A à 1 canal et B à 3 canaux -> exception");
}

void testerRepliDp() {
    std::cout << "DP contrainte infaisable (écart latéral > hauteur)\n";
    const cv::Mat carte(100, 300, CV_32F, cv::Scalar(1.0));
    const cv::Point depart(10, 0), arrivee(290, 99);
    for (seamlib::Method m : seamlib::all_methods()) {
        bool ok = false;
        try {
            const seamlib::SolveResult r = seamlib::solve_edge(carte, m, &depart, &arrivee);
            ok = !r.path.empty() && r.path.front() == depart && r.path.back() == arrivee;
        }
        catch (const std::exception&) {}
        verifier(ok, seamlib::method_name(m) + " : chemin trouvé entre les extrémités imposées");
    }
}

} // namespace stitching::tests

int main() {
    using namespace stitching::tests;
    testerGris();
    testerRepliDp();
    std::cout << '\n' << (nbEchecs == 0 ? "Tous les tests passent." : std::to_string(nbEchecs) + " échec(s).") << '\n';
    return nbEchecs == 0 ? 0 : 1;
}
