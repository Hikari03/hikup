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
        template < typename T >
        concept SetOrVectorOfString =
        std::same_as<T, std::vector<std::string>> ||
        std::same_as<T, std::set<std::string>>;

        template < SetOrVectorOfString T >
        T findCorrespondingFileNames ( const std::set<std::string>& toFind );

        template < SetOrVectorOfString T >
        T _getLocalFileHashes ();

    }
}
