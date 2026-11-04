// include/backend/core/io/pyramidal_tiff_reader.h
#pragma once

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <tiffio.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

namespace IONs {

    // Lecteur d'un TIFF pyramidal produit par createPyramidalTiledTiff.
    // Chaque lecture ouvre et ferme le fichier afin que les appels restent indépendants.
    class PyramidalTiffReaderClass {

        public:
            explicit PyramidalTiffReaderClass(std::filesystem::path tiffPath);

            // `region` est exprimée dans le repère du niveau demandé et doit y être entièrement contenue.
            // `output` reçoit exactement ses dimensions et son type.
            bool readRegion(
                int level,
                const cv::Rect& region,
                cv::Mat& output,
                std::string& errorMessage
            ) const;

            // Récupère le profil ICC (si présent) et les échelles de résolution (X et Y).
            // Laisse scaleX et scaleY à 1.0 et iccProfile vide si non trouvés, mais renvoie true.
            bool readMetadata(
                std::vector<uint8_t>& iccProfile,
                double& scaleX,
                double& scaleY,
                std::string& errorMessage
            ) const;

        private:
            std::filesystem::path tiffPath;

            // Méthodes propres à readMetadata
            void extractResolution(TIFF* tiffFile, double& scaleX, double& scaleY) const;
            void extractIccProfile(TIFF* tiffFile, std::vector<uint8_t>& iccProfile) const;
    };

}
