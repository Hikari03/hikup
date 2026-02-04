#include "Settings.hpp"

#include "utils.hpp"
#include "includes/toml.hpp"



Settings::Settings ( const Settings& other ) {

    if (this == &other)
        return;

    authUser = other.authUser;
    authPass = other.authPass;
    httpAddress = other.httpAddress;
    httpProtocol = other.httpProtocol;
    hostname = other.hostname;
    syncTargets = other.syncTargets;
    syncPeriod = other.syncPeriod;
}

Settings Settings::loadFromFile ( const std::filesystem::path& filePath ) {
    toml::parse_result settings;

    try { settings = toml::parse_file(filePath.string()); }
    catch ( const toml::parse_error& err ) {
        Utils::elog(
            "Error parsing file '" + *err.source().path + "':\n" + std::string(err.description()) + "\n (" + err.
            source().begin + ")\n"
        );
        throw std::runtime_error(
            "Could not load settings from " + filePath.string()
        );
    }

    if ( settings.empty() ) {
        throw std::runtime_error(
            "Could not load settings from " + filePath.string()
        );
    }

    Settings result;

    result.wantHttp = settings["server"]["wantHttpServer"].as_boolean()->value_or(false);
    result.hostname = settings["server"]["hostname"].as_string()->value_or("<NOT-DEFINED>");

    if ( result.wantHttp ) {
        result.httpAddress = settings["server"]["httpAddress"].as_string()->value_or("http://0.0.0.0:6997");
        result.httpProtocol = settings["server"]["httpProtocol"].as_string()->value_or("http");
        result.authUser = settings["auth"]["user"].as_string()->value_or("admin");
        result.authPass = settings["auth"]["password"].as_string()->value_or("admin");
    }

    if ( const auto syncTargets = settings["syncTargets"]["targets"].as_array() ) {
        auto it = syncTargets->begin();
        while ( it != syncTargets->end() ) {
            const auto target = it->as_table();

            result.syncTargets.emplace_back(
                target->get("name")->as_string()->value_or("INVALID"),
                target->get("address")->as_string()->value_or("INVALID"),
                target->get("user")->as_string()->value_or("INVALID"),
                target->get("pass")->as_string()->value_or("INVALID")
            );

            ++it;
        }
    }
    result.syncPeriod = settings["syncTargets"]["syncPeriod"].as_integer()->value_or(30);


    return result;
}

std::string Settings::toString () const {

    return std::string("settings: \n")
            + "  wantHttpServer: " + ( wantHttp ? "true" : "false" ) + "\n"
            + "  hostname: " + hostname + "\n"
            + "  httpAddress: " + httpAddress + "\n"
            + "  httpProtocol: " + httpProtocol + "\n"
            + "auth: \n"
            + "  user: " + authUser + "\n"
            + "  password: " + authPass + "\n"
            + [this]() -> std::string { // SyncTargets
                if ( syncTargets.empty() ) {
                    return "";
                }

                std::string result = "syncTargets:\n";

                for ( const auto& [targetName, targetAddress, targetUser, targetPass] : syncTargets ) {
                    result += "  " + targetName + ":\n";
                    result += "    address: " + targetAddress + '\n';
                    result += "    user: " + targetUser + '\n';
                    result += "    pass: " + targetPass + '\n';
                    result += "\n";
                }

                result += "  syncPeriod: " + std::to_string(syncPeriod) + '\n';
                return result;
            }();

}

std::ostream& operator << ( std::ostream& os, const Settings& settings ) {
    return os << settings.toString();
}
