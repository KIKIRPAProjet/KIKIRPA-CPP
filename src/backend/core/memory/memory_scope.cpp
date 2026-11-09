// src/backend/core/memory/memory_scope.cpp
#include "core/memory/memory_scope.h"

#include <nlohmann/json.hpp>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    // windows.h doit rester avant psapi.h : psapi.h en dépend.
    #include <windows.h>
    #include <psapi.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace MemoryNs {

    namespace {

        // Pause entre deux mesures du thread d'échantillonnage.
        constexpr std::chrono::milliseconds SAMPLING_PERIOD{5};

        constexpr double BYTES_PER_MIB = 1024.0 * 1024.0;

        // Les valeurs en MiB sont arrondies à 3 décimales.
        constexpr double MIB_DECIMALS_FACTOR = 1000.0;

        struct MemoryUsage {
            std::size_t rssBytes = 0;
            std::size_t peakRssBytes = 0;
        };

#if defined(__linux__)
        constexpr std::size_t BYTES_PER_KIB = 1024;
        constexpr std::string_view RSS_PREFIX = "VmRSS:";
        constexpr std::string_view PEAK_RSS_PREFIX = "VmHWM:";

        // /proc/self/status exprime ces valeurs en kio, par exemple "VmRSS:   1234 kB".
        std::size_t parseKibibytesAsBytes(const std::string& line, std::size_t prefixLength) {
            const unsigned long long kibibytes =
                std::strtoull(line.c_str() + prefixLength, nullptr, 10);
            return static_cast<std::size_t>(kibibytes) * BYTES_PER_KIB;
        }
#endif

        // Renvoie std::nullopt si la mesure n'est pas disponible (échec système ou plateforme non gérée).
        std::optional<MemoryUsage> getCurrentUsage() {
#if defined(_WIN32)
            PROCESS_MEMORY_COUNTERS counters{};
            if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
                return std::nullopt;

            MemoryUsage usage;
            usage.rssBytes = static_cast<std::size_t>(counters.WorkingSetSize);
            usage.peakRssBytes = static_cast<std::size_t>(counters.PeakWorkingSetSize);
            return usage;
#elif defined(__linux__)
            std::ifstream statusFile("/proc/self/status");
            if (!statusFile)
                return std::nullopt;

            MemoryUsage usage;
            std::string line;
            while (std::getline(statusFile, line)) {
                if (line.rfind(RSS_PREFIX, 0) == 0)
                    usage.rssBytes = parseKibibytesAsBytes(line, RSS_PREFIX.size());
                else if (line.rfind(PEAK_RSS_PREFIX, 0) == 0)
                    usage.peakRssBytes = parseKibibytesAsBytes(line, PEAK_RSS_PREFIX.size());
            }
            return usage;
#else
            return std::nullopt;
#endif
        }

        double toMebibytes(std::size_t bytes) {
            const double mebibytes = static_cast<double>(bytes) / BYTES_PER_MIB;
            return std::round(mebibytes * MIB_DECIMALS_FACTOR) / MIB_DECIMALS_FACTOR;
        }

    }

    MemoryScopeClass::MemoryScopeClass(Json& targetJson, const std::string& name)
        : outputJson(targetJson),
          scopeName(name),
          rssBeforeBytes(getCurrentUsage().value_or(MemoryUsage{}).rssBytes),
          rssPeakBytes(rssBeforeBytes) {
        try {
            samplingThread = std::thread(&MemoryScopeClass::sampleUntilStopped, this);
        } catch (const std::system_error&) {
            isSampling = false;   // pas de thread : le pic se limite aux mesures de début et de fin
        }
    }

    MemoryScopeClass::~MemoryScopeClass() {
        isSampling = false;
        if (samplingThread.joinable()) samplingThread.join();
        try {
            // Une seule lecture finale, qui compte aussi pour le pic : sinon la RSS peut grimper entre deux lectures
            // et donner peak < after.
            const MemoryUsage usageAfter = getCurrentUsage().value_or(MemoryUsage{});
            recordPeak(usageAfter.rssBytes);
            const std::size_t peakBytes = rssPeakBytes.load();
            // Sous Linux, VmRSS est approché (compteurs par CPU) et VmHWM mis à jour en retard : un échantillon peut
            // dépasser le VmHWM lu ensuite. Le pic du processus ne peut pas être inférieur au pic du bloc.
            const std::size_t processPeakBytes = std::max(usageAfter.peakRssBytes, peakBytes);
            outputJson[scopeName] = {
                {"beforeMib", toMebibytes(rssBeforeBytes)},
                {"peakMib", toMebibytes(peakBytes)},
                {"afterMib", toMebibytes(usageAfter.rssBytes)},
                {"processPeakMib", toMebibytes(processPeakBytes)}};
        } catch (const std::exception&) {   // NOLINT(bugprone-empty-catch) : un destructeur ne doit pas lever ;
        }                                    // la mesure est alors simplement absente du JSON
    }

    void MemoryScopeClass::sampleUntilStopped() {
        while (isSampling) {
            if (const std::optional<MemoryUsage> usage = getCurrentUsage())
                recordPeak(usage->rssBytes);
            std::this_thread::sleep_for(SAMPLING_PERIOD);
        }
    }

    void MemoryScopeClass::recordPeak(std::size_t rssBytes) {
        std::size_t knownPeakBytes = rssPeakBytes.load();

        // compare_exchange_weak recharge knownPeakBytes quand il échoue : on retente
        // tant que la valeur actuelle reste supérieure au pic enregistré.
        while (rssBytes > knownPeakBytes && !rssPeakBytes.compare_exchange_weak(knownPeakBytes, rssBytes)) {
        }
    }

}
