// test_recouvrement_step.cpp
// Tests unitaires et d'intégration pour RecouvrementStep.
//
// Vérifie :
//   1. Entrées invalides (recouvrement <= 0 ou >= 1, DataStore vide, paramètres invalides)
//   2. Images manquantes, illisibles et incompatibles (types différents, dimensions différentes)
//   3. Tuiles sans recouvrement ou à recouvrement faible/ambigu
//   4. Grille synthétique 2x2 avec positions et recouvrements réels connus (voisins horizontaux et verticaux)
//   5. Intégration dans le Pipeline après ImportStep et publication dans le DataStore

#include "steps/RecouvrementStep.h"
#include "steps/ImportStep.h"
#include "core/Pipeline.h"
#include "core/Tuile.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace stitching;
using namespace stitching::steps;

namespace {

int nbEchecs = 0;

void verifier(bool condition, const std::string& message) {
    std::cout << (condition ? "  [OK]    " : "  [ECHEC] ") << message << '\n';
    if (!condition) ++nbEchecs;
}

template <typename... Args>
std::string format(const char* modele, Args... args) {
    char tampon[512];
    std::snprintf(tampon, sizeof tampon, modele, args...);
    return tampon;
}

// Générateur de texture synthétique 16 bits multi-échelle (identique à test_correlation_phase)
cv::Mat textureGeneree(cv::Size taille, unsigned graine) {
    cv::RNG rng(graine);
    cv::Mat somme = cv::Mat::zeros(taille, CV_64F);
    for (int echelle = 1; echelle <= 128; echelle *= 2) {
        cv::Mat bruit(std::max(2, taille.height / echelle), std::max(2, taille.width / echelle), CV_64F);
        rng.fill(bruit, cv::RNG::UNIFORM, 0.0, 1.0);
        cv::Mat agrandi;
        cv::resize(bruit, agrandi, taille, 0, 0, cv::INTER_CUBIC);
        somme += agrandi * std::sqrt(static_cast<double>(echelle));
    }
    cv::normalize(somme, somme, 3000, 60000, cv::NORM_MINMAX);
    cv::Mat resultat;
    somme.convertTo(resultat, CV_16U);
    return resultat;
}

} // namespace

// ======================================== 1. Entrées Invalides ========================================

void testerEntreesInvalides() {
    std::cout << "\n=== 1. Test Entrees Invalides ===\n";

    // 1.1 Overlap <= 0 ou >= 1
    {
        DataStore ds;
        Tiles tiles;
        ds.set("tiles", tiles);

        RecouvrementStep step1(RecouvrementInput{0.0f, {}});
        step1.setDataStore(ds);
        bool ok1 = step1.execute();
        verifier(!ok1 && step1.exportData().contains("error"),
                 "overlap = 0.0f doit echouer avec un message d'erreur");

        RecouvrementStep step2(RecouvrementInput{-0.2f, {}});
        step2.setDataStore(ds);
        bool ok2 = step2.execute();
        verifier(!ok2 && step2.exportData().contains("error"),
                 "overlap = -0.2f doit echouer avec un message d'erreur");

        RecouvrementStep step3(RecouvrementInput{1.0f, {}});
        step3.setDataStore(ds);
        bool ok3 = step3.execute();
        verifier(!ok3 && step3.exportData().contains("error"),
                 "overlap = 1.0f doit echouer avec un message d'erreur");
    }

    // 1.2 DataStore absent ou sans la cle 'tiles'
    {
        RecouvrementStep step;
        bool okNoDataStore = step.execute();
        verifier(!okNoDataStore && step.exportData().contains("error"),
                 "execute() sans DataStore doit echouer proprement");

        DataStore emptyDs;
        step.setDataStore(emptyDs);
        bool okNoTiles = step.execute();
        verifier(!okNoTiles && step.exportData().contains("error"),
                 "execute() sans cle 'tiles' dans DataStore doit echouer proprement");
    }

    // 1.3 Paramètres d'estimation invalides (ex. fractionValideMin > 1 ou fenetre < 8)
    {
        DataStore ds;
        Tiles tiles;
        ds.set("tiles", tiles);

        matching::correlation_phase::Parametres p;
        p.fractionValideMin = 1.5; // invalide : doit être dans [0, 1]
        RecouvrementStep step(RecouvrementInput{0.2f, p});
        step.setDataStore(ds);
        bool ok = step.execute();
        verifier(!ok && step.exportData().contains("error"),
                 "parametres invalides (fractionValideMin > 1) doivent echouer avec message d'erreur");
    }
}

