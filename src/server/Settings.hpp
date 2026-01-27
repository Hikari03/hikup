#pragma once

#include <filesystem>
#include <iostream>
#include <string>

struct Settings {
    std::string authUser;
    std::string authPass;
    std::string httpAddress;
    std::string hostname;
    std::string httpProtocol;
    bool wantHttp = false;

    static Settings loadFromFile ( const std::filesystem::path& filePath );

    [[nodiscard]] std::string toString() const;
};

std::ostream& operator << ( std::ostream& os, const Settings& settings);