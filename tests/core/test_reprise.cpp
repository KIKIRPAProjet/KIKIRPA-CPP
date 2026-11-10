// tests/core/test_reprise.cpp
// Reprise d'un pipeline (core/Pipeline::run, core/Checkpoint, core/RunState) avec des steps de test :
// "a" produit un entier, "b" lui ajoute sa valeur. Chaque step compte ses exécutions, ce qui permet de vérifier
// lesquels sont sautés à la reprise.
//
// GoogleTest, ctest -L unit : sans checkpoint, échec puis reprise, options modifiées, run.from, skip=false,
// pause, script réécrit après chaque step, lecture du champ "run", sérialisation des types du pipeline.

#include "core/checkpoint/checkpoint.h"
#include "core/checkpoint/checkpoint_types.h"
#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/run_state.h"
#include "core/step/data_store.h"
#include "core/step/step.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/types.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Json = nlohmann::json;

constexpr int VALEUR_A = 10;
constexpr int VALEUR_B = 5;
constexpr int VALEUR_A_MODIFIEE = 20;

// Valeurs de l'aller-retour des types du pipeline : des fractions binaires exactes, pour comparer avec ==.
constexpr std::size_t NOMBRE_TYPES_PIPELINE = 7;
constexpr int LARGEUR_TUILE = 400;
constexpr int HAUTEUR_TUILE = 300;
constexpr float POINT_A_X = 1.25F;
constexpr float POINT_A_Y = 2.5F;
constexpr float POINT_B_X = 3.125F;
constexpr float POINT_B_Y = 4.0F;
constexpr float SCORE = 0.75F;
constexpr float TRANSLATION_X = 319.875F;
constexpr float TRANSLATION_Y = -0.03125F;
constexpr double POSITION_X = 320.123456789;
constexpr double POSITION_Y = -0.5;
constexpr double ROTATION = 0.001;
constexpr int COTE_POLYGONE = 10;
constexpr double GAIN_TUILE_0 = 0.75;
constexpr double GAIN_TUILE_1 = 1.25;
constexpr double FORCE_CHAMP = -0.03125;

// Vérification non bloquante : en cas d'échec, GoogleTest affiche le message
void verifier(bool ok, const std::string& message) {
    EXPECT_TRUE(ok) << message;
}

// Réglages d'un step de test.
struct CompteurConfig {
    std::string nom;
    int valeur = 0;
    std::string entree;   // clé lue, vide pour le premier step
    std::string sortie;   // clé écrite
};

// Step de test : sortie = valeur (+ entrée si elle existe). Option "echoue" : renvoie false.
class StepCompteurClass : public StepNs::StepClass {
    int& executions;
    std::string entree, sortie;
    std::function<void()> pendant;   // appelé pendant execute() (ex. demande de pause)
public:
    StepCompteurClass(const CompteurConfig& config, int& executions, std::function<void()> pendant = {})
        : executions(executions), entree(config.entree), sortie(config.sortie), pendant(std::move(pendant)) {
        ConfigNs::StepConfig cfg;
        cfg.name = config.nom;
        cfg.parameters = {{"int", "valeur", config.valeur}, {"bool", "echoue", false}};
        if (!entree.empty()) cfg.inputs = {{"Compteur", entree}};
        cfg.outputs = {{"Compteur", sortie}};
        configJson = cfg;
    }
    bool execute() override {
        ++executions;
        result = Json::object();
        if (getParameter("echoue").get<bool>()) {
            result["error"] = "échec demandé";
            return false;
        }
        int v = getParameter("valeur").get<int>();
        if (!entree.empty()) v += dataStore->get<int>(entree);
        dataStore->replace(sortie, v);
        result["valeur"] = v;
        if (pendant) pendant();
        return true;
    }
};

// Sérialisation du type de test "Compteur" : un entier dans <clé>.txt
void declarerCompteur() {
    CheckpointNs::CheckpointClass::registerType(
        "Compteur",
        [](const StepNs::DataStoreClass& d, const std::string& cle, const fs::path& dossier) {
            std::ofstream(dossier / (cle + ".txt")) << d.get<int>(cle);
        },
        [](StepNs::DataStoreClass& d, const std::string& cle, const fs::path& dossier) {
            std::ifstream in(dossier / (cle + ".txt"));
            int v = 0;
            if (!(in >> v)) throw std::runtime_error("lecture impossible");
            d.replace(cle, v);
        });
}

// Nombre d'exécutions attendues de a et de b pendant un run.
struct Executions {
    int a = 0;
    int b = 0;
};

