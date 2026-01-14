#include <filesystem>
#include <iomanip>
#include <ios>
#include <iostream>
#include <memory>
#include <sodium.h>
#include <string>
#include <sys/sysinfo.h>

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

inline Settings loadSettings () {
	toml::parse_result settings;

	try { settings = toml::parse_file("settings/settings.toml"); }
	catch ( const toml::parse_error& err ) {
		std::cerr << "Error parsing file '" << *err.source().path << "':\n" << err.description() << "\n (" << err.
				source().begin << ")\n";
		throw std::runtime_error(
			"Could not load settings from " + ( std::filesystem::current_path() / "settings" / "settings.toml" ).
			string());
	}

	if ( settings.empty() ) {
		throw std::runtime_error(
			"Could not load settings from " + ( std::filesystem::current_path() / "settings" / "settings.toml" ).
			string());
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
