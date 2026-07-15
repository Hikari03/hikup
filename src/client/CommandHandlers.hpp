#pragma once

#include <fstream>

#include "../shared/Connection.hpp"

namespace CommandHandlers {
	void sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection );
	void downloadFile ( Connection& connection );
	int listFiles ( Connection& connection, const std::string& user, const std::string& pass );
}