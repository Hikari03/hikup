#pragma once

#include <filesystem>
#include <string>
#include <thread>
#include <utility>

#include "includes/mongoose.hpp"

namespace HTTPFileServerVars { // ugly i know
	inline std::string _rootDir;
	inline std::string _authUser;
	inline std::string _authPass;
}

class HTTPFileServer {
public:
	HTTPFileServer ( bool& turnOff, std::string rootDir )
		: _turnOff(turnOff) { HTTPFileServerVars::_rootDir = std::move(rootDir); }

	[[nodiscard]] std::thread run ( std::string authUser = "admin", std::string authPass = "admin", const std::string & address = "0.0.0.0:6997" );

	static std::string createSymlinkFor(const std::filesystem::path & file);
	static void removeSymlinkFor(const std::filesystem::path & file);

private:
	void _run ( const std::string & address ) const;

	static void _generateSymLinks ();

	static void _ev_handler ( mg_connection* c, int ev, void* ev_data );

	const bool& _turnOff;
};