// ======================================== 2. Images Manquantes et Incompatibles ========================================

void testerImagesManquantesEtIncompatibles(const fs::path& tempDir) {
    std::cout << "\n=== 2. Test Images Manquantes et Incompatibles ===\n";

    fs::create_directories(tempDir);

    // 2.1 Image introuvable / manquante
    {
        DataStore ds;
        Tiles tiles;
        auto t0 = std::make_shared<Tuile>(0, tempDir / "inexistant_0.tif", fs::path{}, 500, 500, CV_16U);
        auto t1 = std::make_shared<Tuile>(1, tempDir / "inexistant_1.tif", fs::path{}, 500, 500, CV_16U);
        t0->setVoisins(nullptr, t1.get(), nullptr, nullptr);
        tiles.push_back(t0);
        tiles.push_back(t1);
        ds.set("tiles", tiles);

        RecouvrementStep step(0.2f);
        step.setDataStore(ds);
        bool ok = step.execute();
        verifier(!ok, "step echoue si les images sources sont manquantes");
        {
            const Json exp1 = step.exportData();
            verifier(exp1.contains("failures") && exp1["failures"].size() > 0,
                     "echec de lecture enregistre dans exportData()['failures']");
        }
    }

    // 2.2 Types d'images incompatibles (8 bits vs 16 bits)
    {
        const fs::path pathA = tempDir / "img_typeA.tif";
        const fs::path pathB = tempDir / "img_typeB.tif";

        cv::Mat imgA(500, 500, CV_8U, cv::Scalar(128));
        cv::Mat imgB(500, 500, CV_16U, cv::Scalar(30000));
        cv::imwrite(pathA.string(), imgA);
        cv::imwrite(pathB.string(), imgB);

        DataStore ds;
        Tiles tiles;
        auto t0 = std::make_shared<Tuile>(0, pathA, fs::path{}, 500, 500, CV_8U);
        auto t1 = std::make_shared<Tuile>(1, pathB, fs::path{}, 500, 500, CV_16U);
        t0->setVoisins(nullptr, t1.get(), nullptr, nullptr);
        tiles.push_back(t0);
        tiles.push_back(t1);
        ds.set("tiles", tiles);

        RecouvrementStep step(0.2f);
        step.setDataStore(ds);
        bool ok = step.execute();
        verifier(!ok, "step echoue si types incompatibles (CV_8U vs CV_16U)");
        bool foundReason = false;
        {
            // Capturer une seule fois : évite UB sur temporaire dans le range-for.
            const Json exported = step.exportData();
            if (exported.contains("failures")) {
                for (const auto& f : exported["failures"]) {
                    const std::string reason = f.value("reason", std::string{});
                    if (reason.find("incompatible image types") != std::string::npos) {
                        foundReason = true;
                        break;
                    }
                }
            }
        }
        verifier(foundReason, "la raison 'incompatible image types' est tracee dans failures");
    }

    // 2.3 Tailles d'images incompatibles (500x500 vs 600x600)
    {
        const fs::path pathA = tempDir / "img_sizeA.tif";
        const fs::path pathB = tempDir / "img_sizeB.tif";

        cv::Mat imgA(500, 500, CV_16U, cv::Scalar(1000));
        cv::Mat imgB(600, 600, CV_16U, cv::Scalar(1000));
        cv::imwrite(pathA.string(), imgA);
        cv::imwrite(pathB.string(), imgB);

        DataStore ds;
        Tiles tiles;
        auto t0 = std::make_shared<Tuile>(0, pathA, fs::path{}, 500, 500, CV_16U);
        auto t1 = std::make_shared<Tuile>(1, pathB, fs::path{}, 600, 600, CV_16U);
        t0->setVoisins(nullptr, t1.get(), nullptr, nullptr);
        tiles.push_back(t0);
        tiles.push_back(t1);
        ds.set("tiles", tiles);

        RecouvrementStep step(0.2f);
        step.setDataStore(ds);
        bool ok = step.execute();
        verifier(!ok, "step echoue si dimensions incompatibles (500x500 vs 600x600)");
        bool foundReason = false;
        {
            // Capturer une seule fois : évite UB sur temporaire dans le range-for.
            const Json exported = step.exportData();
            if (exported.contains("failures")) {
                for (const auto& f : exported["failures"]) {
                    const std::string reason = f.value("reason", std::string{});
                    if (reason.find("incompatible image sizes") != std::string::npos) {
                        foundReason = true;
                        break;
                    }
                }
            }
        }
        verifier(foundReason, "la raison 'incompatible image sizes' est tracee dans failures");
    }
}

