// test_reprise.cpp
// Reprise d'un pipeline (core/Pipeline::run, core/Checkpoint, core/RunState) avec des steps de test :
// "a" produit un entier, "b" lui ajoute sa valeur. Chaque step compte ses exécutions, ce qui permet de vérifier
// lesquels sont sautés à la reprise.
//
//   test_reprise
//       Sans checkpoint, échec puis reprise, options modifiées, run.from, skip=false, pause, script réécrit
//       après chaque step, lecture du champ "run". Code de retour 0 si tous les tests passent.

#include "core/Checkpoint.h"
#include "core/CheckpointTypes.h"
#include "core/Config.h"
#include "core/Pipeline.h"
#include "core/Tuile.h"
#include "algorithms/seamline/graphcut/GraphCutSeamFinder.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace stitching::tests {

constexpr int VALEUR_A = 10;
constexpr int VALEUR_B = 5;

int nbEchecs = 0;

void verifier(bool ok, const std::string& message) {
    std::cout << (ok ? "  OK    " : "  ECHEC ") << message << '\n';
    if (!ok) ++nbEchecs;
}

// Step de test : sortie = valeur (+ entrée si elle existe). Option "echoue" : renvoie false.
class StepCompteur : public Step {
    int& executions;
    std::string entree, sortie;
    std::function<void()> pendant;   // appelé pendant execute() (ex. demande de pause)
public:
    StepCompteur(const std::string& nom, int valeur, const std::string& entree, const std::string& sortie,
                 int& executions, std::function<void()> pendant = {})
        : executions(executions), entree(entree), sortie(sortie), pendant(std::move(pendant)) {
        StepConfig cfg;
        cfg.name = nom;
        cfg.options = {{"int", "valeur", valeur}, {"bool", "echoue", false}};
        if (!entree.empty()) cfg.pipelineOptions = {{"Compteur", entree}};
        cfg.signature.pipelineOptions = {{"Compteur", sortie}};
        config = cfg;
    }
    bool execute() override {
        ++executions;
        outputData = Json::object();
        if (option("echoue").get<bool>()) {
            outputData["error"] = "échec demandé";
            return false;
        }
        int v = option("valeur").get<int>();
        if (!entree.empty()) v += data->get<int>(entree);
        data->set(sortie, v);
        outputData["valeur"] = v;
        if (pendant) pendant();
        return true;
    }
};

// Sérialisation du type de test "Compteur" : un entier dans <clé>.txt
void declarerCompteur() {
    Checkpoint::registerType(
        "Compteur",
        [](const DataStore& d, const std::string& cle, const fs::path& dossier) {
            std::ofstream(dossier / (cle + ".txt")) << d.get<int>(cle);
        },
        [](DataStore& d, const std::string& cle, const fs::path& dossier) {
            std::ifstream in(dossier / (cle + ".txt"));
            int v = 0;
            if (!(in >> v)) throw std::runtime_error("lecture impossible");
            d.set(cle, v);
        });
}

struct Banc {
    int execA = 0, execB = 0;
    Pipeline p;
    Banc(const fs::path& checkpoints, std::function<void()> pendantA = {}) {
        p.add(std::make_unique<StepCompteur>("a", VALEUR_A, "", "x", execA, std::move(pendantA)));
        p.add(std::make_unique<StepCompteur>("b", VALEUR_B, "x", "y", execB));
        RunState r;
        r.checkpointDir = checkpoints.string();
        p.setRunState(r);
    }
    // Change une option d'un step (index), comme le ferait un JSON relu
    void option(size_t index, const std::string& cle, Json valeur) {
        const auto& s = p.getSteps()[index];
        s->importConfig({{"name", s->getConfig().at("name")}, {"options", {{{"type", valeur.is_boolean() ? "bool" : "int"}, {"key", cle}, {"value", valeur}}}}});
    }
    bool lancer() { return p.run([](const std::string&) {}); }
    int y() const { return p.getData().has("y") ? p.getData().get<int>("y") : -1; }
};

void testerSansCheckpoint() {
    std::cout << "Sans checkpoint\n";
    Banc b("");
    verifier(b.lancer() && b.lancer(), "deux runs réussis");
    verifier(b.execA == 2 && b.execB == 2, "chaque run refait tous les steps");
    verifier(b.p.getRunState().status == RunStatus::Completed, "statut completed");
    verifier(b.y() == VALEUR_A + VALEUR_B, "sortie finale correcte");
}

void testerEchecPuisReprise(const fs::path& dossier) {
    std::cout << "Échec puis reprise\n";
    fs::remove_all(dossier);
    Banc b(dossier);
    b.option(1, "echoue", true);
    verifier(!b.lancer(), "run 1 en échec au step b");
    verifier(b.p.getRunState().status == RunStatus::Failed, "statut failed");
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
    RunState r = b.p.getRunState();
    r.from = "b";
    b.p.setRunState(r);
    verifier(b.lancer() && b.execA == 2 && b.execB == 4, "from=b : seul b refait");
    r = b.p.getRunState();
    r.from = "a";
    b.p.setRunState(r);
    verifier(b.lancer() && b.execA == 3 && b.execB == 5, "from=a : a et b refaits");
    r = b.p.getRunState();
    r.skip = false;
    b.p.setRunState(r);
    verifier(b.lancer() && b.execA == 4 && b.execB == 6, "skip=false : tout refait");
    verifier(b.p.getRunState().skip, "consigne skip remise à true après le run");

    r = b.p.getRunState();
    r.from = "inconnu";
    b.p.setRunState(r);
    bool leve = false;
    try { b.lancer(); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "from inconnu : std::invalid_argument");

    fs::remove_all(dossier);
    Banc vide(dossier);
    RunState r2 = vide.p.getRunState();
    r2.from = "b";
    vide.p.setRunState(r2);
    leve = false;
    try { vide.lancer(); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "from=b sans checkpoint de a : std::invalid_argument");
}

