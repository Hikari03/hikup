#pragma once

#include <filesystem>
#include <string>
#include <thread>
#include <utility>

#include "includes/mongoose.h"

namespace HTTPFileServerVars { // ugly i know
	inline std::string _rootDir;
	inline std::string _authUser;
	inline std::string _authPass;
	inline std::string _httpDisplayInBrowser;
	inline std::string _hostname;     // for Host validation (DNS rebinding)
	inline bool _secureCookie = false; // set when the server is fronted by TLS
}

class HTTPFileServer {
public:
	HTTPFileServer ( bool& turnOff, std::string rootDir, const bool httpDisplayInBrowser,
	                 const std::string& hostname = {}, const bool secureCookies = false )
		:	_turnOff(turnOff)
	{
		HTTPFileServerVars::_rootDir = std::move(rootDir);
		HTTPFileServerVars::_httpDisplayInBrowser = httpDisplayInBrowser ? "yes" : "no";
		HTTPFileServerVars::_hostname = hostname;
		HTTPFileServerVars::_secureCookie = secureCookies;
	}

	[[nodiscard]] std::thread run ( std::string authUser = "admin", std::string authPass = "admin", const std::string & address = "0.0.0.0:6997" );

	static std::string createSymlinkFor(const std::filesystem::path & file);
	static void removeSymlinkFor(const std::filesystem::path & file);

private:
	void _run ( const std::string & address ) const;

	static void _generateSymLinks ();

	static void _ev_handler ( mg_connection* c, int ev, void* ev_data );

	const bool& _turnOff;
};