struct Banc {
    int execA = 0, execB = 0;
    PipelineNs::PipelineClass p;
    Banc(const fs::path& checkpoints, std::function<void()> pendantA = {}) {
        p.addStep(std::make_unique<StepCompteurClass>(CompteurConfig{"a", VALEUR_A, "", "x"}, execA, std::move(pendantA)));
        p.addStep(std::make_unique<StepCompteurClass>(CompteurConfig{"b", VALEUR_B, "x", "y"}, execB));
        PipelineNs::RunState r;
        r.checkpointDirectory = checkpoints.string();
        p.setRunState(r);
    }
    // Change une option d'un step (index), comme le ferait un JSON relu
    void option(std::size_t index, const std::string& cle, Json valeur) {
        const auto& s = p.getSteps()[index];
        s->importConfig({{"name", s->getConfigJson().at("name")}, {"parameters", {{{"valueType", valeur.is_boolean() ? "bool" : "int"}, {"name", cle}, {"value", valeur}}}}});
    }
    // Change la consigne startFromStep (vide : aucune) ou skipCompletedSteps avant le prochain run
    void consignes(const std::string& startFromStep, bool skipCompletedSteps = true) {
        PipelineNs::RunState r = p.getRunState();
        r.startFromStep = startFromStep;
        r.skipCompletedSteps = skipCompletedSteps;
        p.setRunState(r);
    }
    bool lancer() { return p.run([](const std::string&) {}); }
    // Lance un run et vérifie le nombre d'exécutions de a et de b pendant ce run.
    bool lancerEtCompter(const Executions& attendues) {
        const int avantA = execA;
        const int avantB = execB;
        return lancer() && execA - avantA == attendues.a && execB - avantB == attendues.b;
    }
    int y() const { return p.getDataStore().has("y") ? p.getDataStore().get<int>("y") : -1; }
};

void testerSansCheckpoint() {
    std::cout << "Sans checkpoint\n";
    Banc b("");
    verifier(b.lancer() && b.lancer(), "deux runs réussis");
    verifier(b.execA == 2 && b.execB == 2, "chaque run refait tous les steps");
    verifier(b.p.getRunState().status == PipelineNs::RunStatus::Completed, "statut completed");
    verifier(b.y() == VALEUR_A + VALEUR_B, "sortie finale correcte");
}

void verifierEchecPuisReprise(Banc& b, const fs::path& dossier) {
    std::cout << "Échec puis reprise\n";
    b.option(1, "echoue", true);
    verifier(!b.lancer(), "run 1 en échec au step b");
    verifier(b.p.getRunState().status == PipelineNs::RunStatus::Failed, "statut failed");
    verifier(b.p.getRunState().completedSteps == std::vector<std::string>{"a"}, "completedSteps = [a]");
    verifier(fs::exists(dossier / "00_a" / "manifest.json"), "checkpoint de a écrit");

    b.option(1, "echoue", false);
    verifier(b.lancerEtCompter({0, 1}), "run 2 réussi : a sauté à la reprise, b refait");
    verifier(b.y() == VALEUR_A + VALEUR_B, "sortie de a restaurée depuis le checkpoint");
}

void verifierOptionsModifiees(Banc& b) {
    std::cout << "Options modifiées\n";
    b.option(0, "valeur", VALEUR_A_MODIFIEE);
    verifier(b.lancerEtCompter({1, 1}), "run 3 réussi : a et b refaits (checkpoint de a périmé)");
    verifier(b.y() == VALEUR_A_MODIFIEE + VALEUR_B, "nouvelle valeur prise en compte");

    std::cout << "Tout est déjà fait\n";
    verifier(b.lancerEtCompter({0, 0}), "aucun step refait");
    verifier(b.y() == VALEUR_A_MODIFIEE + VALEUR_B, "sorties restaurées");
}

void verifierConsignes(Banc& b) {
    std::cout << "run.from et skip\n";
    b.consignes("b");
    verifier(b.lancerEtCompter({0, 1}), "from=b : seul b refait");
    b.consignes("a");
    verifier(b.lancerEtCompter({1, 1}), "from=a : a et b refaits");
    b.consignes("", false);
    verifier(b.lancerEtCompter({1, 1}), "skip=false : tout refait");
    verifier(b.p.getRunState().skipCompletedSteps, "consigne skip remise à true après le run");

    b.consignes("inconnu");
    bool leve = false;
    try { b.lancer(); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "from inconnu : std::invalid_argument");
}

void verifierFromSansCheckpoint(const fs::path& dossier) {
    fs::remove_all(dossier);
    Banc vide(dossier);
    vide.consignes("b");
    bool leve = false;
    try { vide.lancer(); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "from=b sans checkpoint de a : std::invalid_argument");
}

void testerEchecPuisReprise(const fs::path& dossier) {
    fs::remove_all(dossier);
    Banc b(dossier);
    verifierEchecPuisReprise(b, dossier);
    verifierOptionsModifiees(b);
    verifierConsignes(b);
    verifierFromSansCheckpoint(dossier);
}

