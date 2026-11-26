// src/backend/core/io/unicode_path.cpp
#include "core/io/unicode_path.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <tiffio.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace IONs {

    namespace fs = std::filesystem;

    namespace {

        // Vrai si cv::imread et cv::imwrite, qui ne prennent qu'une chaîne étroite, peuvent recevoir ce chemin tel quel.
        // Sous Windows, cette chaîne est dans la page de code du système (Windows-1252 en Belgique) : « œuvre_été »
        // y passe, « 作品 » non (path.string() lève, ou rend des « ? »). Ailleurs, la chaîne étroite est l'UTF-8 du
        // système de fichiers et convient toujours.
        bool hasNarrowSpelling(const fs::path& path) {
#ifdef _WIN32
            try {
                return fs::path(path.string()) == path;
            } catch (const std::exception&) {
                return false;
            }
#else
            static_cast<void>(path);
            return true;
#endif
        }

        // Ouvertures en lecture d'un fichier que quelqu'un remplace au même instant. La publication d'un TIFF
        // passe par un temporaire puis un renommage (core/io/file_replacement.h) : sous Windows, le renommage et
        // l'ouverture se disputent le fichier le temps d'un battement, et l'ouverture échoue alors que le fichier
        // existe et sera là l'instant suivant. Arrive pour de vrai : l'interface réécrit les pyramides en tâche
        // de fond pendant qu'un pipeline les lit. Le renommage étant quasi instantané, deux réessais suffisent.
        constexpr int READ_ATTEMPT_COUNT = 3;
        constexpr std::chrono::milliseconds READ_RETRY_DELAY{4};

        // Un échec d'ouverture peut-il venir d'un conflit d'accès ? Il faut au moins que le fichier existe. On ne
        // cherche pas à en savoir plus : une ouverture ordinaire peut réussir là où libtiff échoue, les deux ne
        // demandant pas les mêmes droits de partage, et se fier à elle laisserait passer la course qu'on veut
        // précisément éviter. Le prix est deux tentatives perdues sur un fichier qui n'est pas un TIFF.
        bool mightBeABusyFile(const fs::path& path) {
            std::error_code errorCode;
            return fs::exists(path, errorCode);
        }

    }

    fs::path pathFromUtf8(const std::string& utf8Path) { return fs::u8path(utf8Path); }

    std::string pathToUtf8(const fs::path& path) {
        const auto utf8Path = path.u8string();   // std::string en C++17, std::u8string en C++20
        return {utf8Path.begin(), utf8Path.end()};
    }

    TIFF* openTiff(const fs::path& path, const char* mode) {
        const bool reading = mode != nullptr && *mode == 'r';
        for (int attempt = 1;; ++attempt) {
#ifdef _WIN32
            TIFF* tiffFile = TIFFOpenW(path.wstring().c_str(), mode);
#else
            TIFF* tiffFile = TIFFOpen(path.c_str(), mode);
#endif
            if (tiffFile != nullptr) return tiffFile;
            // Un fichier absent, illisible pour de bon, ou une ouverture en écriture échouent tout de suite :
            // seul un conflit d'accès mérite d'attendre.
            if (!reading || attempt >= READ_ATTEMPT_COUNT || !mightBeABusyFile(path)) return nullptr;
            std::this_thread::sleep_for(READ_RETRY_DELAY);
        }
    }

    // Le chemin direct est gardé quand il marche : cv::imdecode demande le fichier entier en mémoire en plus de l'image
    // décodée (pour une tuile de 361 Mo, 361 Mo de plus le temps de la lecture). Seuls les chemins que la page de code
    // ne sait pas écrire paient ce coût.
    cv::Mat readImage(const fs::path& path, int flags) {
        if (hasNarrowSpelling(path)) return cv::imread(path.string(), flags);
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        const std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (bytes.empty()) return {};
        return cv::imdecode(bytes, flags);
    }

    bool writeImage(const fs::path& path, const cv::Mat& image) {
        if (hasNarrowSpelling(path)) return cv::imwrite(path.string(), image);
        std::vector<unsigned char> bytes;
        if (!cv::imencode(pathToUtf8(path.extension()), image, bytes)) return false;
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(output);
    }

}
