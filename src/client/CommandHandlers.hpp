#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include "../shared/Connection.hpp"

namespace CommandHandlers {
	void sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection, bool quiet = false, const std::string& e2eKey = "" );
	std::string downloadFile ( Connection& connection, bool quiet = false, const std::filesystem::path& destination = {}, bool summary = true );
	int downloadFileE2E ( Connection& connection, const std::string& key, bool quiet = false );
	int listFiles ( Connection& connection, const std::string& user, const std::string& pass );
	int infoFile ( Connection& connection, bool quiet = false );
}