#include <filesystem>
#include <iomanip>
#include <ios>
#include <iostream>
#include <memory>
#include <sodium.h>
#include <string>

#include "includes/toml.hpp"

struct Settings {
	std::string authUser;
	std::string authPass;
	std::string httpAddress;
	std::string hostname;
	std::string httpProtocol;
	bool wantHttp = false;
};

inline std::ostream& operator << ( std::ostream& os, const Settings& settings) {
	os << "settings: \n"
	<< "  wantHttpServer: " << ( settings.wantHttp ? "true" : "false" ) << "\n"
	<< "  hostname: " << settings.hostname << "\n"
	<< "  httpAddress: " << settings.httpAddress << "\n"
	<< "  httpProtocol: " << settings.httpProtocol << "\n"
	<< "auth: \n"
	<< "  user: " << settings.authUser << "\n"
	<< "  password: " << settings.authPass;

	return os;
}

inline std::string humanReadableSize ( const size_t size ) {
	const char* units[] = {"B", "KB", "MB", "GB", "TB"};
	auto sizeDouble = static_cast<double>(size);
	size_t unitIndex = 0;

	// Calculate the appropriate unit
	while ( sizeDouble >= 1000.0 && unitIndex < std::size(units) - 1 ) {
		sizeDouble /= 1000.0;
		unitIndex++;
	}

	// Format the size to 2 decimal places and append unit
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(2) << sizeDouble << " " << units[unitIndex];
	return oss.str();
}

inline std::string binToHex ( const unsigned char* bin, const size_t size ) {
	const auto hex = std::make_unique<char[]>(size * 2 + 1);

	sodium_bin2hex(hex.get(), size * 2 + 1, bin, size);

	return {hex.get(), size * 2};;
}

inline Settings loadSettings () {

	toml::parse_result settings;

	try {
		settings = toml::parse_file("settings/settings.toml");
	} catch ( const toml::parse_error& err ) {
		std::cerr
		<< "Error parsing file '" << *err.source().path
		<< "':\n" << err.description()
		<< "\n (" << err.source().begin << ")\n";
		throw std::runtime_error("Could not load settings from " + (std::filesystem::current_path() /"settings"/"settings.toml").string());
	}

	if ( settings.empty() ) {
		throw std::runtime_error("Could not load settings from " + (std::filesystem::current_path() /"settings"/"settings.toml").string());
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

	return result;
}