void testerPause(const fs::path& dossier) {
    std::cout << "Pause\n";
    fs::remove_all(dossier);
    PipelineNs::PipelineClass* cible = nullptr;
    Banc b(dossier, [&] { cible->requestPause(); });
    cible = &b.p;
    verifier(!b.lancer(), "run interrompu");
    verifier(b.p.getRunState().status == PipelineNs::RunStatus::Paused, "statut paused");
    verifier(b.execA == 1 && b.execB == 0, "pause entre a et b");

    Banc suite(dossier);   // nouveau processus : même dossier de checkpoints
    PipelineNs::RunState r = suite.p.getRunState();
    r.completedSteps = b.p.getRunState().completedSteps;
    suite.p.setRunState(r);
    verifier(suite.lancer() && suite.execA == 0 && suite.execB == 1, "reprise : seul b exécuté");
    verifier(suite.y() == VALEUR_A + VALEUR_B, "sortie finale correcte");
}

void testerScript(const fs::path& dossier) {
    std::cout << "Script réécrit après chaque step\n";
    fs::remove_all(dossier);
    Banc b(dossier);
    b.p.setScriptPath(dossier / "run.json");
    b.option(1, "echoue", true);
    b.lancer();
    std::ifstream in(dossier / "run.json");
    const Json script = Json::parse(in, nullptr, false);
    verifier(!script.is_discarded() && script.contains("pipeline") && script.contains("run"), "run.json lisible");
    verifier(!script.is_discarded() && script["run"].value("status", "") == "failed", "statut failed écrit");

    Banc relu(dossier);
    relu.p.configure(script);
    verifier(relu.p.getRunState().completedSteps == std::vector<std::string>{"a"}, "configure() relit le champ run");

    std::cout << "Champ run invalide\n";
    bool leve = false;
    try { PipelineNs::RunState::fromJson({{"status", "bidon"}}); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "statut inconnu refusé");
    leve = false;
    try { PipelineNs::RunState::fromJson({{"skipCompletedSteps", "oui"}}); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "skip non booléen refusé");
}

// Données d'essai de l'aller-retour des types du pipeline.
DataNs::PairMatches appariementDEssai() {
    DataNs::PairMatches pm{0, 1, DataNs::Direction::Bottom, {}};
    pm.featureMatches.pointsInA = {{POINT_A_X, POINT_A_Y}};
    pm.featureMatches.pointsInB = {{POINT_B_X, POINT_B_Y}};
    pm.featureMatches.confidenceScores = {SCORE};
    pm.featureMatches.translationX = TRANSLATION_X;
    pm.featureMatches.translationY = TRANSLATION_Y;
    return pm;
}

DataNs::TileTerritory territoireDEssai() {
    DataNs::TileTerritory territoire;
    territoire.tileIndex = 1;
    territoire.polygons = {{{0, 0}, {COTE_POLYGONE, 0}, {COTE_POLYGONE, COTE_POLYGONE}}};
    territoire.holes = {{{{2, 2}, {3, 2}, {3, 3}}}};
    return territoire;
}

DataNs::Seamline coutureDEssai() {
    return {0, 1, DataNs::Direction::Right, {{POSITION_X, POSITION_Y}, {POSITION_X, 1.0}}};
}

void remplirDonneesDEssai(StepNs::DataStoreClass& avant) {
    auto t0 = std::make_shared<DataNs::TileClass>(0, "a.tif", "", LARGEUR_TUILE, HAUTEUR_TUILE, CV_16UC3);
    auto t1 = std::make_shared<DataNs::TileClass>(1, "b.tif", "", LARGEUR_TUILE, HAUTEUR_TUILE, CV_16UC3);
    t0->setNeighbours(nullptr, t1.get(), nullptr, nullptr);
    t1->setNeighbours(t0.get(), nullptr, nullptr, nullptr);
    avant.replace("tiles", DataNs::TileList{t0, t1});
    avant.replace("matches", std::vector<DataNs::PairMatches>{appariementDEssai()});
    avant.replace("positions", std::vector<DataNs::TilePositionData>{{"r00_c01", {POSITION_X, POSITION_Y}, ROTATION}});
    avant.replace("territories", std::vector<DataNs::TileTerritory>{territoireDEssai()});
    avant.replace("seamlines", DataNs::Seamlines{coutureDEssai()});
    avant.replace("intensityCorrection", DataNs::IntensityCorrection{{GAIN_TUILE_0, GAIN_TUILE_1}, FORCE_CHAMP, {1}});
}

