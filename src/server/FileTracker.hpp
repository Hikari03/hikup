#pragma once
#include <filesystem>
#include <set>
#include <string>

#include "includes/toml.hpp"

class FileTracker {
public:
    explicit FileTracker( const std::filesystem::path& path );

    void add ( const std::set<std::string>& additions );
    void add ( const std::string& addition );
    void remove ( const std::set<std::string>& toRemove );
    void remove ( const std::string& toRemove );
    [[nodiscard]] std::set<std::string> list ( ) const;

private:
    toml::table root;
    std::filesystem::path filePath;
};
