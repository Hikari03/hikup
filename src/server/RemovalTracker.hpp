#pragma once
#include <filesystem>
#include <set>
#include <string>

#include "includes/toml.hpp"

class RemovalTracker {
public:
    explicit RemovalTracker( const std::filesystem::path& path );

    [[nodiscard]] bool add ( const std::set<std::string>& additions );
    [[nodiscard]] bool add ( const std::string& addition );
    void remove ( const std::set<std::string>& toRemove );
    void remove ( const std::string& toRemove );
    std::set<std::string> list ( ) const;

private:
    toml::table root;
    std::filesystem::path filePath;
};
