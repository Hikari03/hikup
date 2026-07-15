#pragma once

#include "CommandType.hpp"
#include "util.cpp"
#include "../shared/Connection.hpp"

namespace Batch {

	int autoResolve ( Command::Type type, Connection & connection, const std::vector<std::string> & files );
	int upload ( const std::vector<std::string>& files, Connection& connection );
	int download ( Connection & connection, const std::vector<std::string> & files );
	int remove ( Connection & connection, const std::vector<std::string> & files );

}
