// src/backend/algorithms/verification/final_verification.cpp
#include "algorithms/verification/final_verification.h"

#include <nlohmann/json.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
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

        constexpr std::size_t JPEG_FRAME_HEADER_SIZE = 6;
        constexpr std::size_t JPEG_LENGTH_FIELD_SIZE = 2;
        constexpr std::size_t JPEG_FRAME_PRECISION_OFFSET = 0;
        constexpr std::size_t JPEG_FRAME_HEIGHT_OFFSET = 1;
        constexpr std::size_t JPEG_FRAME_WIDTH_OFFSET = 3;
        constexpr std::size_t JPEG_FRAME_COMPONENTS_OFFSET = 5;
        constexpr std::size_t JFIF_VERSION_MAJOR_OFFSET = 5;
        constexpr std::size_t JFIF_VERSION_MINOR_OFFSET = 6;
        constexpr std::size_t JFIF_THUMBNAIL_WIDTH_OFFSET = 12;
        constexpr std::size_t JFIF_THUMBNAIL_HEIGHT_OFFSET = 13;
        constexpr std::size_t JFIF_MINIMUM_PAYLOAD_SIZE = 14;
        constexpr std::size_t EXIF_HEADER_OFFSET = 6;
        constexpr std::size_t EXIF_HEADER_SIZE = 8;
        constexpr std::size_t EXIF_ENTRY_SIZE = 12;
        constexpr std::size_t EXIF_ENTRY_VALUE_OFFSET = 8;
        constexpr std::size_t EXIF_INLINE_VALUE_SIZE = 4;
        constexpr std::uint16_t EXIF_TIFF_MAGIC = 42;
        constexpr std::uint16_t EXIF_ASCII_FIELD_TYPE = 2;
        constexpr std::uint16_t EXIF_SHORT_FIELD_TYPE = 3;
        constexpr std::uint16_t EXIF_ORIENTATION_TAG = 0x0112;
        constexpr std::uint16_t EXIF_MAKE_TAG = 0x010F;
        constexpr std::uint16_t EXIF_MODEL_TAG = 0x0110;
        constexpr std::uint16_t EXIF_SOFTWARE_TAG = 0x0131;
        constexpr std::uint16_t EXIF_DATE_TIME_TAG = 0x0132;
        constexpr std::uint16_t EXIF_ARTIST_TAG = 0x013B;
        constexpr std::uint16_t EXIF_COPYRIGHT_TAG = 0x8298;
        constexpr ttag_t TIFF_EXIF_DATETIME_ORIGINAL = 36867;
        constexpr ttag_t TIFF_EXIF_DATETIME_DIGITIZED = 36868;
        constexpr std::uint8_t JPEG_MARKER_PREFIX = 0xFF;
        constexpr std::uint8_t JPEG_START_OF_IMAGE = 0xD8;
        constexpr std::uint8_t JPEG_END_OF_IMAGE = 0xD9;
        constexpr std::uint8_t JPEG_START_OF_SCAN = 0xDA;
        constexpr std::uint8_t JPEG_TEM = 0x01;
        constexpr std::uint8_t JPEG_RESTART_FIRST = 0xD0;
        constexpr std::uint8_t JPEG_RESTART_LAST = 0xD8;
        constexpr std::uint8_t JPEG_SOF_FIRST = 0xC0;
        constexpr std::uint8_t JPEG_SOF_LAST = 0xCF;
        constexpr std::uint8_t JPEG_DHT = 0xC4;
        constexpr std::uint8_t JPEG_JPG = 0xC8;
        constexpr std::uint8_t JPEG_DAC = 0xCC;
        constexpr std::uint8_t JPEG_APP_FIRST = 0xE0;
        constexpr std::uint8_t JPEG_APP_LAST = 0xEF;
        constexpr std::uint8_t JPEG_APP0 = 0xE0;
        constexpr std::uint8_t JPEG_APP1 = 0xE1;
        constexpr std::uint8_t JPEG_APP2 = 0xE2;
        constexpr std::uint8_t JPEG_COMMENT = 0xFE;

        // Définitions de structures pour stocker les métadonnées des images
        struct ImageMetadata {
            std::string format;
            Json properties = Json::object();
            std::vector<std::uint8_t> iccProfile;
            std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> metadataSegments;
            bool hasIccProfile = false;
        };

        struct ComparisonDiagnostics {
            Json differences = Json::array();
            Json warnings = Json::array();
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

        std::unique_ptr<TIFF, decltype(&TIFFClose)> openTiff(const std::filesystem::path& path) {
            std::unique_ptr<TIFF, decltype(&TIFFClose)> tiffFile(TIFFOpen(path.string().c_str(), "r"), TIFFClose);
            if (!tiffFile) throw std::runtime_error("cannot open TIFF: " + path.string());
            return tiffFile;
        }

        void addTiffDimensions(TIFF* tiffFile, const std::filesystem::path& path, Json& properties) {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            const bool dimensionsRead = TIFFGetField(tiffFile, TIFFTAG_IMAGEWIDTH, &width) &&
                                        TIFFGetField(tiffFile, TIFFTAG_IMAGELENGTH, &height);
            if (!dimensionsRead || width == 0 || height == 0)
                throw std::runtime_error("TIFF has missing or invalid dimensions: " + path.string());
            const auto maximumDimension = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
            if (width > maximumDimension || height > maximumDimension)
                throw std::runtime_error("TIFF dimensions exceed the supported report range: " + path.string());
            properties["width"] = width;
            properties["height"] = height;
        }

        void addTiffTechnicalProperties(TIFF* tiffFile, Json& properties) {
            std::uint16_t samplesPerPixel = 1;
            std::uint16_t bitsPerSample = 1;
            std::uint16_t sampleFormat = SAMPLEFORMAT_UINT;
            std::uint16_t photometric = 0;
            std::uint16_t compression = COMPRESSION_NONE;
            std::uint16_t orientation = ORIENTATION_TOPLEFT;
            std::uint16_t planarConfiguration = PLANARCONFIG_CONTIG;
            TIFFGetFieldDefaulted(tiffFile, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
            TIFFGetFieldDefaulted(tiffFile, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
            TIFFGetFieldDefaulted(tiffFile, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
            TIFFGetFieldDefaulted(tiffFile, TIFFTAG_PHOTOMETRIC, &photometric);
            TIFFGetFieldDefaulted(tiffFile, TIFFTAG_COMPRESSION, &compression);
            TIFFGetFieldDefaulted(tiffFile, TIFFTAG_ORIENTATION, &orientation);
            TIFFGetFieldDefaulted(tiffFile, TIFFTAG_PLANARCONFIG, &planarConfiguration);

            properties.update({
                {"bitsPerSample", bitsPerSample},
                {"samplesPerPixel", samplesPerPixel},
                {"sampleFormat", sampleFormat},
                {"photometricInterpretation", photometric},
                {"compression", compression},
                {"orientation", orientation},
                {"planarConfiguration", planarConfiguration}
            });
        }

        void addTiffExtraSamples(TIFF* tiffFile, Json& properties) {
            std::uint16_t extraSampleCount = 0;
            std::uint16_t* extraSamples = nullptr;
            if (TIFFGetField(tiffFile, TIFFTAG_EXTRASAMPLES, &extraSampleCount, &extraSamples)) {
                Json values = Json::array();
                for (std::uint16_t index = 0; index < extraSampleCount; ++index) values.push_back(extraSamples[index]);
                properties["extraSamples"] = std::move(values);
            } else {
                properties["extraSamples"] = Json::array();
            }
        }

        void addTiffTextProperties(TIFF* tiffFile, Json& properties) {
            addTextTag(tiffFile, TIFFTAG_IMAGEDESCRIPTION, "imageDescription", properties);
            addTextTag(tiffFile, TIFFTAG_DOCUMENTNAME, "documentName", properties);
            addTextTag(tiffFile, TIFFTAG_MAKE, "make", properties);
            addTextTag(tiffFile, TIFFTAG_MODEL, "model", properties);
            addTextTag(tiffFile, TIFFTAG_SOFTWARE, "software", properties);
            addTextTag(tiffFile, TIFFTAG_DATETIME, "dateTime", properties);
            addTextTag(tiffFile, TIFFTAG_ARTIST, "artist", properties);
            addTextTag(tiffFile, TIFFTAG_COPYRIGHT, "copyright", properties);
        }

        void addTiffIccProfile(TIFF* tiffFile, ImageMetadata& metadata) {
            std::uint32_t iccSize = 0;
            void* iccBuffer = nullptr;
            if (TIFFGetField(tiffFile, TIFFTAG_ICCPROFILE, &iccSize, &iccBuffer) &&
                iccSize > 0 && iccBuffer != nullptr) {
                const auto* profile = static_cast<const std::uint8_t*>(iccBuffer);
                metadata.iccProfile.assign(profile, profile + iccSize);
                metadata.hasIccProfile = true;
            }
        }

        void addTiffExifProperties(TIFF* tiffFile, Json& properties) {
            toff_t exifOffset = 0;
            const bool exifDirectoryPresent = TIFFGetField(tiffFile, TIFFTAG_EXIFIFD, &exifOffset) != 0;
            properties["exifDirectoryPresent"] = exifDirectoryPresent;
            properties["exifParseStatus"] = exifDirectoryPresent ? "unread" : "absent";
            if (exifDirectoryPresent) {
                if (TIFFReadEXIFDirectory(tiffFile, exifOffset)) {
                    addTextTag(tiffFile, TIFF_EXIF_DATETIME_ORIGINAL, "dateTimeOriginal", properties);
                    addTextTag(tiffFile, TIFF_EXIF_DATETIME_DIGITIZED, "dateTimeDigitized", properties);
                    properties["exifParseStatus"] = "parsed";
                } else {
                    properties["exifParseStatus"] = "invalid";
                }
            }
        }

        ImageMetadata readTiffMetadata(const std::filesystem::path& path) {
            std::unique_ptr<TIFF, decltype(&TIFFClose)> tiffFile = openTiff(path);
            ImageMetadata metadata;
            metadata.format = "TIFF";
            addTiffDimensions(tiffFile.get(), path, metadata.properties);
            addTiffTechnicalProperties(tiffFile.get(), metadata.properties);
            addTiffExtraSamples(tiffFile.get(), metadata.properties);
            addTiffTextProperties(tiffFile.get(), metadata.properties);
            addTiffIccProfile(tiffFile.get(), metadata);
            addTiffExifProperties(tiffFile.get(), metadata.properties);
            return metadata;
        }

        std::uint16_t readBigEndian16(const std::uint8_t* bytes) {
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(bytes[0]) << std::numeric_limits<std::uint8_t>::digits) | bytes[1]);
        }

        bool readExif16(const std::vector<std::uint8_t>& bytes, std::size_t offset, bool littleEndian, std::uint16_t& value) {
            if (offset > bytes.size() || bytes.size() - offset < JPEG_LENGTH_FIELD_SIZE) return false;
            value = littleEndian
                ? static_cast<std::uint16_t>(
                      bytes[offset] |
                      (static_cast<std::uint16_t>(bytes[offset + 1]) << std::numeric_limits<std::uint8_t>::digits))
                : readBigEndian16(bytes.data() + offset);
            return true;
        }

        bool readExif32(const std::vector<std::uint8_t>& bytes, std::size_t offset, bool littleEndian, std::uint32_t& value) {
            constexpr std::size_t UINT32_SIZE = sizeof(std::uint32_t);
            if (offset > bytes.size() || bytes.size() - offset < UINT32_SIZE) return false;
            if (littleEndian) {
                value = static_cast<std::uint32_t>(bytes[offset]) |
                        (static_cast<std::uint32_t>(bytes[offset + 1]) << std::numeric_limits<std::uint8_t>::digits) |
                        (static_cast<std::uint32_t>(bytes[offset + 2]) << (2 * std::numeric_limits<std::uint8_t>::digits)) |
                        (static_cast<std::uint32_t>(bytes[offset + 3]) << (3 * std::numeric_limits<std::uint8_t>::digits));
            } else {
                value = (static_cast<std::uint32_t>(bytes[offset]) << (3 * std::numeric_limits<std::uint8_t>::digits)) |
                        (static_cast<std::uint32_t>(bytes[offset + 1]) << (2 * std::numeric_limits<std::uint8_t>::digits)) |
                        (static_cast<std::uint32_t>(bytes[offset + 2]) << std::numeric_limits<std::uint8_t>::digits) |
                        static_cast<std::uint32_t>(bytes[offset + 3]);
            }
            return true;
        }

        struct ExifEntry {
            std::uint16_t tag = 0;
            std::uint16_t fieldType = 0;
            std::uint32_t valueCount = 0;
            std::size_t offset = 0;
        };

        Json invalidExifMetadata() {
            return {{"orientation", 1}, {"parseStatus", "invalid"}};
        }

        bool readExifEntryHeader(const std::vector<std::uint8_t>& payload, std::size_t entryOffset,
                                 bool littleEndian, ExifEntry& entry) {
            entry.offset = entryOffset;
            return readExif16(payload, entryOffset, littleEndian, entry.tag) &&
                   readExif16(payload, entryOffset + JPEG_LENGTH_FIELD_SIZE, littleEndian, entry.fieldType) &&
                   readExif32(payload, entryOffset + (2 * JPEG_LENGTH_FIELD_SIZE), littleEndian, entry.valueCount) &&
                   entryOffset <= payload.size() && payload.size() - entryOffset >= EXIF_ENTRY_SIZE;
        }

        void addExifOrientation(const std::vector<std::uint8_t>& payload, const ExifEntry& entry,
                                bool littleEndian, Json& metadata) {
            if (entry.tag != EXIF_ORIENTATION_TAG || entry.fieldType != EXIF_SHORT_FIELD_TYPE ||
                entry.valueCount == 0)
                return;

            std::uint16_t orientation = 1;
            if (entry.valueCount <= JPEG_LENGTH_FIELD_SIZE) {
                if (!readExif16(payload, entry.offset + EXIF_ENTRY_VALUE_OFFSET, littleEndian, orientation)) return;
            } else {
                std::uint32_t valueOffset = 0;
                if (!readExif32(payload, entry.offset + EXIF_ENTRY_VALUE_OFFSET, littleEndian, valueOffset) ||
                    !readExif16(payload, EXIF_HEADER_OFFSET + static_cast<std::size_t>(valueOffset),
                                littleEndian, orientation))
                    return;
            }
            metadata["orientation"] = orientation;
        }

        std::string exifTextFieldName(std::uint16_t tag) {
            switch (tag) {
            case EXIF_MAKE_TAG: return "make";
            case EXIF_MODEL_TAG: return "model";
            case EXIF_SOFTWARE_TAG: return "software";
            case EXIF_DATE_TIME_TAG: return "dateTime";
            case EXIF_ARTIST_TAG: return "artist";
            case EXIF_COPYRIGHT_TAG: return "copyright";
            default: return {};
            }
        }

        void addExifTextField(const std::vector<std::uint8_t>& payload, const ExifEntry& entry,
                              bool littleEndian, Json& metadata) {
            const std::string name = exifTextFieldName(entry.tag);
            if (name.empty() || entry.fieldType != EXIF_ASCII_FIELD_TYPE || entry.valueCount == 0) return;

            std::size_t valueOffset = entry.offset + EXIF_ENTRY_VALUE_OFFSET;
            if (entry.valueCount > EXIF_INLINE_VALUE_SIZE) {
                std::uint32_t relativeOffset = 0;
                if (!readExif32(payload, entry.offset + EXIF_ENTRY_VALUE_OFFSET, littleEndian, relativeOffset)) return;
                valueOffset = EXIF_HEADER_OFFSET + static_cast<std::size_t>(relativeOffset);
            }
            if (valueOffset > payload.size() || entry.valueCount > payload.size() - valueOffset) return;
            const auto* text = reinterpret_cast<const char*>(payload.data() + valueOffset);
            const std::size_t valueLength = entry.valueCount;
            const std::size_t textLength = std::find(text, text + valueLength, '\0') - text;
            metadata[name] = std::string(text, textLength);
        }

        bool processExifEntry(const std::vector<std::uint8_t>& payload, std::size_t entryOffset,
                              bool littleEndian, Json& metadata) {
            ExifEntry entry;
            if (!readExifEntryHeader(payload, entryOffset, littleEndian, entry)) return false;
            addExifOrientation(payload, entry, littleEndian, metadata);
            addExifTextField(payload, entry, littleEndian, metadata);
            return true;
        }

        Json readJpegExifMetadata(const std::vector<std::uint8_t>& payload) {
            Json metadata = invalidExifMetadata();
            if (payload.size() < EXIF_HEADER_OFFSET + EXIF_HEADER_SIZE) return metadata;
            const bool littleEndian = payload[EXIF_HEADER_OFFSET] == 'I' && payload[EXIF_HEADER_OFFSET + 1] == 'I';
            const bool bigEndian = payload[EXIF_HEADER_OFFSET] == 'M' && payload[EXIF_HEADER_OFFSET + 1] == 'M';
            if (!littleEndian && !bigEndian) return metadata;

            std::uint16_t magic = 0;
            std::uint32_t firstIfdOffset = 0;
            if (!readExif16(payload, EXIF_HEADER_OFFSET + JPEG_LENGTH_FIELD_SIZE, littleEndian, magic) ||
                magic != EXIF_TIFF_MAGIC ||
                !readExif32(payload, EXIF_HEADER_OFFSET + (2 * JPEG_LENGTH_FIELD_SIZE), littleEndian, firstIfdOffset))
                return metadata;
            const std::size_t ifdOffset = EXIF_HEADER_OFFSET + static_cast<std::size_t>(firstIfdOffset);
            std::uint16_t entryCount = 0;
            if (!readExif16(payload, ifdOffset, littleEndian, entryCount)) return metadata;

            metadata["parseStatus"] = "parsed";
            for (std::size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
                const std::size_t entryOffset = ifdOffset + JPEG_LENGTH_FIELD_SIZE + entryIndex * EXIF_ENTRY_SIZE;
                if (!processExifEntry(payload, entryOffset, littleEndian, metadata)) return invalidExifMetadata();
            }
            return metadata;
        }

        bool isStartOfFrame(std::uint8_t marker) {
            return marker >= JPEG_SOF_FIRST && marker <= JPEG_SOF_LAST && marker != JPEG_DHT &&
                   marker != JPEG_JPG && marker != JPEG_DAC;
        }

        struct JpegMetadataState {
            ImageMetadata imageMetadata;
            Json metadataSegments = Json::array();
            Json properties = {{"orientation", 1}};
            std::map<unsigned int, std::vector<std::uint8_t>> iccChunks;
            unsigned int expectedIccChunks = 0;
        };

        bool readJpegMarker(std::istream& input, std::uint8_t& marker) {
            int prefix = input.get();
            while (prefix != std::char_traits<char>::eof() && prefix != JPEG_MARKER_PREFIX) prefix = input.get();
            if (prefix == std::char_traits<char>::eof()) return false;
            int markerValue = input.get();
            while (markerValue == JPEG_MARKER_PREFIX) markerValue = input.get();
            if (markerValue == std::char_traits<char>::eof()) return false;
            marker = static_cast<std::uint8_t>(markerValue);
            return true;
        }

        std::vector<std::uint8_t> readJpegSegmentPayload(std::istream& input, const std::filesystem::path& path) {
            std::array<std::uint8_t, JPEG_LENGTH_FIELD_SIZE> lengthBytes{};
            if (!input.read(reinterpret_cast<char*>(lengthBytes.data()), static_cast<std::streamsize>(lengthBytes.size())))
                throw std::runtime_error("truncated JPEG segment: " + path.string());
            const std::uint16_t segmentLength = readBigEndian16(lengthBytes.data());
            if (segmentLength < JPEG_LENGTH_FIELD_SIZE)
                throw std::runtime_error("invalid JPEG segment length: " + path.string());
            std::vector<std::uint8_t> payload(segmentLength - JPEG_LENGTH_FIELD_SIZE);
            if (!payload.empty() &&
                !input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size())))
                throw std::runtime_error("truncated JPEG segment: " + path.string());
            return payload;
        }

        void readJpegFrameProperties(std::uint8_t marker, const std::vector<std::uint8_t>& payload,
                                     const std::filesystem::path& path, Json& properties) {
            if (!isStartOfFrame(marker)) return;
            if (payload.size() < JPEG_FRAME_HEADER_SIZE)
                throw std::runtime_error("invalid JPEG frame header: " + path.string());
            const std::uint16_t height = readBigEndian16(payload.data() + JPEG_FRAME_HEIGHT_OFFSET);
            const std::uint16_t width = readBigEndian16(payload.data() + JPEG_FRAME_WIDTH_OFFSET);
            if (width == 0 || height == 0) throw std::runtime_error("JPEG has invalid dimensions: " + path.string());
            properties["width"] = width;
            properties["height"] = height;
            properties["bitsPerSample"] = payload[JPEG_FRAME_PRECISION_OFFSET];
            properties["samplesPerPixel"] = payload[JPEG_FRAME_COMPONENTS_OFFSET];
            properties["compression"] = marker;
        }

        bool collectJpegIccProfile(std::uint8_t marker, const std::vector<std::uint8_t>& payload,
                                   const std::filesystem::path& path, JpegMetadataState& state) {
            constexpr std::array<std::uint8_t, 12> ICC_SIGNATURE = {
                'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0
            };
            const std::size_t chunkIndexOffset = ICC_SIGNATURE.size();
            const std::size_t chunkCountOffset = chunkIndexOffset + 1;
            if (marker != JPEG_APP2 || payload.size() < ICC_SIGNATURE.size() + JPEG_LENGTH_FIELD_SIZE ||
                !std::equal(ICC_SIGNATURE.begin(), ICC_SIGNATURE.end(), payload.begin()))
                return false;

            const unsigned int chunkIndex = payload[chunkIndexOffset];
            const unsigned int chunkCount = payload[chunkCountOffset];
            if (chunkIndex == 0 || chunkCount == 0 || chunkIndex > chunkCount ||
                (state.expectedIccChunks != 0 && state.expectedIccChunks != chunkCount))
                throw std::runtime_error("invalid JPEG ICC profile segments: " + path.string());
            state.expectedIccChunks = chunkCount;
            if (state.iccChunks.count(chunkIndex) != 0)
                throw std::runtime_error("duplicate JPEG ICC profile segment: " + path.string());
            state.iccChunks.emplace(chunkIndex, std::vector<std::uint8_t>(
                payload.begin() + static_cast<std::ptrdiff_t>(chunkCountOffset + 1), payload.end()));
            return true;
        }

        void readJpegExifProperties(std::uint8_t marker, const std::vector<std::uint8_t>& payload,
                                    Json& properties) {
            constexpr std::array<std::uint8_t, EXIF_HEADER_OFFSET> EXIF_SIGNATURE = {'E', 'x', 'i', 'f', 0, 0};
            if (marker != JPEG_APP1 || payload.size() < EXIF_SIGNATURE.size() ||
                !std::equal(EXIF_SIGNATURE.begin(), EXIF_SIGNATURE.end(), payload.begin()))
                return;
            properties["exifPresent"] = true;
            properties["exifPayloadBytes"] = payload.size();
            const Json exifMetadata = readJpegExifMetadata(payload);
            properties["exif"] = exifMetadata;
            for (const char* propertyName : {"orientation", "make", "model", "software", "dateTime", "artist", "copyright"})
                if (exifMetadata.contains(propertyName)) properties[propertyName] = exifMetadata.at(propertyName);
        }

        bool isJfifSegment(std::uint8_t marker, const std::vector<std::uint8_t>& payload) {
            return marker == JPEG_APP0 && payload.size() >= JFIF_MINIMUM_PAYLOAD_SIZE &&
                   payload[0] == 'J' && payload[1] == 'F' && payload[2] == 'I' &&
                   payload[3] == 'F' && payload[4] == 0;
        }

        void readJpegApplicationSegment(std::uint8_t marker, const std::vector<std::uint8_t>& payload,
                                        const std::filesystem::path& path, JpegMetadataState& state) {
            if (marker < JPEG_APP_FIRST || marker > JPEG_APP_LAST) return;
            state.metadataSegments.push_back({{"marker", marker}, {"payloadBytes", payload.size()}});
            const bool jfifSegment = isJfifSegment(marker, payload);
            if (jfifSegment) {
                state.properties["jfifVersion"] = {
                    {"major", payload[JFIF_VERSION_MAJOR_OFFSET]},
                    {"minor", payload[JFIF_VERSION_MINOR_OFFSET]},
                    {"thumbnailWidth", payload[JFIF_THUMBNAIL_WIDTH_OFFSET]},
                    {"thumbnailHeight", payload[JFIF_THUMBNAIL_HEIGHT_OFFSET]}
                };
            }

            readJpegExifProperties(marker, payload, state.properties);
            const bool isIccProfile = collectJpegIccProfile(marker, payload, path, state);
            if (!jfifSegment && !isIccProfile)
                state.imageMetadata.metadataSegments.emplace_back(marker, payload);
        }

        void readJpegComment(std::uint8_t marker, const std::vector<std::uint8_t>& payload,
                             JpegMetadataState& state) {
            if (marker != JPEG_COMMENT) return;
            state.properties["commentBytes"] =
                state.properties.value("commentBytes", std::size_t{0}) + payload.size();
            state.properties["commentCount"] = state.properties.value("commentCount", std::size_t{0}) + 1;
            state.imageMetadata.metadataSegments.emplace_back(marker, payload);
        }

        void collectJpegSegment(std::uint8_t marker, const std::vector<std::uint8_t>& payload,
                                const std::filesystem::path& path, JpegMetadataState& state) {
            readJpegFrameProperties(marker, payload, path, state.properties);
            readJpegApplicationSegment(marker, payload, path, state);
            readJpegComment(marker, payload, state);
        }

        ImageMetadata finishJpegMetadata(const std::filesystem::path& path, JpegMetadataState& state) {
            if (!state.properties.contains("width") || !state.properties.contains("height"))
                throw std::runtime_error("JPEG has no supported frame header: " + path.string());
            if (!state.properties.contains("exifPresent")) {
                state.properties["exifPresent"] = false;
                state.properties["exifPayloadBytes"] = 0;
                state.properties["exif"] = Json();
            }
            state.properties["metadataSegments"] = std::move(state.metadataSegments);

            if (state.expectedIccChunks != 0) {
                if (state.iccChunks.size() != state.expectedIccChunks)
                    throw std::runtime_error("incomplete JPEG ICC profile: " + path.string());
                for (unsigned int index = 1; index <= state.expectedIccChunks; ++index) {
                    const auto chunk = state.iccChunks.find(index);
                    if (chunk == state.iccChunks.end())
                        throw std::runtime_error("incomplete JPEG ICC profile: " + path.string());
                    state.imageMetadata.iccProfile.insert(
                        state.imageMetadata.iccProfile.end(), chunk->second.begin(), chunk->second.end());
                }
                state.imageMetadata.hasIccProfile = !state.imageMetadata.iccProfile.empty();
            }
            state.imageMetadata.format = "JPEG";
            state.imageMetadata.properties = std::move(state.properties);
            return std::move(state.imageMetadata);
        }

        ImageMetadata readJpegMetadata(const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("cannot open JPEG: " + path.string());
            const std::array<std::uint8_t, JPEG_LENGTH_FIELD_SIZE> signature = {
                static_cast<std::uint8_t>(input.get()), static_cast<std::uint8_t>(input.get())
            };
            if (signature[0] != JPEG_MARKER_PREFIX || signature[1] != JPEG_START_OF_IMAGE)
                throw std::runtime_error("invalid JPEG signature: " + path.string());

            JpegMetadataState state;
            std::uint8_t marker = 0;
            while (readJpegMarker(input, marker)) {
                if (marker == JPEG_END_OF_IMAGE || marker == JPEG_START_OF_SCAN) break;
                if (marker == JPEG_TEM || (marker >= JPEG_RESTART_FIRST && marker <= JPEG_RESTART_LAST)) continue;
                collectJpegSegment(marker, readJpegSegmentPayload(input, path), path, state);
            }
            return finishJpegMetadata(path, state);
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

        Json compareJpegMetadataSegment(
            std::size_t index, const ImageMetadata& input, const ImageMetadata& output, Json& differences
        ) {
            const bool inputPresent = index < input.metadataSegments.size();
            const bool outputPresent = index < output.metadataSegments.size();
            const auto inputSegment = inputPresent ? input.metadataSegments[index] :
                std::pair<std::uint8_t, std::vector<std::uint8_t>>{};
            const auto outputSegment = outputPresent ? output.metadataSegments[index] :
                std::pair<std::uint8_t, std::vector<std::uint8_t>>{};
            const bool equal = inputPresent && outputPresent && inputSegment == outputSegment;
            if (!equal) {
                differences.push_back({
                    {"property", "jpegMetadataSegment"},
                    {"index", index},
                    {"inputPresent", inputPresent},
                    {"outputPresent", outputPresent},
                    {"equal", false}
                });
            }
            return {
                {"index", index},
                {"inputPresent", inputPresent},
                {"outputPresent", outputPresent},
                {"inputMarker", inputPresent ? Json(inputSegment.first) : Json()},
                {"outputMarker", outputPresent ? Json(outputSegment.first) : Json()},
                {"inputBytes", inputPresent ? Json(inputSegment.second.size()) : Json()},
                {"outputBytes", outputPresent ? Json(outputSegment.second.size()) : Json()},
                {"equal", equal}
            };
        }

        Json compareJpegMetadataSegments(const ImageMetadata& input, const ImageMetadata& output, Json& differences) {
            Json comparisons = Json::array();
            const std::size_t segmentCount = std::max(input.metadataSegments.size(), output.metadataSegments.size());
            for (std::size_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
                comparisons.push_back(compareJpegMetadataSegment(segmentIndex, input, output, differences));
            return comparisons;
        }

        std::string iccComparisonStatus(bool inputPresent, bool outputPresent, bool identicalBytes) {
            if (!inputPresent && !outputPresent) return "absent_from_both";
            if (inputPresent != outputPresent) return "presence_differs";
            if (identicalBytes) return "identical_profile_data";
            return "colorimetric_equivalence_undetermined";
        }

        Json compareIccProfiles(const ImageMetadata& input, const ImageMetadata& output,
                                ComparisonDiagnostics& diagnostics) {
            const bool identicalBytes = input.hasIccProfile && output.hasIccProfile &&
                                        input.iccProfile == output.iccProfile;
            const std::string comparison =
                iccComparisonStatus(input.hasIccProfile, output.hasIccProfile, identicalBytes);
            if (input.hasIccProfile != output.hasIccProfile ||
                (input.hasIccProfile && output.hasIccProfile && !identicalBytes)) {
                diagnostics.differences.push_back({
                    {"property", "iccProfile"},
                    {"inputPresent", input.hasIccProfile},
                    {"outputPresent", output.hasIccProfile},
                    {"inputBytes", input.iccProfile.size()},
                    {"outputBytes", output.iccProfile.size()},
                    {"comparison", comparison}
                });
            }
            if (!input.hasIccProfile || !output.hasIccProfile)
                diagnostics.warnings.push_back("ICC profile is missing from at least one image");
            else if (!identicalBytes)
                diagnostics.warnings.push_back(
                    "ICC colorimetric equivalence cannot be determined without a color-management engine");

            return {
                {"inputPresent", input.hasIccProfile},
                {"outputPresent", output.hasIccProfile},
                {"inputBytes", input.iccProfile.size()},
                {"outputBytes", output.iccProfile.size()},
                {"identicalBytes", identicalBytes},
                {"colorimetricEquivalence", comparison}
            };
        }

        Json compareDimensions(const ImageMetadata& input, const ImageMetadata& output) {
            const std::int64_t inputWidth = input.properties.at("width").get<std::int64_t>();
            const std::int64_t inputHeight = input.properties.at("height").get<std::int64_t>();
            const std::int64_t outputWidth = output.properties.at("width").get<std::int64_t>();
            const std::int64_t outputHeight = output.properties.at("height").get<std::int64_t>();
            const double inputAspectRatio = static_cast<double>(inputWidth) / static_cast<double>(inputHeight);
            const double outputAspectRatio = static_cast<double>(outputWidth) / static_cast<double>(outputHeight);
            return {
                {"input", {{"width", inputWidth}, {"height", inputHeight}}},
                {"output", {{"width", outputWidth}, {"height", outputHeight}}},
                {"difference", {{"width", outputWidth - inputWidth}, {"height", outputHeight - inputHeight}}},
                {"outputToInputRatio", {
                    {"width", static_cast<double>(outputWidth) / static_cast<double>(inputWidth)},
                    {"height", static_cast<double>(outputHeight) / static_cast<double>(inputHeight)},
                    {"aspectRatio", outputAspectRatio / inputAspectRatio}
                }}
            };
        }

        Json createComparisonReport(const std::filesystem::path& inputPath, const std::filesystem::path& outputPath,
                                   const ImageMetadata& input, const ImageMetadata& output) {
            ComparisonDiagnostics diagnostics;
            Json metadataComparisons = compareMetadata(input, output, diagnostics.differences);
            metadataComparisons["jpegMetadataSegments"] =
                compareJpegMetadataSegments(input, output, diagnostics.differences);
            const Json iccComparison = compareIccProfiles(input, output, diagnostics);
            return {
                {"status", "report_ready"},
                {"inputImage", {{"path", inputPath.string()}, {"format", input.format}, {"metadata", input.properties}}},
                {"outputImage", {{"path", outputPath.string()}, {"format", output.format}, {"metadata", output.properties}}},
                {"dimensions", compareDimensions(input, output)},
                {"metadataComparisons", metadataComparisons},
                {"iccProfile", iccComparison},
                {"differences", diagnostics.differences},
                {"warnings", diagnostics.warnings}
            };
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
        return createComparisonReport(inputImagePath, outputImagePath, inputMetadata, outputMetadata);
    }

}
