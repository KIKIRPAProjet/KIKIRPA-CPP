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
#include "core/data/tile.h"
#include "algorithms/seamline/graphcut/GraphCutSeamFinder.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace stitching::tests {

using Json = nlohmann::json;

constexpr int VALEUR_A = 10;
constexpr int VALEUR_B = 5;

// Vérification non bloquante : en cas d'échec, GoogleTest affiche le message
void verifier(bool ok, const std::string& message) {
    EXPECT_TRUE(ok) << message;
}

// Step de test : sortie = valeur (+ entrée si elle existe). Option "echoue" : renvoie false.
class StepCompteur : public StepNs::StepClass {
    int& executions;
    std::string entree, sortie;
    std::function<void()> pendant;   // appelé pendant execute() (ex. demande de pause)
public:
    StepCompteur(const std::string& nom, int valeur, const std::string& entree, const std::string& sortie,
                 int& executions, std::function<void()> pendant = {})
        : executions(executions), entree(entree), sortie(sortie), pendant(std::move(pendant)) {
        ConfigNs::StepConfig cfg;
        cfg.name = nom;
        cfg.parameters = {{"int", "valeur", valeur}, {"bool", "echoue", false}};
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

struct Banc {
    int execA = 0, execB = 0;
    PipelineNs::PipelineClass p;
    Banc(const fs::path& checkpoints, std::function<void()> pendantA = {}) {
        p.addStep(std::make_unique<StepCompteur>("a", VALEUR_A, "", "x", execA, std::move(pendantA)));
        p.addStep(std::make_unique<StepCompteur>("b", VALEUR_B, "x", "y", execB));
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

void testerEchecPuisReprise(const fs::path& dossier) {
    std::cout << "Échec puis reprise\n";
    fs::remove_all(dossier);
    Banc b(dossier);
    b.option(1, "echoue", true);
    verifier(!b.lancer(), "run 1 en échec au step b");
    verifier(b.p.getRunState().status == PipelineNs::RunStatus::Failed, "statut failed");
    verifier(b.p.getRunState().completedSteps == std::vector<std::string>{"a"}, "completedSteps = [a]");
    verifier(fs::exists(dossier / "00_a" / "manifest.json"), "checkpoint de a écrit");

    b.option(1, "echoue", false);
    verifier(b.lancer(), "run 2 réussi");
    verifier(b.execA == 1, "a sauté à la reprise");
    verifier(b.execB == 2, "b refait");
    verifier(b.y() == VALEUR_A + VALEUR_B, "sortie de a restaurée depuis le checkpoint");

    std::cout << "Options modifiées\n";
    b.option(0, "valeur", 20);
    verifier(b.lancer(), "run 3 réussi");
    verifier(b.execA == 2 && b.execB == 3, "a et b refaits (checkpoint de a périmé)");
    verifier(b.y() == 20 + VALEUR_B, "nouvelle valeur prise en compte");

    std::cout << "Tout est déjà fait\n";
    verifier(b.lancer() && b.execA == 2 && b.execB == 3, "aucun step refait");
    verifier(b.y() == 20 + VALEUR_B, "sorties restaurées");

    std::cout << "run.from et skip\n";
    PipelineNs::RunState r = b.p.getRunState();
    r.startFromStep = "b";
    b.p.setRunState(r);
    verifier(b.lancer() && b.execA == 2 && b.execB == 4, "from=b : seul b refait");
    r = b.p.getRunState();
    r.startFromStep = "a";
    b.p.setRunState(r);
    verifier(b.lancer() && b.execA == 3 && b.execB == 5, "from=a : a et b refaits");
    r = b.p.getRunState();
    r.skipCompletedSteps = false;
    b.p.setRunState(r);
    verifier(b.lancer() && b.execA == 4 && b.execB == 6, "skip=false : tout refait");
    verifier(b.p.getRunState().skipCompletedSteps, "consigne skip remise à true après le run");

    r = b.p.getRunState();
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

// Aller-retour save → load des types du pipeline (core/CheckpointTypes) sans passer par un step
void testerTypesDuPipeline(const fs::path& dossier) {
    std::cout << "Sérialisation des types du pipeline\n";
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    const auto types = CheckpointNs::pipelineTypes();
    verifier(types.size() == 4 && CheckpointNs::CheckpointClass::hasType("Tiles") && CheckpointNs::CheckpointClass::hasType("PairMatches") &&
                 CheckpointNs::CheckpointClass::hasType("Positions") && CheckpointNs::CheckpointClass::hasType("Territories"),
             "4 types déclarés d'office");

    StepNs::DataStoreClass avant, apres;
    auto t0 = std::make_shared<DataNs::TileClass>(0, "a.tif", "", 400, 300, CV_16UC3);
    auto t1 = std::make_shared<DataNs::TileClass>(1, "b.tif", "", 400, 300, CV_16UC3);
    t0->setNeighbours(nullptr, t1.get(), nullptr, nullptr);
    t1->setNeighbours(t0.get(), nullptr, nullptr, nullptr);
    avant.replace("tiles", DataNs::TileList{t0, t1});
    DataNs::PairMatches pm{0, 1, DataNs::Direction::Bottom, {}};
    pm.featureMatches.pointsInA = {{1.25f, 2.5f}};
    pm.featureMatches.pointsInB = {{3.125f, 4.f}};
    pm.featureMatches.confidenceScores = {0.75f};
    pm.featureMatches.translationX = 319.875f;
    pm.featureMatches.translationY = -0.03125f;
    avant.replace("matches", std::vector<DataNs::PairMatches>{pm});
    avant.replace("positions", std::vector<DataNs::TilePositionData>{{"r00_c01", {320.123456789, -0.5}, 0.001}});
    DataNs::TileTerritory terr;
    terr.tileIndex = 1;
    terr.polygons = {{{0, 0}, {10, 0}, {10, 10}}};
    terr.holes = {{{{2, 2}, {3, 2}, {3, 3}}}};
    avant.replace("territories", std::vector<DataNs::TileTerritory>{terr});

    const std::pair<const char*, const char*> cles[] = {
        {"Tiles", "tiles"}, {"PairMatches", "matches"}, {"Positions", "positions"}, {"Territories", "territories"}};
    for (const auto& [type, cle] : cles) {
        types.at(type).first(avant, cle, dossier);
        types.at(type).second(apres, cle, dossier);
    }

    const auto& tuiles = apres.get<DataNs::TileList>("tiles");
    verifier(tuiles.size() == 2 && tuiles[0]->getSource() == "a.tif" && tuiles[0]->getType() == CV_16UC3 &&
                 tuiles[0]->getLevel(0).width == 400 && tuiles[0]->getRight() == tuiles[1].get() &&
                 tuiles[1]->getLeft() == tuiles[0].get() && tuiles[0]->getLeft() == nullptr,
             "Tiles : fiches et voisins");
    const auto& m = apres.get<std::vector<DataNs::PairMatches>>("matches");
    verifier(m.size() == 1 && m[0].direction == DataNs::Direction::Bottom && m[0].featureMatches.translationX == pm.featureMatches.translationX &&
                 m[0].featureMatches.translationY == pm.featureMatches.translationY && m[0].featureMatches.pointsInA == pm.featureMatches.pointsInA &&
                 m[0].featureMatches.pointsInB == pm.featureMatches.pointsInB && m[0].featureMatches.confidenceScores == pm.featureMatches.confidenceScores,
             "PairMatches : valeurs exactes");
    const auto& pos = apres.get<std::vector<DataNs::TilePositionData>>("positions");
    verifier(pos.size() == 1 && pos[0].tileId == "r00_c01" && pos[0].position == cv::Point2d(320.123456789, -0.5) &&
                 pos[0].rotationRadians == 0.001,
             "Positions : valeurs exactes");
    const auto& tr = apres.get<std::vector<DataNs::TileTerritory>>("territories");
    verifier(tr.size() == 1 && tr[0].tileIndex == 1 && tr[0].polygons == terr.polygons && tr[0].holes == terr.holes,
             "Territories : polygones et trous");
}

// Chaque test a son dossier de checkpoints (ctest lance chaque test dans un processus à part, parfois en
// parallèle) et déclare le type "Compteur" dans son processus
class Reprise : public ::testing::Test {
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

} // namespace stitching::tests
