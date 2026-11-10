// include/backend/core/io/tiled_tiff_writer.h
#pragma once

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <tiffio.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace IONs {

    // Écrit une image pleine résolution dans un TIFF tuilé, bloc par bloc, sans jamais l'avoir entière en mémoire.
    // Blocs de DataNs::BLOCK_SIZE pixels de côté, compression LZW, TIFF classique ou BigTIFF selon la taille estimée.
    //
    // Utilisation : open(), writeBlock() une fois par bloc de la grille, dans n'importe quel ordre, puis close().
    // Le fichier est écrit sous un nom temporaire à côté de la destination, et renommé par close() seulement :
    // une destination existante n'est remplacée que par un fichier complet. Si close() n'a pas abouti (erreur, ou
    // objet détruit avant), le fichier temporaire est supprimé.
    //
    // Mémoire : un bloc (BLOCK_SIZE² pixels), quelle que soit la taille de l'image.
    class TiledTiffWriterClass {

        public:
            TiledTiffWriterClass() = default;
            // Ferme le fichier et supprime le temporaire si close() n'a pas réussi.
            ~TiledTiffWriterClass();
            TiledTiffWriterClass(const TiledTiffWriterClass&) = delete;
            TiledTiffWriterClass& operator=(const TiledTiffWriterClass&) = delete;
            TiledTiffWriterClass(TiledTiffWriterClass&&) = delete;
            TiledTiffWriterClass& operator=(TiledTiffWriterClass&&) = delete;

            // Prépare l'écriture de `outputPath`, image de `imageSize` pixels et de type OpenCV `imageType`.
            // Types acceptés : CV_8U ou CV_16U, 1 canal (niveaux de gris) ou 3 canaux, écrits dans l'ordre reçu et
            // déclarés RGB : l'appelant fournit des blocs déjà en RGB (pas en BGR d'OpenCV).
            // Crée le dossier de destination si besoin. Renvoie false avec la cause dans errorMessage.
            bool open(const std::filesystem::path& outputPath, cv::Size imageSize, int imageType,
                      std::string& errorMessage);

            // Écrit le bloc dont le coin haut-gauche est `blockOrigin` (multiple de BLOCK_SIZE, dans l'image).
            // `block` a le type donné à open() et la taille du bloc : BLOCK_SIZE, ou moins sur les bords droit et bas.
            // Un bloc ne peut être écrit qu'une fois. Renvoie false avec la cause dans errorMessage.
            bool writeBlock(cv::Point blockOrigin, const cv::Mat& block, std::string& errorMessage);

            // Termine le fichier et le renomme en `outputPath`. Échoue si un bloc de la grille n'a pas été écrit.
            // Après un échec, le temporaire est supprimé et la destination n'est pas touchée.
            bool close(std::string& errorMessage);

            // Vrai si le fichier est un BigTIFF (taille estimée au-delà de 4 Gio). Connu après open().
            bool isBigTiff() const { return bigTiff; }

        private:
            bool validateBlock(cv::Point blockOrigin, const cv::Mat& block, std::string& errorMessage) const;
            std::size_t blockIndex(cv::Point blockOrigin) const;
            void abandon();

            std::filesystem::path destinationPath;
            std::filesystem::path temporaryPath;   // vide quand aucun fichier temporaire n'est à supprimer
            cv::Size canvasSize;
            int pixelType = -1;
            int columnCount = 0;   // nombre de blocs par ligne de la grille
            bool bigTiff = false;
            std::unique_ptr<TIFF, decltype(&TIFFClose)> tiffFile{nullptr, &TIFFClose};
            cv::Mat tileBuffer;               // un bloc complet : libtiff n'écrit que des blocs entiers
            std::vector<bool> writtenBlocks;  // un indicateur par bloc de la grille, ligne par ligne

    };

}
