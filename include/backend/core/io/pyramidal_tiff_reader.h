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

    struct PyramidViewportRequest {
        cv::Point2d origin;
        cv::Size viewportSize;
        double zoomPercent = 100.0;
    };

    struct PyramidViewportRegion {
        int level = 0;                           // niveau lu, 0 = résolution d'origine
        cv::Rect region;                         // rectangle lu dans les coordonnées de `level`
        cv::Rect2d fullResolutionRegion;         // rectangle couvert, dans le niveau 0
        cv::Mat pixels;                          // pixels correspondant à `region`
    };

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

            // Dimensions des niveaux présents dans le TIFF, du niveau 0 aux niveaux réduits.
            bool readPyramidDimensions(
                std::vector<cv::Size>& dimensions,
                std::string& errorMessage
            ) const;

            // Lit la zone visible selon une origine pleine résolution, une taille écran et un
            // facteur de zoom (100 = 1 pixel source par pixel écran). Les parties hors image
            // sont tronquées.
            bool readViewport(
                const PyramidViewportRequest& request,
                PyramidViewportRegion& output,
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
