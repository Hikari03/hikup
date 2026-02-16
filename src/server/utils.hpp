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

    namespace FS {
        std::set<std::string> findCorrespondingFileNames ( const std::set<std::string>& hash );

        std::optional<std::string> findCorrespondingFileName ( const std::string& hash );

        std::set<std::string> getLocalFileHashes ();

        std::optional<std::string> getLocalRawFileName ( const std::string& filename );

    }
}
