// include/backend/core/io/tile_pixel_reader.h
#pragma once

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <filesystem>
#include <string>

namespace IONs {

    // Ce que la sonde apprend d'une image source, en ne lisant que ses tags.
    struct SourceRegionSupport {
        bool servesRegionsCheaply = false;   // une région se lit sans décoder toute l'image
        int width = 0;
        int height = 0;
    };

    // Lit les tags de `sourcePath` pour savoir si une région de sa pleine résolution peut être lue à bon compte :
    //   - TIFF tuilé : oui, une tuile suffit ;
    //   - TIFF à bandes non compressé, canaux entrelacés, ordre des octets de la machine : oui, par décalage ;
    //   - TIFF à bandes courtes : oui, les bandes recoupées sont décodées ;
    //   - une seule bande compressée, canaux séparés, modèle colorimétrique autre que gris ou RGB, ou pas un TIFF
    //     lisible (JPEG, PNG) : non, il faut garder un niveau 0 dans le cache.
    // « Oui » implique que PyramidalTiffReaderClass sait lire ce fichier : profondeur, nombre de canaux et format
    // d'échantillon sont vérifiés en même temps. Ne lève jamais ; `width` et `height` ne sont remplis qu'en cas de oui.
    SourceRegionSupport probeSource(const std::filesystem::path& sourcePath);

    // Tous les pixels du niveau `level` d'une tuile, **en RGB**, pris au meilleur endroit : dans le cache quand la
    // tuile en a un (au niveau 1, 145 Mio au lieu des 578 de la source d'une tuile du KIKIRPA, et aucune
    // réduction en mémoire), sinon dans l'image de l'œuvre, réduite ici autant de fois que nécessaire.
    // `pyramidPath` vide : une tuile sans cache, ce que font les tests qui fabriquent leurs tuiles à la main.
    // Image vide si la lecture échoue. Réduire puis échanger les canaux, ou l'inverse, donne le même résultat
    // (INTER_AREA moyenne canal par canal) : les deux chemins rendent donc exactement les mêmes pixels.
    cv::Mat readTileLevel(const std::filesystem::path& sourcePath, const std::filesystem::path& pyramidPath,
                          int level);

    // Pixels d'une tuile, où qu'ils soient : le niveau 0 dans l'image de l'œuvre quand celle-ci sait servir une
    // région (le cache ne garde alors que les niveaux réduits, soit 27 % de sa taille), sinon dans le cache ; les
    // niveaux réduits toujours dans le cache.
    //
    // Les niveaux sont numérotés comme ceux de la pyramide (0 = pleine résolution, 1 = moitié...), que le fichier de
    // cache les contienne tous ou non : la traduction vers le niveau du fichier est la raison d'être de cette classe.
    // Tout appelant qui compte les niveaux à la main doit passer par elle.
    //
    // Même contrat que PyramidalTiffReaderClass pour chaque méthode ; chaque lecture ouvre et ferme son fichier.
    class TilePixelReaderClass {

        public:
            // Ouvre le cache pour savoir où vit le niveau 0 (voir decideLevelZeroFromSource). `sourcePath` vide :
            // tout vient du cache.
            TilePixelReaderClass(std::filesystem::path sourcePath, std::filesystem::path pyramidPath);
            // Même chose avec la réponse déjà connue, pour ne pas rouvrir le cache à chaque région.
            TilePixelReaderClass(std::filesystem::path sourcePath, std::filesystem::path pyramidPath,
                                 bool levelZeroInSource);

            bool readRegion(int level, const cv::Rect& region, cv::Mat& output, std::string& errorMessage) const;
            bool readLevel(int level, cv::Mat& output, std::string& errorMessage) const;
            bool getLevelDimensions(int level, int& width, int& height, std::string& errorMessage) const;
            // Niveaux de la pyramide, que le cache les contienne tous ou non.
            int getLevelCount() const;
            bool readsLevelZeroFromSource() const { return levelZeroFromSource; }

        private:
            // Fichier et niveau à lire pour le niveau `level` de la pyramide.
            std::filesystem::path fileOf(int level) const;
            int fileLevelOf(int level) const;
            bool decideLevelZeroFromSource() const;

            std::filesystem::path sourcePath;
            std::filesystem::path pyramidPath;
            bool levelZeroFromSource = false;
    };

}
