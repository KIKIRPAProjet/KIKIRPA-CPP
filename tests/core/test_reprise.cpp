// test_reprise.cpp
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
#include "core/pipeline/pipeline.h"
#include "core/pipeline/run_state.h"
#include "core/data/tile.h"
#include "core/step/step.h"

#include <opencv2/core/hal/interface.h>
#include <opencv2/core/types.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

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

namespace fs = std::filesystem; // NOLINT(readability-identifier-naming)

namespace {

using Json = nlohmann::json;

constexpr int VALEUR_A = 10;
constexpr int VALEUR_B = 5;
constexpr int VALEUR_MODIFIEE = 20;
constexpr int EXECUTIONS_A_APRES_DEPART_A = 3;
constexpr int EXECUTIONS_B_APRES_DEPART_A = 5;
constexpr int EXECUTIONS_A_APRES_RECALCUL = 4;
constexpr int EXECUTIONS_B_APRES_RECALCUL = 6;
constexpr int LARGEUR_TUILE = 400;
constexpr int HAUTEUR_TUILE = 300;
constexpr int BORD_TUILE = 10;
constexpr float POINT_A_X = 1.25F;
constexpr float POINT_A_Y = 2.5F;
constexpr float POINT_B_X = 3.125F;
constexpr float POINT_B_Y = 4.F;
constexpr float SCORE_CONFIANCE = 0.75F;
constexpr double TRANSLATION_X = 319.875;
constexpr double TRANSLATION_Y = -0.03125;
constexpr double POSITION_X = 320.123456789;
constexpr double POSITION_Y = -0.5;
constexpr double ROTATION_RADIANS = 0.001;

// Vérification non bloquante : en cas d'échec, GoogleTest affiche le message
void verifier(bool ok, const std::string& message) {
    EXPECT_TRUE(ok) << message;
}

struct StepCompteurConfig {
    std::string name;
    int value;
    std::string input;
    std::string output;
};

// Step de test : sortie = valeur (+ entrée si elle existe). Option "echoue" : renvoie false.
class StepCompteurClass : public StepNs::StepClass {
    int& executions;
    std::string entree, sortie;
    std::function<void()> pendant;   // appelé pendant execute() (ex. demande de pause)

