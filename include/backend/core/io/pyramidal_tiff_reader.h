// include/backend/core/io/pyramidal_tiff_reader.h
#pragma once

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <filesystem>
#include <string>

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

        private:
            std::filesystem::path tiffPath;
    };

}
