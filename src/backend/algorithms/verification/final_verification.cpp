// src/backend/algorithms/verification/final_verification.cpp
#include "algorithms/verification/final_verification.h"

#include <tiffio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace VerificationNs {

    namespace {

        using Json = nlohmann::json;

        // Définitions de structures pour stocker les métadonnées des images
        struct ImageMetadata {
            std::string format;
            Json properties = Json::object();
            std::vector<std::uint8_t> iccProfile;
            std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> metadataSegments;
            bool hasIccProfile = false;
        };


        // Lit les métadonnées d'une image TIFF ou JPEG, en utilisant libtiff pour les TIFF et une lecture manuelle pour les JPEG.
        std::string lowerExtension(const std::filesystem::path& path) {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return extension;
        }

        // Ajoute un tag texte à l'objet JSON des propriétés si le tag est présent dans le fichier TIFF.
        void addTextTag(TIFF* tiffFile, ttag_t tag, const std::string& name, Json& properties) {
            char* value = nullptr;
            if (TIFFGetField(tiffFile, tag, &value) && value != nullptr) properties[name] = value;
        }

        // Lit les métadonnées d'une image TIFF, en utilisant libtiff pour extraire les informations pertinentes.
        ImageMetadata readTiffMetadata(const std::filesystem::path& path) {
            std::unique_ptr<TIFF, decltype(&TIFFClose)> tiffFile(TIFFOpen(path.string().c_str(), "r"), TIFFClose);
            if (tiffFile == nullptr) throw std::runtime_error("cannot open TIFF: " + path.string());

            ImageMetadata metadata;
            metadata.format = "TIFF";
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint16_t samplesPerPixel = 1;
            std::uint16_t bitsPerSample = 1;
            std::uint16_t sampleFormat = SAMPLEFORMAT_UINT;
            std::uint16_t photometric = 0;
            std::uint16_t compression = COMPRESSION_NONE;
            std::uint16_t orientation = ORIENTATION_TOPLEFT;
            std::uint16_t planarConfiguration = PLANARCONFIG_CONTIG;
            bool success =
                TIFFGetField(tiffFile.get(), TIFFTAG_IMAGEWIDTH, &width) &&
                TIFFGetField(tiffFile.get(), TIFFTAG_IMAGELENGTH, &height);
            if (!success || width == 0 || height == 0) {
                throw std::runtime_error("TIFF has missing or invalid dimensions: " + path.string());
            }
            if (width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
                height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                throw std::runtime_error("TIFF dimensions exceed the supported report range: " + path.string());
            }

            TIFFGetFieldDefaulted(tiffFile.get(), TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
            TIFFGetFieldDefaulted(tiffFile.get(), TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
            TIFFGetFieldDefaulted(tiffFile.get(), TIFFTAG_SAMPLEFORMAT, &sampleFormat);
            TIFFGetFieldDefaulted(tiffFile.get(), TIFFTAG_PHOTOMETRIC, &photometric);
            TIFFGetFieldDefaulted(tiffFile.get(), TIFFTAG_COMPRESSION, &compression);
            TIFFGetFieldDefaulted(tiffFile.get(), TIFFTAG_ORIENTATION, &orientation);
            TIFFGetFieldDefaulted(tiffFile.get(), TIFFTAG_PLANARCONFIG, &planarConfiguration);

            metadata.properties = {
                {"width", width},
                {"height", height},
                {"bitsPerSample", bitsPerSample},
                {"samplesPerPixel", samplesPerPixel},
                {"sampleFormat", sampleFormat},
                {"photometricInterpretation", photometric},
                {"compression", compression},
                {"orientation", orientation},
                {"planarConfiguration", planarConfiguration}
            };

            std::uint16_t extraSampleCount = 0;
            std::uint16_t* extraSamples = nullptr;
            if (TIFFGetField(tiffFile.get(), TIFFTAG_EXTRASAMPLES, &extraSampleCount, &extraSamples)) {
                Json values = Json::array();
                for (std::uint16_t index = 0; index < extraSampleCount; ++index) values.push_back(extraSamples[index]);
                metadata.properties["extraSamples"] = std::move(values);
            } else {
                metadata.properties["extraSamples"] = Json::array();
            }

            addTextTag(tiffFile.get(), TIFFTAG_IMAGEDESCRIPTION, "imageDescription", metadata.properties);
            addTextTag(tiffFile.get(), TIFFTAG_DOCUMENTNAME, "documentName", metadata.properties);
            addTextTag(tiffFile.get(), TIFFTAG_MAKE, "make", metadata.properties);
            addTextTag(tiffFile.get(), TIFFTAG_MODEL, "model", metadata.properties);
            addTextTag(tiffFile.get(), TIFFTAG_SOFTWARE, "software", metadata.properties);
            addTextTag(tiffFile.get(), TIFFTAG_DATETIME, "dateTime", metadata.properties);
            addTextTag(tiffFile.get(), TIFFTAG_ARTIST, "artist", metadata.properties);
            addTextTag(tiffFile.get(), TIFFTAG_COPYRIGHT, "copyright", metadata.properties);

            std::uint32_t iccSize = 0;
            void* iccBuffer = nullptr;
            if (TIFFGetField(tiffFile.get(), TIFFTAG_ICCPROFILE, &iccSize, &iccBuffer) &&
                iccSize > 0 && iccBuffer != nullptr) {
                const auto* profile = static_cast<const std::uint8_t*>(iccBuffer);
                metadata.iccProfile.assign(profile, profile + iccSize);
                metadata.hasIccProfile = true;
            }
            toff_t exifOffset = 0;
            const bool exifDirectoryPresent = TIFFGetField(tiffFile.get(), TIFFTAG_EXIFIFD, &exifOffset) != 0;
            metadata.properties["exifDirectoryPresent"] = exifDirectoryPresent;
            metadata.properties["exifParseStatus"] = exifDirectoryPresent ? "unread" : "absent";
            if (exifDirectoryPresent) {
                if (TIFFReadEXIFDirectory(tiffFile.get(), exifOffset)) {
                    addTextTag(tiffFile.get(), static_cast<ttag_t>(36867), "dateTimeOriginal", metadata.properties);
                    addTextTag(tiffFile.get(), static_cast<ttag_t>(36868), "dateTimeDigitized", metadata.properties);
                    metadata.properties["exifParseStatus"] = "parsed";
                } else {
                    metadata.properties["exifParseStatus"] = "invalid";
                }
            }
            return metadata;
        }

        std::uint16_t readBigEndian16(const std::uint8_t* bytes) {
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
        }

        bool readExif16(const std::vector<std::uint8_t>& bytes, std::size_t offset, bool littleEndian, std::uint16_t& value) {
            if (offset > bytes.size() || bytes.size() - offset < 2) return false;
            value = littleEndian
                ? static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8))
                : readBigEndian16(bytes.data() + offset);
            return true;
        }

        bool readExif32(const std::vector<std::uint8_t>& bytes, std::size_t offset, bool littleEndian, std::uint32_t& value) {
            if (offset > bytes.size() || bytes.size() - offset < 4) return false;
            if (littleEndian) {
                value = static_cast<std::uint32_t>(bytes[offset]) |
                        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
                        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
                        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
            } else {
                value = (static_cast<std::uint32_t>(bytes[offset]) << 24) |
                        (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
                        (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
                        static_cast<std::uint32_t>(bytes[offset + 3]);
            }
            return true;
        }

        Json readJpegExifMetadata(const std::vector<std::uint8_t>& payload) {
            constexpr std::size_t TIFF_HEADER_OFFSET = 6;
            Json metadata = {{"orientation", 1}, {"parseStatus", "invalid"}};
            if (payload.size() < TIFF_HEADER_OFFSET + 8) return metadata;
            const bool littleEndian = payload[TIFF_HEADER_OFFSET] == 'I' && payload[TIFF_HEADER_OFFSET + 1] == 'I';
            const bool bigEndian = payload[TIFF_HEADER_OFFSET] == 'M' && payload[TIFF_HEADER_OFFSET + 1] == 'M';
            if (!littleEndian && !bigEndian) return metadata;

            std::uint16_t magic = 0;
            std::uint32_t firstIfdOffset = 0;
            if (!readExif16(payload, TIFF_HEADER_OFFSET + 2, littleEndian, magic) || magic != 42 ||
                !readExif32(payload, TIFF_HEADER_OFFSET + 4, littleEndian, firstIfdOffset))
                return metadata;
            const std::size_t ifdOffset = TIFF_HEADER_OFFSET + static_cast<std::size_t>(firstIfdOffset);
            std::uint16_t entryCount = 0;
            if (!readExif16(payload, ifdOffset, littleEndian, entryCount)) return metadata;

            metadata["parseStatus"] = "parsed";
            for (std::size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
                const std::size_t entryOffset = ifdOffset + 2 + entryIndex * 12;
                std::uint16_t tag = 0;
                std::uint16_t fieldType = 0;
                std::uint32_t valueCount = 0;
                if (!readExif16(payload, entryOffset, littleEndian, tag) ||
                    !readExif16(payload, entryOffset + 2, littleEndian, fieldType) ||
                    !readExif32(payload, entryOffset + 4, littleEndian, valueCount) ||
                    entryOffset > payload.size() || payload.size() - entryOffset < 12)
                    return Json{{"orientation", 1}, {"parseStatus", "invalid"}};

                if (tag == 0x0112 && fieldType == 3 && valueCount > 0) {
                    std::uint16_t orientation = 1;
                    if (valueCount <= 2) {
                        if (!readExif16(payload, entryOffset + 8, littleEndian, orientation)) return metadata;
                    } else {
                        std::uint32_t valueOffset = 0;
                        if (!readExif32(payload, entryOffset + 8, littleEndian, valueOffset) ||
                            !readExif16(payload, TIFF_HEADER_OFFSET + static_cast<std::size_t>(valueOffset),
                                        littleEndian, orientation))
                            return metadata;
                    }
                    metadata["orientation"] = orientation;
                }

                std::string name;
                switch (tag) {
                case 0x010F: name = "make"; break;
                case 0x0110: name = "model"; break;
                case 0x0131: name = "software"; break;
                case 0x0132: name = "dateTime"; break;
                case 0x013B: name = "artist"; break;
                case 0x8298: name = "copyright"; break;
                default: break;
                }
                if (name.empty() || fieldType != 2 || valueCount == 0) continue;

                std::size_t valueOffset = entryOffset + 8;
                if (valueCount > 4) {
                    std::uint32_t relativeOffset = 0;
                    if (!readExif32(payload, entryOffset + 8, littleEndian, relativeOffset)) continue;
                    valueOffset = TIFF_HEADER_OFFSET + static_cast<std::size_t>(relativeOffset);
                }
                if (valueOffset > payload.size() || valueCount > payload.size() - valueOffset) continue;
                const auto* text = reinterpret_cast<const char*>(payload.data() + valueOffset);
                const std::size_t textLength = std::find(text, text + valueCount, '\0') - text;
                metadata[name] = std::string(text, textLength);
            }
            return metadata;
        }

        bool isStartOfFrame(std::uint8_t marker) {
            return marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        }

        ImageMetadata readJpegMetadata(const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("cannot open JPEG: " + path.string());

            const std::array<std::uint8_t, 2> signature = {
                static_cast<std::uint8_t>(input.get()), static_cast<std::uint8_t>(input.get())
            };
            if (signature[0] != 0xFF || signature[1] != 0xD8)
                throw std::runtime_error("invalid JPEG signature: " + path.string());

            ImageMetadata metadata;
            metadata.format = "JPEG";
            Json metadataSegments = Json::array();
            Json properties = {{"orientation", 1}};
            std::map<unsigned int, std::vector<std::uint8_t>> iccChunks;
            unsigned int expectedIccChunks = 0;

            while (input) {
                int prefix = input.get();
                while (prefix != std::char_traits<char>::eof() && prefix != 0xFF) prefix = input.get();
                if (prefix == std::char_traits<char>::eof()) break;
                int markerValue = input.get();
                while (markerValue == 0xFF) markerValue = input.get();
                if (markerValue == std::char_traits<char>::eof()) break;
                const auto marker = static_cast<std::uint8_t>(markerValue);
                if (marker == 0xD9 || marker == 0xDA) break;
                if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) continue;

                std::array<std::uint8_t, 2> lengthBytes{};
                if (!input.read(reinterpret_cast<char*>(lengthBytes.data()), 2))
                    throw std::runtime_error("truncated JPEG segment: " + path.string());
                const std::uint16_t segmentLength = readBigEndian16(lengthBytes.data());
                if (segmentLength < 2) throw std::runtime_error("invalid JPEG segment length: " + path.string());
                std::vector<std::uint8_t> payload(segmentLength - 2);
                if (!payload.empty() && !input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size())))
                    throw std::runtime_error("truncated JPEG segment: " + path.string());

                if (isStartOfFrame(marker)) {
                    if (payload.size() < 6) throw std::runtime_error("invalid JPEG frame header: " + path.string());
                    const std::uint16_t height = readBigEndian16(payload.data() + 1);
                    const std::uint16_t width = readBigEndian16(payload.data() + 3);
                    if (width == 0 || height == 0) throw std::runtime_error("JPEG has invalid dimensions: " + path.string());
                    properties["width"] = width;
                    properties["height"] = height;
                    properties["bitsPerSample"] = payload[0];
                    properties["samplesPerPixel"] = payload[5];
                    properties["compression"] = marker;
                }

                if (marker >= 0xE0 && marker <= 0xEF) {
                    metadataSegments.push_back({{"marker", marker}, {"payloadBytes", payload.size()}});
                    if (marker == 0xE0 && payload.size() >= 14 &&
                        payload[0] == 'J' && payload[1] == 'F' && payload[2] == 'I' &&
                        payload[3] == 'F' && payload[4] == 0) {
                        properties["jfifVersion"] = {
                            {"major", payload[5]}, {"minor", payload[6]},
                            {"thumbnailWidth", payload[12]}, {"thumbnailHeight", payload[13]}
                        };
                    } else if (marker == 0xE0) {
                        metadata.metadataSegments.emplace_back(marker, payload);
                    }
                } else if (marker == 0xFE) {
                    properties["commentBytes"] = properties.value("commentBytes", std::size_t{0}) + payload.size();
                    properties["commentCount"] = properties.value("commentCount", std::size_t{0}) + 1;
                    metadata.metadataSegments.emplace_back(marker, payload);
                }

                constexpr std::array<std::uint8_t, 6> EXIF_SIGNATURE = {'E', 'x', 'i', 'f', 0, 0};
                if (marker == 0xE1 && payload.size() >= EXIF_SIGNATURE.size() &&
                    std::equal(EXIF_SIGNATURE.begin(), EXIF_SIGNATURE.end(), payload.begin())) {
                    properties["exifPresent"] = true;
                    properties["exifPayloadBytes"] = payload.size();
                    const Json exifMetadata = readJpegExifMetadata(payload);
                    properties["exif"] = exifMetadata;
                    for (const char* propertyName : {"orientation", "make", "model", "software", "dateTime", "artist", "copyright"})
                        if (exifMetadata.contains(propertyName)) properties[propertyName] = exifMetadata.at(propertyName);
                }

                constexpr std::array<std::uint8_t, 12> ICC_SIGNATURE = {
                    'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0
                };
                if (marker == 0xE2 && payload.size() >= ICC_SIGNATURE.size() + 2 &&
                    std::equal(ICC_SIGNATURE.begin(), ICC_SIGNATURE.end(), payload.begin())) {
                    const unsigned int chunkIndex = payload[12];
                    const unsigned int chunkCount = payload[13];
                    if (chunkIndex == 0 || chunkCount == 0 || chunkIndex > chunkCount ||
                        (expectedIccChunks != 0 && expectedIccChunks != chunkCount))
                        throw std::runtime_error("invalid JPEG ICC profile segments: " + path.string());
                    expectedIccChunks = chunkCount;
                    if (iccChunks.count(chunkIndex) != 0)
                        throw std::runtime_error("duplicate JPEG ICC profile segment: " + path.string());
                    iccChunks.emplace(chunkIndex, std::vector<std::uint8_t>(
                        payload.begin() + static_cast<std::ptrdiff_t>(ICC_SIGNATURE.size() + 2), payload.end()));
                } else if (marker >= 0xE0 && marker <= 0xEF && marker != 0xE0) {
                    metadata.metadataSegments.emplace_back(marker, payload);
                }
            }

            if (!properties.contains("width") || !properties.contains("height"))
                throw std::runtime_error("JPEG has no supported frame header: " + path.string());
            if (!properties.contains("exifPresent")) {
                properties["exifPresent"] = false;
                properties["exifPayloadBytes"] = 0;
                properties["exif"] = Json();
            }
            properties["metadataSegments"] = std::move(metadataSegments);

            if (expectedIccChunks != 0) {
                if (iccChunks.size() != expectedIccChunks)
                    throw std::runtime_error("incomplete JPEG ICC profile: " + path.string());
                for (unsigned int index = 1; index <= expectedIccChunks; ++index) {
                    const auto chunk = iccChunks.find(index);
                    if (chunk == iccChunks.end()) throw std::runtime_error("incomplete JPEG ICC profile: " + path.string());
                    metadata.iccProfile.insert(metadata.iccProfile.end(), chunk->second.begin(), chunk->second.end());
                }
                metadata.hasIccProfile = !metadata.iccProfile.empty();
            }
            metadata.properties = std::move(properties);
            return metadata;
        }

        ImageMetadata readImageMetadata(const std::filesystem::path& path) {
            const std::string extension = lowerExtension(path);
            if (extension == ".tif" || extension == ".tiff") return readTiffMetadata(path);
            if (extension == ".jpg" || extension == ".jpeg") return readJpegMetadata(path);
            throw std::runtime_error("unsupported image format: " + path.string());
        }

        Json compareMetadata(const ImageMetadata& input, const ImageMetadata& output, Json& differences) {
            Json comparisons = Json::object();
            std::set<std::string> propertyNames;
            for (auto property = input.properties.begin(); property != input.properties.end(); ++property)
                propertyNames.insert(property.key());
            for (auto property = output.properties.begin(); property != output.properties.end(); ++property)
                propertyNames.insert(property.key());

            for (const std::string& name : propertyNames) {
                if (name == "metadataSegments") continue;
                const Json inputValue = input.properties.contains(name) ? input.properties.at(name) : Json();
                const Json outputValue = output.properties.contains(name) ? output.properties.at(name) : Json();
                const bool equal = inputValue == outputValue;
                comparisons[name] = {{"input", inputValue}, {"output", outputValue}, {"equal", equal}};
                if (!equal) differences.push_back({{"property", name}, {"input", inputValue}, {"output", outputValue}});
            }
            return comparisons;
        }

        Json compareJpegMetadataSegments(const ImageMetadata& input, const ImageMetadata& output, Json& differences) {
            Json comparisons = Json::array();
            const std::size_t segmentCount = std::max(input.metadataSegments.size(), output.metadataSegments.size());
            for (std::size_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
                const bool inputPresent = segmentIndex < input.metadataSegments.size();
                const bool outputPresent = segmentIndex < output.metadataSegments.size();
                const auto inputSegment = inputPresent ? input.metadataSegments[segmentIndex] :
                    std::pair<std::uint8_t, std::vector<std::uint8_t>>{};
                const auto outputSegment = outputPresent ? output.metadataSegments[segmentIndex] :
                    std::pair<std::uint8_t, std::vector<std::uint8_t>>{};
                const bool equal = inputPresent && outputPresent && inputSegment == outputSegment;
                comparisons.push_back({
                    {"index", segmentIndex},
                    {"inputPresent", inputPresent},
                    {"outputPresent", outputPresent},
                    {"inputMarker", inputPresent ? Json(inputSegment.first) : Json()},
                    {"outputMarker", outputPresent ? Json(outputSegment.first) : Json()},
                    {"inputBytes", inputPresent ? Json(inputSegment.second.size()) : Json()},
                    {"outputBytes", outputPresent ? Json(outputSegment.second.size()) : Json()},
                    {"equal", equal}
                });
                if (!equal) {
                    differences.push_back({
                        {"property", "jpegMetadataSegment"},
                        {"index", segmentIndex},
                        {"inputPresent", inputPresent},
                        {"outputPresent", outputPresent},
                        {"equal", false}
                    });
                }
            }
            return comparisons;
        }

    }

    nlohmann::json compareImages(
        const std::filesystem::path& inputImagePath,
        const std::filesystem::path& outputImagePath
    ) {
        const std::string outputExtension = lowerExtension(outputImagePath);
        if (outputExtension != ".tif" && outputExtension != ".tiff")
            throw std::runtime_error("reconstructed image must be a TIFF: " + outputImagePath.string());
        const ImageMetadata inputMetadata = readImageMetadata(inputImagePath);
        const ImageMetadata outputMetadata = readImageMetadata(outputImagePath);
        const std::int64_t inputWidth = inputMetadata.properties.at("width").get<std::int64_t>();
        const std::int64_t inputHeight = inputMetadata.properties.at("height").get<std::int64_t>();
        const std::int64_t outputWidth = outputMetadata.properties.at("width").get<std::int64_t>();
        const std::int64_t outputHeight = outputMetadata.properties.at("height").get<std::int64_t>();

        Json differences = Json::array();
        Json metadataComparisons = compareMetadata(inputMetadata, outputMetadata, differences);
        metadataComparisons["jpegMetadataSegments"] =
            compareJpegMetadataSegments(inputMetadata, outputMetadata, differences);
        const bool sameIccPresence = inputMetadata.hasIccProfile == outputMetadata.hasIccProfile;
        const bool identicalIccBytes = inputMetadata.hasIccProfile && outputMetadata.hasIccProfile &&
                                       inputMetadata.iccProfile == outputMetadata.iccProfile;
        std::string iccComparison;
        if (!inputMetadata.hasIccProfile && !outputMetadata.hasIccProfile) {
            iccComparison = "absent_from_both";
        } else if (!sameIccPresence) {
            iccComparison = "presence_differs";
        } else if (identicalIccBytes) {
            iccComparison = "identical_profile_data";
        } else {
            iccComparison = "colorimetric_equivalence_undetermined";
        }
        if (!sameIccPresence || (inputMetadata.hasIccProfile && outputMetadata.hasIccProfile && !identicalIccBytes)) {
            differences.push_back({
                {"property", "iccProfile"},
                {"inputPresent", inputMetadata.hasIccProfile},
                {"outputPresent", outputMetadata.hasIccProfile},
                {"inputBytes", inputMetadata.iccProfile.size()},
                {"outputBytes", outputMetadata.iccProfile.size()},
                {"comparison", iccComparison}
            });
        }

        Json warnings = Json::array();
        if (!inputMetadata.hasIccProfile || !outputMetadata.hasIccProfile)
            warnings.push_back("ICC profile is missing from at least one image");
        if (inputMetadata.hasIccProfile && outputMetadata.hasIccProfile && !identicalIccBytes)
            warnings.push_back("ICC colorimetric equivalence cannot be determined without a color-management engine");

        const double inputAspectRatio = static_cast<double>(inputWidth) / inputHeight;
        const double outputAspectRatio = static_cast<double>(outputWidth) / outputHeight;
        return {
            {"status", "report_ready"},
            {"inputImage", {{"path", inputImagePath.string()}, {"format", inputMetadata.format},
                            {"metadata", inputMetadata.properties}}},
            {"outputImage", {{"path", outputImagePath.string()}, {"format", outputMetadata.format},
                             {"metadata", outputMetadata.properties}}},
            {"dimensions", {
                {"input", {{"width", inputWidth}, {"height", inputHeight}}},
                {"output", {{"width", outputWidth}, {"height", outputHeight}}},
                {"difference", {{"width", outputWidth - inputWidth}, {"height", outputHeight - inputHeight}}},
                {"outputToInputRatio", {{"width", static_cast<double>(outputWidth) / inputWidth},
                                        {"height", static_cast<double>(outputHeight) / inputHeight},
                                        {"aspectRatio", outputAspectRatio / inputAspectRatio}}}
            }},
            {"metadataComparisons", metadataComparisons},
            {"iccProfile", {
                {"inputPresent", inputMetadata.hasIccProfile},
                {"outputPresent", outputMetadata.hasIccProfile},
                {"inputBytes", inputMetadata.iccProfile.size()},
                {"outputBytes", outputMetadata.iccProfile.size()},
                {"identicalBytes", identicalIccBytes},
                {"colorimetricEquivalence", iccComparison}
            }},
            {"differences", differences},
            {"warnings", warnings}
        };
    }

}
