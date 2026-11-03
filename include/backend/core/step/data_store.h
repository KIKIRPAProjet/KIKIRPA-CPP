// include/backend/core/step/data_store.h
#pragma once

#include <any>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace StepNs {

    // Casier de données partagé entre les steps du pipeline, indexé par clé (DataSlot::key : "tiles", "matches"...).
    // Un step lit ses entrées et écrit ses sorties ici. Le type est effacé (std::any) : get<T> doit redonner
    // exactement le type rangé.
    class DataStoreClass {

        public:
            // Range une nouvelle donnée : à préférer quand une clé ne doit être produite qu'une fois, car un doublon
            // révèle alors une erreur. Lève std::logic_error si la clé existe déjà.
            template <class T>
            void add(const std::string& key, T data) {
                if (has(key)) throw std::logic_error("DataStore: key '" + key + "' already exists");
                dataByKey[key] = std::move(data);
            }

            // Range une donnée, en écrasant celle de même clé si elle existe : cas d'un step qui met à jour une clé
            // produite plus tôt, ou d'un chargement de checkpoint.
            template <class T>
            void replace(const std::string& key, T data) { dataByKey[key] = std::move(data); }

            // Lit une donnée sans la copier ; la référence reste valable tant que la clé n'est pas remplacée.
            // Lève std::out_of_range si la clé est absente, std::bad_any_cast si le type ne correspond pas.
            template <class T>
            const T& get(const std::string& key) const {
                auto entry = dataByKey.find(key);
                if (entry == dataByKey.end()) throw std::out_of_range("DataStore: no data for key '" + key + "'");
                return std::any_cast<const T&>(entry->second);
            }

            // À tester avant get quand une entrée est facultative.
            bool has(const std::string& key) const { return dataByKey.count(key) > 0; }

        private:
            // clé -> donnée, de n'importe quel type (std::any mémorise le type réel pour any_cast).
            std::map<std::string, std::any> dataByKey;

    };

}
