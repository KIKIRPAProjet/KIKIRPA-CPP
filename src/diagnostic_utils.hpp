#pragma once
#include <pipeline/validation_report.hpp>
#include <utility>

namespace pipeline::detail {
/** Crée un diagnostic avec un contexte facultatif, sans dupliquer sa construction. */
inline Diagnostic issue(std::string code, std::string message,
                        std::optional<std::string> id = {}, Json details = Json::object()) {
    return {std::move(code), std::move(message), std::move(id), std::move(details)};
}
}