// ======================================== 3. Pas de Recouvrement et Recouvrement Faible ========================================

void testerSansRecouvrementEtRecouvrementFaible(const fs::path& tempDir) {
    std::cout << "\n=== 3. Test Sans Recouvrement et Recouvrement Faible ===\n";

    fs::create_directories(tempDir);

    // 3.1 Deux tuiles sans recouvrement (textures totalement disjointes / décalage hors marge)
    {
        const cv::Mat canvasA = textureGeneree(cv::Size(600, 600), 101);
        const cv::Mat canvasB = textureGeneree(cv::Size(600, 600), 999); // graine completement differente

        const fs::path pathA = tempDir / "disjoint_A.tif";
        const fs::path pathB = tempDir / "disjoint_B.tif";
        cv::imwrite(pathA.string(), canvasA);
        cv::imwrite(pathB.string(), canvasB);

        DataStore ds;
        Tiles tiles;
        auto t0 = std::make_shared<Tuile>(0, pathA, fs::path{}, 600, 600, CV_16U);
        auto t1 = std::make_shared<Tuile>(1, pathB, fs::path{}, 600, 600, CV_16U);
        t0->setVoisins(nullptr, t1.get(), nullptr, nullptr);
        tiles.push_back(t0);
        tiles.push_back(t1);
        ds.set("tiles", tiles);

        RecouvrementStep step(0.20f);
        step.setDataStore(ds);
        bool ok = step.execute();
        verifier(!ok, "paire sans contenu commun ou hors marge doit echouer");
        verifier(step.exportData().contains("failures") && !step.exportData()["failures"].empty(),
                 "echec sans recouvrement trace dans failures");
    }

    // 3.2 Recouvrement plat / ambigu (texture uniforme sans pic franc)
    {
        cv::Mat uniformA(600, 600, CV_16U, cv::Scalar(10000));
        cv::Mat uniformB(600, 600, CV_16U, cv::Scalar(10000));

        const fs::path pathA = tempDir / "uniform_A.tif";
        const fs::path pathB = tempDir / "uniform_B.tif";
        cv::imwrite(pathA.string(), uniformA);
        cv::imwrite(pathB.string(), uniformB);

        DataStore ds;
        Tiles tiles;
        auto t0 = std::make_shared<Tuile>(0, pathA, fs::path{}, 600, 600, CV_16U);
        auto t1 = std::make_shared<Tuile>(1, pathB, fs::path{}, 600, 600, CV_16U);
        t0->setVoisins(nullptr, t1.get(), nullptr, nullptr);
        tiles.push_back(t0);
        tiles.push_back(t1);
        ds.set("tiles", tiles);

        RecouvrementStep step(0.20f);
        step.setDataStore(ds);
        bool ok = step.execute();
        verifier(!ok, "tuiles uniformes (pic ambigu) doivent echouer au test du seuil de rapport");
    }
}

// ======================================== 4. Grille Synthétique 2x2 à Vérité Connue ========================================

