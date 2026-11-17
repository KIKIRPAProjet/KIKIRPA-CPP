// src/backend/core/io/unicode_path.cpp
#include "core/io/unicode_path.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <tiffio.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
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

    }

    fs::path pathFromUtf8(const std::string& utf8Path) { return fs::u8path(utf8Path); }

    std::string pathToUtf8(const fs::path& path) {
        const auto utf8Path = path.u8string();   // std::string en C++17, std::u8string en C++20
        return {utf8Path.begin(), utf8Path.end()};
    }

    TIFF* openTiff(const fs::path& path, const char* mode) {
#ifdef _WIN32
        return TIFFOpenW(path.wstring().c_str(), mode);
#else
        return TIFFOpen(path.c_str(), mode);
#endif
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
