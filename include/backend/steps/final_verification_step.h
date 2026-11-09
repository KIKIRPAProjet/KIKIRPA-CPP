// include/backend/steps/final_verification_step.h
#pragma once

#include "core/data/verification_report.h"
#include "core/step/step.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace StepsNs {

    using Json = nlohmann::json;

    // Chemins des deux images passés au step de vérification.
    struct FinalVerificationInput {
        std::string inputImagePath;   // Chemin de la référence client (TIFF/JPEG).
        std::string outputImagePath;  // Chemin du TIFF reconstruit à contrôler.
    };

    // Rapport publié par le step dans le DataStore sous la clé verificationReport.
    struct FinalVerificationOutput {
        std::string key = "verificationReport"; // Clé stable consommée par le DataStore et le checkpoint.
        DataNs::VerificationReport value;        // Rapport complet exposé par le step.
        std::string type = "VerificationReport"; // Nom logique utilisé par le registre des données.
    };

    // Compare les dimensions et métadonnées d'une image de référence et de l'image reconstruite.
    // Les différences sont rapportées sans verdict de qualité; les DPI/PPI ne sont pas comparés.
    class FinalVerificationStepClass : public StepNs::StepClass {

        public:
            // Crée le step avec les chemins par défaut du constructeur ou ceux fournis par l'appelant.
            explicit FinalVerificationStepClass(const FinalVerificationInput& input = {});

            // Lit les deux fichiers, publie le rapport et retourne false avec une erreur en cas d'échec.
            bool execute() override;

            // Empreinte des deux fichiers externes pour invalider le checkpoint lorsqu'ils changent.
            Json externalInputsFingerprint() const override;

            // Retourne le même objet rapport que celui placé dans le DataStore.
            const FinalVerificationOutput& getOutput() const { return output; }

        private:
            FinalVerificationOutput output;

    };

}
