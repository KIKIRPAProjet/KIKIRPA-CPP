#include <iostream>
#include "steps/ImportStep.h"

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "[TEST] Demarrage du test : ImportStep" << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Configurer l'entree : dossier images/ et regex img_C..._R...
    stitching::steps::ImportInput input;
    input.path = "images/";
    input.format = R"(img_C(\d+)_R(\d+)\.tif)";

    // 2. Instancier le step
    stitching::steps::ImportStep step(input);

    // 3. Execution
    std::cout << "[*] Lecture du dossier : " << input.path << std::endl;
    if (!step.execute()) {
        std::cerr << "[-] ECHEC lors de l'execution du step !" << std::endl;
        std::cerr << "[-] Details erreur : " << step.exportData().dump(2) << std::endl;
        return 1;
    }

    // 4. Verification des resultats
    const auto& output = step.getOutput();
    std::cout << "[+] SUCCES : ImportStep termine sans erreur." << std::endl;
    std::cout << "[+] Nombre de tuiles chargees : " << output.tiles.size() << std::endl;

    for (const auto& tile : output.tiles) {
        std::cout << "    * Fichier : " << tile.path.filename().string()
                  << " | Grille : (col=" << tile.col << ", row=" << tile.row << ")"
                  << " | Dimensions : " << tile.image.cols << "x" << tile.image.rows
                  << " | Canaux : " << tile.image.channels() 
                  << std::endl;
    }

    if (output.tiles.size() == 4) {
        std::cout << "\n[OK] Les 4 tuiles sont chargees et conformes !" << std::endl;
        return 0;
    } else {
        std::cerr << "\n[!] Attention : 4 tuiles etaient attendues, " 
                  << output.tiles.size() << " ont ete trouvees." << std::endl;
        return 2;
    }
}
