#pragma once

#include <fstream>

#include "../shared/Connection.hpp"

namespace CommandHandlers {
	void sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection, bool quiet = false );
	void downloadFile ( Connection& connection, bool quiet = false );
	int listFiles ( Connection& connection, const std::string& user, const std::string& pass );
}