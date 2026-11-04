// include/backend/core/memory/memory_scope.h
#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>

namespace MemoryNs {

    using Json = nlohmann::json;

    // Mesure la mémoire résidente (RSS) du processus pendant la durée de vie de l'objet.
    // Fonctionne sous Windows et Linux.
    //
    // Usage : { MemoryNs::MemoryScopeClass scope(resultJson, "name"); ...code mesuré... }
    //
    // À la destruction, écrit targetJson[name] = {beforeMib, peakMib, afterMib, processPeakMib}
    // et affiche un résumé sur la sortie standard.
    //   peakMib        : pic de RSS pendant le bloc (mesuré par un thread d'échantillonnage)
    //   processPeakMib : pic de RSS depuis le lancement du processus
    // Toutes les valeurs sont en MiB, arrondies à 3 décimales.
    class MemoryScopeClass {

        public:
            // Démarre la mesure et le thread d'échantillonnage. Ne lève pas d'exception.
            MemoryScopeClass(Json& targetJson, const std::string& name);

            // Arrête l'échantillonnage et enregistre le résultat.
            ~MemoryScopeClass();

            // Non copiable : l'objet possède un thread.
            MemoryScopeClass(const MemoryScopeClass&) = delete;
            MemoryScopeClass& operator=(const MemoryScopeClass&) = delete;

        private:
            // Met à jour le pic de RSS si la valeur actuelle le dépasse.
            void updatePeak();

            Json& outputJson;
            std::string scopeName;
            std::size_t rssBeforeBytes;
            std::atomic<std::size_t> rssPeakBytes;
            std::atomic<bool> isSampling{true};
            std::thread samplingThread;   // déclaré en dernier : il démarre quand tout le reste est initialisé

    };

}
