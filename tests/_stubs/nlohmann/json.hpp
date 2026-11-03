#pragma once
#include <string>
#include <type_traits>

namespace nlohmann {
struct json {
    std::string m_model_path = "tests/_stubs/test_runner.cpp";
    json() = default;
    template<typename T>
    T value(const std::string& key, const T& def) const {
        if constexpr (std::is_same_v<T, std::string>) {
            if (key == "model_path") {
                return m_model_path;
            }
        }
        return def;
    }
    std::string& operator[](const std::string&) { return m_model_path; }
};
}
