// include/Backend/core/step/data_store.h
#pragma once

#include <any>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace StepNs {

    // Données qui circulent entre les steps, rangées par clé de PipelineOption ("tiles", "matches"...).
    // Un step lit ses pipelineOptions ici et y écrit les clés de sa signature.
    class DataStoreClass {

        public:
            template <class T>
            void add(const std::string& key, T data) {
                if (has(key)) throw std::logic_error("DataStore: key '" + key + "' already exists");
                dataByKey[key] = std::move(data);
            }

            template <class T>
            void replace(const std::string& key, T data) { dataByKey[key] = std::move(data); }

            // Lève std::out_of_range si la clé est absente, std::bad_any_cast si le type ne correspond pas
            template <class T>
            const T& get(const std::string& key) const {
                auto entry = dataByKey.find(key);
                if (entry == dataByKey.end()) throw std::out_of_range("DataStore: no data for key '" + key + "'");
                return std::any_cast<const T&>(entry->second);
            }

            bool has(const std::string& key) const { return dataByKey.count(key) > 0; }

        private:
            std::map<std::string, std::any> dataByKey;

    };

}