void testerGrilleSynthetique2x2(const fs::path& tempDir) {
    std::cout << "\n=== 4. Test Grille Synthetique 2x2 (Verite Connue) ===\n";

    fs::create_directories(tempDir);

    constexpr int COTE = 1000;              // px par tuile
    constexpr double RECOUVREMENT_NOM = 0.20; // 20% nominal
    constexpr double RECOUVREMENT_VRAI = 0.23; // 23% réel (30 px d'écart à la position nominale)
    constexpr double TOLERANCE_PIXEL = 1.0;  // EstimationRecouvrement mesure au pixel entier près (facteurDft = 1)
    constexpr double TOLERANCE_OVERLAP = 0.02;

    // Canvas source large pour découper 2x2 tuiles
    const cv::Mat source = textureGeneree(cv::Size(2500, 2500), 12345);

    // Déplacements connus :
    // Voisin horizontal (Right) : dx = 1000 * (1 - 0.23) = 770 px, dy = 3 px
    // Voisin vertical (Bottom)  : dx = -4 px, dy = 1000 * (1 - 0.23) = 770 px
    const cv::Point vraiH(static_cast<int>(std::round(COTE * (1.0 - RECOUVREMENT_VRAI))), 3);
    const cv::Point vraiV(-4, static_cast<int>(std::round(COTE * (1.0 - RECOUVREMENT_VRAI))));

    const cv::Point pos0(200, 200);
    const cv::Point pos1 = pos0 + vraiH;
    const cv::Point pos2 = pos0 + vraiV;
    const cv::Point pos3 = pos2 + vraiH;

    const fs::path path0 = tempDir / "img_C00_R00.tif";
    const fs::path path1 = tempDir / "img_C01_R00.tif";
    const fs::path path2 = tempDir / "img_C00_R01.tif";
    const fs::path path3 = tempDir / "img_C01_R01.tif";

    cv::imwrite(path0.string(), source(cv::Rect(pos0, cv::Size(COTE, COTE))));
    cv::imwrite(path1.string(), source(cv::Rect(pos1, cv::Size(COTE, COTE))));
    cv::imwrite(path2.string(), source(cv::Rect(pos2, cv::Size(COTE, COTE))));
    cv::imwrite(path3.string(), source(cv::Rect(pos3, cv::Size(COTE, COTE))));

    auto t0 = std::make_shared<Tuile>(0, path0, fs::path{}, COTE, COTE, CV_16U);
    auto t1 = std::make_shared<Tuile>(1, path1, fs::path{}, COTE, COTE, CV_16U);
    auto t2 = std::make_shared<Tuile>(2, path2, fs::path{}, COTE, COTE, CV_16U);
    auto t3 = std::make_shared<Tuile>(3, path3, fs::path{}, COTE, COTE, CV_16U);

    // Topologie 2x2 :
    // t0 (0,0) -> Right: t1 (1,0), Bottom: t2 (0,1)
    // t1 (1,0) -> Left:  t0 (0,0), Bottom: t3 (1,1)
    // t2 (0,1) -> Top:   t0 (0,0), Right:  t3 (1,1)
    // t3 (1,1) -> Left:  t2 (0,1), Top:    t1 (1,0)
    t0->setVoisins(nullptr, t1.get(), nullptr, t2.get());
    t1->setVoisins(t0.get(), nullptr, nullptr, t3.get());
    t2->setVoisins(nullptr, t3.get(), t0.get(), nullptr);
    t3->setVoisins(t2.get(), nullptr, t1.get(), nullptr);

    Tiles tiles = {t0, t1, t2, t3};
    DataStore ds;
    ds.set("tiles", tiles);

    RecouvrementStep step(static_cast<float>(RECOUVREMENT_NOM));
    step.setDataStore(ds);

    const bool ok = step.execute();
    verifier(ok, "execution de RecouvrementStep sur la grille 2x2 doit reussir");
    const auto& pairs = step.getOutput().pairs;
    verifier(pairs.size() == 4, format("nombre de paires mesurees attendu = 4, obtenu = %d", static_cast<double>(pairs.size())));

    for (const auto& p : pairs) {
        cv::Point vraiAttendu;
        if (p.dir == Direction::Right) vraiAttendu = vraiH;
        else if (p.dir == Direction::Bottom) vraiAttendu = vraiV;

        const double errX = std::abs(p.estimation.translation.x - vraiAttendu.x);
        const double errY = std::abs(p.estimation.translation.y - vraiAttendu.y);
        const double errOverlap = std::abs(p.estimation.overlap - RECOUVREMENT_VRAI);

        verifier(errX <= TOLERANCE_PIXEL && errY <= TOLERANCE_PIXEL,
                 format("paire (%d -> %d, %s) : translation (%.1f, %.1f), attendu (%d, %d), ecart (%.1f, %.1f) px <= 1.0 px",
                        p.idA, p.idB, p.dir == Direction::Right ? "Right" : "Bottom",
                        p.estimation.translation.x, p.estimation.translation.y,
                        vraiAttendu.x, vraiAttendu.y, errX, errY));

        verifier(errOverlap <= TOLERANCE_OVERLAP,
                 format("paire (%d -> %d) : recouvrement mesure %.2f %% (attendu %.2f %%, ecart %.2f %%)",
                        p.idA, p.idB, p.estimation.overlap * 100.0, RECOUVREMENT_VRAI * 100.0, errOverlap * 100.0));

        verifier(p.zoneA.size() == p.zoneB.size() && !p.zoneA.empty(),
                 format("paire (%d -> %d) : zoneA (%dx%d) == zoneB (%dx%d) et non vide",
                        p.idA, p.idB, p.zoneA.width, p.zoneA.height, p.zoneB.width, p.zoneB.height));

        const cv::Point decalage(static_cast<int>(std::lround(p.estimation.translation.x)),
                                 static_cast<int>(std::lround(p.estimation.translation.y)));
        verifier(p.zoneB == p.zoneA - decalage,
                 format("paire (%d -> %d) : relation geometrique zoneB == zoneA - round(translation) respectee",
                        p.idA, p.idB));

        // Test de conversion toPairContext()
        PairContext ctx = p.toPairContext();
        verifier(ctx.dir == p.dir && ctx.translation.has_value() && *ctx.translation == p.estimation.translation,
                 format("paire (%d -> %d) : toPairContext() produit un PairContext valide", p.idA, p.idB));
    }

    // Publication dans le DataStore
    verifier(ds.has("recouvrements"), "le DataStore contient la cle 'recouvrements'");
    const auto& dsPairs = ds.get<std::vector<PairRecouvrement>>("recouvrements");
    verifier(dsPairs.size() == 4, "le vecteur dans le DataStore contient bien les 4 paires");
}

