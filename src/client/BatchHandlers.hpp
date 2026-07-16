#pragma once

#include "CommandType.hpp"
#include "util.cpp"
#include "../shared/Connection.hpp"

namespace Batch {

	int autoResolve ( const std::set<Command::Type> & command, Connection & connection, const std::vector<std::string> & files, bool quiet = false );
	int upload ( Connection& connection, const std::vector<std::string>& files, bool quiet = false );
	int download ( Connection & connection, const std::vector<std::string> & files, bool quiet = false );
	int remove ( Connection & connection, const std::vector<std::string> & files, bool quiet = false );

}