    static ConfigNs::StepConfig makeConfig(const StepCompteurConfig& setup) {
        ConfigNs::StepConfig cfg;
        cfg.name = setup.name;
        cfg.parameters = {{"int", "valeur", setup.value}, {"bool", "echoue", false}};
        if (!setup.input.empty()) cfg.inputs = {{"Compteur", setup.input}};
        cfg.outputs = {{"Compteur", setup.output}};
        return cfg;
    }

public:
    StepCompteurClass(const StepCompteurConfig& setup, int& executions,
                      std::function<void()> pendant = {})
        : executions(executions), entree(setup.input), sortie(setup.output),
          pendant(std::move(pendant)) { configJson = makeConfig(setup); }
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

struct Banc {
    int execA = 0, execB = 0;
    PipelineNs::PipelineClass p;
    Banc(const fs::path& checkpoints, std::function<void()> pendantA = {}) {
        p.addStep(std::make_unique<StepCompteurClass>(
            StepCompteurConfig{"a", VALEUR_A, "", "x"}, execA, std::move(pendantA)));
        p.addStep(std::make_unique<StepCompteurClass>(
            StepCompteurConfig{"b", VALEUR_B, "x", "y"}, execB));
        PipelineNs::RunState r;
        r.checkpointDirectory = checkpoints.string();
        p.setRunState(r);
    }
    // Change une option d'un step (index), comme le ferait un JSON relu
    void option(size_t index, const std::string& cle, Json valeur) {
        const auto& s = p.getSteps()[index];
        s->importConfig({{"name", s->getConfigJson().at("name")}, {"parameters", {{{"valueType", valeur.is_boolean() ? "bool" : "int"}, {"name", cle}, {"value", valeur}}}}});
    }
    bool lancer() { return p.run([](const std::string&) {}); }
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

void testerEchecInitial(Banc& b, const fs::path& dossier) {
    b.option(1, "echoue", true);
    verifier(!b.lancer(), "run 1 en échec au step b");
    verifier(b.p.getRunState().status == PipelineNs::RunStatus::Failed, "statut failed");
    verifier(b.p.getRunState().completedSteps == std::vector<std::string>{"a"}, "completedSteps = [a]");
    verifier(b.p.getRunState().failedStep == "b", "le step en échec est sauvegardé");
    verifier(b.p.getRunState().stepResults.at("a").at("valeur") == VALEUR_A,
             "le résultat du step terminé est sauvegardé");
    verifier(fs::exists(dossier / "00_a" / "manifest.json"), "checkpoint de a écrit");

    b.option(1, "echoue", false);
    verifier(b.lancer(), "run 2 réussi");
    verifier(b.execA == 1, "a sauté à la reprise");
    verifier(b.execB == 2, "b refait");
    verifier(b.y() == VALEUR_A + VALEUR_B, "sortie de a restaurée depuis le checkpoint");
}

void testerOptionsModifieesEtDepart(Banc& b) {
    b.option(0, "valeur", VALEUR_MODIFIEE);
    verifier(b.lancer(), "run 3 réussi");
    verifier(b.execA == 2 && b.execB == 3, "a et b refaits (checkpoint de a périmé)");
    verifier(b.y() == VALEUR_MODIFIEE + VALEUR_B, "nouvelle valeur prise en compte");

    verifier(b.lancer() && b.execA == 2 && b.execB == 3, "aucun step refait");
    verifier(b.y() == VALEUR_MODIFIEE + VALEUR_B, "sorties restaurées");

    PipelineNs::RunState r = b.p.getRunState();
    r.startFromStep = "b";
    b.p.setRunState(r);
    verifier(b.lancer() && b.execA == 2 && b.execB == 4, "from=b : seul b refait");
    r = b.p.getRunState();
    r.startFromStep = "a";
    b.p.setRunState(r);
    verifier(b.lancer() && b.execA == EXECUTIONS_A_APRES_DEPART_A &&
             b.execB == EXECUTIONS_B_APRES_DEPART_A, "from=a : a et b refaits");
    r = b.p.getRunState();
    r.skipCompletedSteps = false;
    b.p.setRunState(r);
    verifier(b.lancer() && b.execA == EXECUTIONS_A_APRES_RECALCUL &&
             b.execB == EXECUTIONS_B_APRES_RECALCUL, "skip=false : tout refait");
    verifier(b.p.getRunState().skipCompletedSteps, "consigne skip remise à true après le run");
}

void testerDepartInvalide(Banc& b, const fs::path& dossier) {
    PipelineNs::RunState r = b.p.getRunState();
    r.startFromStep = "inconnu";
    b.p.setRunState(r);
    bool leve = false;
    try { b.lancer(); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "from inconnu : std::invalid_argument");

    fs::remove_all(dossier);
    Banc vide(dossier);
    PipelineNs::RunState r2 = vide.p.getRunState();
    r2.startFromStep = "b";
    vide.p.setRunState(r2);
    leve = false;
    try { vide.lancer(); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "from=b sans checkpoint de a : std::invalid_argument");
}

void testerEchecPuisReprise(const fs::path& dossier) {
    std::cout << "Échec puis reprise\n";
    fs::remove_all(dossier);
    Banc b(dossier);
    testerEchecInitial(b, dossier);
    testerOptionsModifieesEtDepart(b);
    testerDepartInvalide(b, dossier);
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

void testerCheckpointCorrompu(const fs::path& dossier) {
    std::cout << "Checkpoint de sortie absent\n";
    fs::remove_all(dossier);
    Banc b(dossier);
    verifier(b.lancer(), "premier run terminé");
    fs::remove(dossier / "00_a" / "x.txt");
    verifier(b.lancer(), "la reprise recalcule après une sortie de checkpoint absente");
    verifier(b.execA == 2 && b.execB == 2, "le step corrompu et sa suite sont recalculés");
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

void creerDonneesDeTypes(StepNs::DataStoreClass& donnees) {
    auto t0 = std::make_shared<DataNs::TileClass>(0, "a.tif", "", LARGEUR_TUILE, HAUTEUR_TUILE, CV_16UC3);
    auto t1 = std::make_shared<DataNs::TileClass>(1, "b.tif", "", LARGEUR_TUILE, HAUTEUR_TUILE, CV_16UC3);
    t0->setNeighbours(nullptr, t1.get(), nullptr, nullptr);
    t1->setNeighbours(t0.get(), nullptr, nullptr, nullptr);
    donnees.replace("tiles", DataNs::TileList{t0, t1});
    DataNs::PairMatches pm{0, 1, DataNs::Direction::Bottom, {}};
    pm.featureMatches.pointsInA = {{POINT_A_X, POINT_A_Y}};
    pm.featureMatches.pointsInB = {{POINT_B_X, POINT_B_Y}};
    pm.featureMatches.confidenceScores = {SCORE_CONFIANCE};
    pm.featureMatches.translationX = TRANSLATION_X;
    pm.featureMatches.translationY = TRANSLATION_Y;
    donnees.replace("matches", std::vector<DataNs::PairMatches>{pm});
    donnees.replace("positions", std::vector<DataNs::TilePositionData>{
        {"r00_c01", {POSITION_X, POSITION_Y}, ROTATION_RADIANS}});
    DataNs::TileTerritory territory;
    territory.tileIndex = 1;
    territory.polygons = {{{0, 0}, {BORD_TUILE, 0}, {BORD_TUILE, BORD_TUILE}}};
    territory.holes = {{{{2, 2}, {3, 2}, {3, 3}}}};
    donnees.replace("territories", std::vector<DataNs::TileTerritory>{territory});
}

void serialiserEtRelireTypes(const fs::path& dossier, const StepNs::DataStoreClass& avant,
                            StepNs::DataStoreClass& apres) {
    const auto types = CheckpointNs::pipelineTypes();
    const std::pair<const char*, const char*> cles[] = {
        {"Tiles", "tiles"}, {"PairMatches", "matches"}, {"Positions", "positions"}, {"Territories", "territories"}};
    for (const auto& [type, cle] : cles) {
        types.at(type).first(avant, cle, dossier);
        types.at(type).second(apres, cle, dossier);
    }
}

void verifierTuilesSerialisees(const StepNs::DataStoreClass& donnees) {
    const auto& tuiles = donnees.get<DataNs::TileList>("tiles");
    verifier(tuiles.size() == 2 && tuiles[0]->getSource() == "a.tif" &&
             tuiles[0]->getType() == CV_16UC3 &&
             tuiles[0]->getLevel(0).width == LARGEUR_TUILE &&
             tuiles[0]->getRight() == tuiles[1].get() &&
             tuiles[1]->getLeft() == tuiles[0].get() && tuiles[0]->getLeft() == nullptr,
             "Tiles : fiches et voisins");
}

void verifierCorrespondancesSerialisees(const StepNs::DataStoreClass& donnees) {
    const auto& correspondances = donnees.get<std::vector<DataNs::PairMatches>>("matches");
    const auto& match = correspondances.at(0);
    verifier(correspondances.size() == 1 && match.direction == DataNs::Direction::Bottom &&
             match.featureMatches.translationX == TRANSLATION_X &&
             match.featureMatches.translationY == TRANSLATION_Y &&
             match.featureMatches.pointsInA == std::vector<cv::Point2f>{{POINT_A_X, POINT_A_Y}} &&
             match.featureMatches.pointsInB == std::vector<cv::Point2f>{{POINT_B_X, POINT_B_Y}} &&
             match.featureMatches.confidenceScores == std::vector<float>{SCORE_CONFIANCE},
             "PairMatches : valeurs exactes");
}

void verifierPositionsEtTerritoiresSerialises(const StepNs::DataStoreClass& donnees) {
    const auto& positions = donnees.get<std::vector<DataNs::TilePositionData>>("positions");
    verifier(positions.size() == 1 && positions[0].tileId == "r00_c01" &&
             positions[0].position == cv::Point2d(POSITION_X, POSITION_Y) &&
             positions[0].rotationRadians == ROTATION_RADIANS,
             "Positions : valeurs exactes");
    const auto& territoires = donnees.get<std::vector<DataNs::TileTerritory>>("territories");
    const std::vector<std::vector<cv::Point>> polygonesAttendus = {
        {{0, 0}, {BORD_TUILE, 0}, {BORD_TUILE, BORD_TUILE}}};
    const std::vector<std::vector<std::vector<cv::Point>>> trousAttendus = {
        {{{2, 2}, {3, 2}, {3, 3}}}};
    verifier(territoires.size() == 1 && territoires[0].tileIndex == 1 &&
             territoires[0].polygons == polygonesAttendus &&
             territoires[0].holes == trousAttendus,
             "Territories : polygones et trous");
}

// Aller-retour save → load des types du pipeline sans passer par un step.
void testerTypesDuPipeline(const fs::path& dossier) {
    std::cout << "Sérialisation des types du pipeline\n";
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    const auto types = CheckpointNs::pipelineTypes();
    verifier(types.size() == 4 && CheckpointNs::CheckpointClass::hasType("Tiles") && CheckpointNs::CheckpointClass::hasType("PairMatches") &&
                 CheckpointNs::CheckpointClass::hasType("Positions") && CheckpointNs::CheckpointClass::hasType("Territories"),
             "4 types déclarés d'office");
    StepNs::DataStoreClass avant, apres;
    creerDonneesDeTypes(avant);
    serialiserEtRelireTypes(dossier, avant, apres);
    verifierTuilesSerialisees(apres);
    verifierCorrespondancesSerialisees(apres);
    verifierPositionsEtTerritoiresSerialises(apres);
}

// Chaque test a son dossier de checkpoints (ctest lance chaque test dans un processus à part, parfois en
// parallèle) et déclare le type "Compteur" dans son processus
class RepriseClass : public ::testing::Test {
protected:
    fs::path dossier;
    void SetUp() override {
        declarerCompteur();
        dossier = fs::temp_directory_path() /
                  ("kikirpa_test_reprise_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    }
    void TearDown() override { fs::remove_all(dossier); }
};

TEST_F(RepriseClass, SansCheckpoint) { testerSansCheckpoint(); }
TEST_F(RepriseClass, EchecPuisReprise) { testerEchecPuisReprise(dossier); }
TEST_F(RepriseClass, Pause) { testerPause(dossier); }
TEST_F(RepriseClass, CheckpointCorrompu) { testerCheckpointCorrompu(dossier); }
TEST_F(RepriseClass, Script) { testerScript(dossier); }
TEST_F(RepriseClass, TypesDuPipeline) { testerTypesDuPipeline(dossier); }

}
