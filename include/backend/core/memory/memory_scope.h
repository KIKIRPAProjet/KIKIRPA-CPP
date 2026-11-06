// include/backend/core/memory/memory_scope.h
#pragma once

#include <nlohmann/json_fwd.hpp>

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
    // À la destruction, écrit targetJson[name] = {beforeMib, peakMib, afterMib, processPeakMib}. Rien n'est affiché :
    // dans un step, passer getResult() en targetJson pour que le logger du pipeline montre les valeurs.
    //   peakMib        : pic de RSS pendant le bloc (mesuré par un thread d'échantillonnage)
    //   processPeakMib : pic de RSS depuis le lancement du processus
    // Garanti : peakMib >= beforeMib, peakMib >= afterMib et processPeakMib >= peakMib.
    // Toutes les valeurs sont en MiB, arrondies à 3 décimales. Sur une plateforme non gérée, elles valent 0.
    // targetJson doit vivre plus longtemps que l'objet.
    class MemoryScopeClass {

        public:
            // Démarre la mesure et le thread d'échantillonnage. Ne lève pas d'exception : si le thread ne peut pas
            // démarrer, le pic se limite aux mesures de début et de fin.
            MemoryScopeClass(Json& targetJson, const std::string& name);

            // Arrête l'échantillonnage et enregistre le résultat. Ne lève pas d'exception.
            ~MemoryScopeClass();

            // Non copiable : l'objet possède un thread.
            MemoryScopeClass(const MemoryScopeClass&) = delete;
            MemoryScopeClass& operator=(const MemoryScopeClass&) = delete;

        private:
            // Met à jour le pic de RSS si rssBytes le dépasse.
            void recordPeak(std::size_t rssBytes);

            // Corps du thread d'échantillonnage : mesure régulièrement jusqu'à l'arrêt demandé par le destructeur.
            void sampleUntilStopped();

            Json& outputJson;
            std::string scopeName;
            std::size_t rssBeforeBytes;
            std::atomic<std::size_t> rssPeakBytes;
            std::atomic<bool> isSampling{true};
            std::thread samplingThread;   // déclaré en dernier : il démarre quand tout le reste est initialisé

    };

}
