#pragma once
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "includes/toml.hpp"

std::string operator+ ( const std::string& lhs, const toml::source_position& rhs );

namespace Utils {
    void log ( const std::string& message, bool newline = true );
    void elog ( const std::string& message, bool newline = true );
}