void testerPause(const fs::path& dossier) {
    std::cout << "Pause\n";
    fs::remove_all(dossier);
    Pipeline* cible = nullptr;
    Banc b(dossier, [&] { cible->requestPause(); });
    cible = &b.p;
    verifier(!b.lancer(), "run interrompu");
    verifier(b.p.getRunState().status == RunStatus::Paused, "statut paused");
    verifier(b.execA == 1 && b.execB == 0, "pause entre a et b");

    Banc suite(dossier);   // nouveau processus : même dossier de checkpoints
    RunState r = suite.p.getRunState();
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
    try { parseRunState({{"status", "bidon"}}); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "statut inconnu refusé");
    leve = false;
    try { parseRunState({{"skip", "oui"}}); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "skip non booléen refusé");
}

// Aller-retour save → load des types du pipeline (core/CheckpointTypes) sans passer par un step
void testerTypesDuPipeline(const fs::path& dossier) {
    std::cout << "Sérialisation des types du pipeline\n";
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    const auto types = pipelineTypes();
    verifier(types.size() == 4 && Checkpoint::hasType("Tiles") && Checkpoint::hasType("PairMatches") &&
                 Checkpoint::hasType("Positions") && Checkpoint::hasType("Territories"),
             "4 types déclarés d'office");

    DataStore avant, apres;
    auto t0 = std::make_shared<Tuile>(0, "a.tif", "", 400, 300, CV_16UC3);
    auto t1 = std::make_shared<Tuile>(1, "b.tif", "", 400, 300, CV_16UC3);
    t0->setVoisins(nullptr, t1.get(), nullptr, nullptr);
    t1->setVoisins(t0.get(), nullptr, nullptr, nullptr);
    avant.set("tiles", Tiles{t0, t1});
    PairMatches pm{0, 1, Direction::Bottom, {}};
    pm.matches.pointsA = {{1.25f, 2.5f}};
    pm.matches.pointsB = {{3.125f, 4.f}};
    pm.matches.scores = {0.75f};
    pm.matches.dx = 319.875f;
    pm.matches.dy = -0.03125f;
    avant.set("matches", std::vector<PairMatches>{pm});
    avant.set("positions", std::vector<TilePositionData>{{"r00_c01", {320.123456789, -0.5}, 0.001}});
    Seamline::TileTerritory terr;
    terr.tile_index = 1;
    terr.polygons = {{{0, 0}, {10, 0}, {10, 10}}};
    terr.holes = {{{{2, 2}, {3, 2}, {3, 3}}}};
    avant.set("territories", std::vector<Seamline::TileTerritory>{terr});

    const std::pair<const char*, const char*> cles[] = {
        {"Tiles", "tiles"}, {"PairMatches", "matches"}, {"Positions", "positions"}, {"Territories", "territories"}};
    for (const auto& [type, cle] : cles) {
        types.at(type).first(avant, cle, dossier);
        types.at(type).second(apres, cle, dossier);
    }

    const auto& tuiles = apres.get<Tiles>("tiles");
    verifier(tuiles.size() == 2 && tuiles[0]->getSource() == "a.tif" && tuiles[0]->getType() == CV_16UC3 &&
                 tuiles[0]->getNiveau(0).largeur == 400 && tuiles[0]->getRight() == tuiles[1].get() &&
                 tuiles[1]->getLeft() == tuiles[0].get() && tuiles[0]->getLeft() == nullptr,
             "Tiles : fiches et voisins");
    const auto& m = apres.get<std::vector<PairMatches>>("matches");
    verifier(m.size() == 1 && m[0].dir == Direction::Bottom && m[0].matches.dx == pm.matches.dx &&
                 m[0].matches.dy == pm.matches.dy && m[0].matches.pointsA == pm.matches.pointsA &&
                 m[0].matches.pointsB == pm.matches.pointsB && m[0].matches.scores == pm.matches.scores,
             "PairMatches : valeurs exactes");
    const auto& pos = apres.get<std::vector<TilePositionData>>("positions");
    verifier(pos.size() == 1 && pos[0].tile_id == "r00_c01" && pos[0].position == cv::Point2d(320.123456789, -0.5) &&
                 pos[0].rotation == 0.001,
             "Positions : valeurs exactes");
    const auto& tr = apres.get<std::vector<Seamline::TileTerritory>>("territories");
    verifier(tr.size() == 1 && tr[0].tile_index == 1 && tr[0].polygons == terr.polygons && tr[0].holes == terr.holes,
             "Territories : polygones et trous");
}

} // namespace stitching::tests

int main() {
    using namespace stitching::tests;
    const fs::path dossier = fs::temp_directory_path() / "kikirpa_test_reprise";
    try {
        declarerCompteur();
        testerSansCheckpoint();
        testerEchecPuisReprise(dossier);
        testerPause(dossier);
        testerScript(dossier);
        testerTypesDuPipeline(dossier);
    } catch (const std::exception& e) {
        std::cout << "  ECHEC exception inattendue : " << e.what() << '\n';
        ++nbEchecs;
    }
    fs::remove_all(dossier);
    std::cout << (nbEchecs == 0 ? "Tous les tests passent\n" : std::to_string(nbEchecs) + " test(s) en échec\n");
    return nbEchecs == 0 ? 0 : 1;
}
