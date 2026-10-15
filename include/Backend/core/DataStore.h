#pragma once
#include <any>
#include <map>
#include <stdexcept>
#include <string>

namespace stitching {

// Données qui circulent entre les steps, rangées par clé de PipelineOption ("tiles", "matches"...).
// Un step lit ses pipelineOptions ici et y écrit les clés de sa signature.
// Un step peut écrire : data->set("tiles", output.tiles) ;
// Un autre peut lire : data->get<Tiles>("tiles").
class DataStore {
    std::map<std::string, std::any> values;
public:
    template <class T>
    void set(const std::string& key, T value) { values[key] = std::move(value); }

    // Lève std::out_of_range si la clé est absente, std::bad_any_cast si le type ne correspond pas
    template <class T>
    const T& get(const std::string& key) const {
        auto it = values.find(key);
        if (it == values.end()) throw std::out_of_range("DataStore: no data for key '" + key + "'");
        return std::any_cast<const T&>(it->second);
    }

    bool has(const std::string& key) const { return values.count(key) > 0; }
};

} // namespace stitching