void verifierTuilesEtAppariements(const StepNs::DataStoreClass& apres) {
    const auto& tuiles = apres.get<DataNs::TileList>("tiles");
    verifier(tuiles.size() == 2 && tuiles[0]->getSource() == "a.tif" && tuiles[0]->getType() == CV_16UC3 &&
                 tuiles[0]->getLevel(0).width == LARGEUR_TUILE && tuiles[0]->getRight() == tuiles[1].get() &&
                 tuiles[1]->getLeft() == tuiles[0].get() && tuiles[0]->getLeft() == nullptr,
             "Tiles : fiches et voisins");
    const DataNs::PairMatches attendu = appariementDEssai();
    const auto& m = apres.get<std::vector<DataNs::PairMatches>>("matches");
    verifier(m.size() == 1 && m[0].direction == DataNs::Direction::Bottom && m[0].featureMatches.translationX == attendu.featureMatches.translationX &&
                 m[0].featureMatches.translationY == attendu.featureMatches.translationY && m[0].featureMatches.pointsInA == attendu.featureMatches.pointsInA &&
                 m[0].featureMatches.pointsInB == attendu.featureMatches.pointsInB && m[0].featureMatches.confidenceScores == attendu.featureMatches.confidenceScores,
             "PairMatches : valeurs exactes");
}

void verifierPositionsEtTerritoires(const StepNs::DataStoreClass& apres) {
    const auto& pos = apres.get<std::vector<DataNs::TilePositionData>>("positions");
    verifier(pos.size() == 1 && pos[0].tileId == "r00_c01" && pos[0].position == cv::Point2d(POSITION_X, POSITION_Y) &&
                 pos[0].rotationRadians == ROTATION,
             "Positions : valeurs exactes");
    const DataNs::TileTerritory attendu = territoireDEssai();
    const auto& tr = apres.get<std::vector<DataNs::TileTerritory>>("territories");
    verifier(tr.size() == 1 && tr[0].tileIndex == 1 && tr[0].polygons == attendu.polygons && tr[0].holes == attendu.holes,
             "Territories : polygones et trous");
}

void verifierCouturesEtCorrection(const StepNs::DataStoreClass& apres) {
    const DataNs::Seamline attendue = coutureDEssai();
    const auto& coutures = apres.get<DataNs::Seamlines>("seamlines");
    verifier(coutures.size() == 1 && coutures[0].tileIdA == 0 && coutures[0].tileIdB == 1 &&
                 coutures[0].direction == DataNs::Direction::Right && coutures[0].points == attendue.points,
             "Seamlines : coutures exactes");
    const auto& correction = apres.get<DataNs::IntensityCorrection>("intensityCorrection");
    verifier(correction.tileGains == std::vector<double>{GAIN_TUILE_0, GAIN_TUILE_1} && correction.fieldStrength == FORCE_CHAMP &&
                 correction.isolatedTileIndices == std::vector<int>{1},
             "IntensityCorrection : gains, champ et tuiles isolées");
}

// Aller-retour save → load des types du pipeline (core/CheckpointTypes) sans passer par un step
void testerTypesDuPipeline(const fs::path& dossier) {
    std::cout << "Sérialisation des types du pipeline\n";
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    const auto types = CheckpointNs::pipelineTypes();
    verifier(types.size() == NOMBRE_TYPES_PIPELINE, "6 types déclarés d'office");

    StepNs::DataStoreClass avant;
    StepNs::DataStoreClass apres;
    remplirDonneesDEssai(avant);
    const std::pair<const char*, const char*> cles[] = {
        {"Tiles", "tiles"}, {"PairMatches", "matches"}, {"Positions", "positions"}, {"Territories", "territories"},
        {"Seamlines", "seamlines"}, {"IntensityCorrection", "intensityCorrection"}};
    for (const auto& [type, cle] : cles) {
        verifier(CheckpointNs::CheckpointClass::hasType(type), std::string(type) + " déclaré");
        types.at(type).first(avant, cle, dossier);
        types.at(type).second(apres, cle, dossier);
    }
    verifierTuilesEtAppariements(apres);
    verifierPositionsEtTerritoires(apres);
    verifierCouturesEtCorrection(apres);
}

// Chaque test a son dossier de checkpoints (ctest lance chaque test dans un processus à part, parfois en
// parallèle) et déclare le type "Compteur" dans son processus
struct Reprise : public ::testing::Test {
protected:
    fs::path dossier;
    void SetUp() override {
        declarerCompteur();
        dossier = fs::temp_directory_path() /
                  ("kikirpa_test_reprise_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    }
    void TearDown() override { fs::remove_all(dossier); }
};

TEST_F(Reprise, SansCheckpoint) { testerSansCheckpoint(); }
TEST_F(Reprise, EchecPuisReprise) { testerEchecPuisReprise(dossier); }
TEST_F(Reprise, Pause) { testerPause(dossier); }
TEST_F(Reprise, Script) { testerScript(dossier); }
TEST_F(Reprise, TypesDuPipeline) { testerTypesDuPipeline(dossier); }

}
