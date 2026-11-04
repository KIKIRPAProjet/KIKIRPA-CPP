// src/backend/core/memory/memory_scope.cpp
#include "core/memory/memory_scope.h"

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    // windows.h doit rester avant psapi.h : psapi.h en dépend.
    #include <windows.h>
    #include <psapi.h>
#endif

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
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
        samplingThread = std::thread([this] {
            while (isSampling) {
                updatePeak();
                std::this_thread::sleep_for(SAMPLING_PERIOD);
            }
        });
    }

    MemoryScopeClass::~MemoryScopeClass() {
        isSampling = false;
        samplingThread.join();
        updatePeak();

        const MemoryUsage usageAfter = getCurrentUsage().value_or(MemoryUsage{});
        const double beforeMib = toMebibytes(rssBeforeBytes);
        const double peakMib = toMebibytes(rssPeakBytes.load());
        const double afterMib = toMebibytes(usageAfter.rssBytes);
        const double processPeakMib = toMebibytes(usageAfter.peakRssBytes);

        outputJson[scopeName] = {
            {"beforeMib", beforeMib},
            {"peakMib", peakMib},
            {"afterMib", afterMib},
            {"processPeakMib", processPeakMib}};

        std::cout << "[memory] " << scopeName << ": before " << beforeMib << " MiB, peak "
                  << peakMib << " MiB, after " << afterMib << " MiB" << std::endl;
    }

    void MemoryScopeClass::updatePeak() {
        const std::optional<MemoryUsage> usage = getCurrentUsage();
        if (!usage)
            return;

        std::size_t knownPeakBytes = rssPeakBytes.load();

        // compare_exchange_weak recharge knownPeakBytes quand il échoue : on retente
        // tant que la valeur actuelle reste supérieure au pic enregistré.
        while (usage->rssBytes > knownPeakBytes &&
               !rssPeakBytes.compare_exchange_weak(knownPeakBytes, usage->rssBytes)) {
        }
    }

}
