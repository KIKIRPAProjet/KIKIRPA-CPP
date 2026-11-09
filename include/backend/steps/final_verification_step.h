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
        std::string inputImagePath;   // TIFF ou JPEG de référence
        std::string outputImagePath;  // TIFF reconstruit
    };

    // Rapport publié par le step dans le DataStore sous la clé verificationReport.
    struct FinalVerificationOutput {
        std::string key = "verificationReport";
        DataNs::VerificationReport value;
        std::string type = "VerificationReport";
    };

    // Compare les dimensions et métadonnées d'une image de référence et de l'image reconstruite.
    // Les différences sont rapportées sans verdict de qualité; les DPI/PPI ne sont pas comparés.
    class FinalVerificationStepClass : public StepNs::StepClass {

        public:
            explicit FinalVerificationStepClass(const FinalVerificationInput& input = {});
            bool execute() override;
            // Empreinte des deux fichiers externes pour invalider le checkpoint lorsqu'ils changent.
            Json externalInputsFingerprint() const override;
            const FinalVerificationOutput& getOutput() const { return output; }

        private:
            FinalVerificationOutput output;

    };

}
