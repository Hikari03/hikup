#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct Settings {

    Settings () = default;

    Settings ( const Settings& other );

    struct SyncTarget;

    std::string authUser;
    std::string authPass;

    std::string httpAddress;
    std::string hostname;
    std::string httpProtocol;
    std::vector<SyncTarget> syncTargets;
    int syncPeriod;

    bool wantHttp = false;

    static Settings loadFromFile ( const std::filesystem::path& filePath );

    [[nodiscard]] std::string toString () const;

    struct SyncTarget {
        std::string targetName;
        std::string targetAddress;
        std::string targetUser;
        std::string targetPass;
    };
};

std::ostream& operator << ( std::ostream& os, const Settings& settings );