// ======================================== 5. Test Intégration Pipeline Complet ========================================

void testerPipelineComplet(const fs::path& tempDir) {
    std::cout << "\n=== 5. Test Integration Pipeline (ImportStep -> RecouvrementStep) ===\n";

    fs::create_directories(tempDir);

    // On prépare une mini grille 1x2 (2 tuiles horizontales)
    constexpr int COTE = 800;
    const cv::Mat source = textureGeneree(cv::Size(2000, 1500), 54321);

    const cv::Point posA(100, 100);
    const cv::Point posB(100 + static_cast<int>(std::round(COTE * 0.8)), 102); // ~20% recouvrement nominal

    const fs::path dirImages = tempDir / "images_pipeline";
    fs::create_directories(dirImages);

    cv::imwrite((dirImages / "img_C01_R01.tif").string(), source(cv::Rect(posA, cv::Size(COTE, COTE))));
    cv::imwrite((dirImages / "img_C02_R01.tif").string(), source(cv::Rect(posB, cv::Size(COTE, COTE))));

    Pipeline pipeline;
    pipeline.add(std::make_unique<ImportStep>(ImportInput{dirImages.string(), R"(img_C(\d+)_R(\d+)\.tif)"}));
    pipeline.add(std::make_unique<RecouvrementStep>(RecouvrementInput{0.20f, {}}));

    bool ok = pipeline.run();
    verifier(ok, "Pipeline complet (ImportStep -> RecouvrementStep) s'execute avec succes");
    verifier(pipeline.getData().has("recouvrements"), "DataStore final contient 'recouvrements'");
    const auto& pairs = pipeline.getData().get<std::vector<PairRecouvrement>>("recouvrements");
    verifier(pairs.size() == 1, format("recouvrements trouves dans le pipeline = %d (attendu = 1)", static_cast<double>(pairs.size())));
}

int main(int argc, char** argv) {
    std::cout << "============================================================\n";
    std::cout << "TESTS AUTOMATISES : RecouvrementStep (EstimationRecouvrement)\n";
    std::cout << "============================================================\n";

    const fs::path tempDir = fs::temp_directory_path() / "test_recouvrement_step_tmp";
    std::error_code ec;
    fs::remove_all(tempDir, ec);

    testerEntreesInvalides();
    testerImagesManquantesEtIncompatibles(tempDir);
    testerSansRecouvrementEtRecouvrementFaible(tempDir);
    testerGrilleSynthetique2x2(tempDir);
    testerPipelineComplet(tempDir);

    fs::remove_all(tempDir, ec);

    std::cout << "\n============================================================\n";
    std::cout << (nbEchecs == 0 ? "TOUS LES TESTS ONT REUSSI (0 ECHEC)"
                                : std::to_string(nbEchecs) + " ECHEC(S) DETECTE(S)") << '\n';
    std::cout << "============================================================\n";

    return nbEchecs == 0 ? 0 : 1;
}
