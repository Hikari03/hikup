#include "CommandType.hpp"

#include <string>
#include <utility>

namespace Command {

	std::string toString ( const Type command ) {
		switch ( command ) {
			case Type::UPLOAD:
				return "UPLOAD";
			case Type::DOWNLOAD:
				return "DOWNLOAD";
			case Type::REMOVE:
				return "REMOVE";
			case Type::LIST:
				return "LIST";
			case Type::BATCH:
				return "BATCH";
			case Type::QUIET:
				return "QUIET";
			case Type::INVALID:
				return "INVALID";
			default: // cannot happen
				std::unreachable();
		}
	}

	Type selectBasic ( const std::set<Type>& commands ) {
		if ( commands.contains(Type::UPLOAD) )
			return Type::UPLOAD;

		if ( commands.contains(Type::DOWNLOAD) )
			return Type::DOWNLOAD;

		if ( commands.contains(Type::REMOVE) )
			return Type::REMOVE;

		if ( commands.contains(Type::LIST) )
			return Type::LIST;

		return Type::INVALID;
	}

	bool isValid ( const std::set<Type>& commands ) {
		if ( commands.contains(Type::INVALID) )
			return true;

		return false;
	}

	std::set<Type> resolveCommand ( std::string command ) {

		std::set<Type> res;

		size_t pos;

		if ( pos = command.find("up"); pos != std::string::npos ) {
			command.erase(pos, 2);
			res.emplace(Type::UPLOAD);
		}
		else if ( pos = command.find("down"); pos != std::string::npos ) {
			command.erase(pos, 4);
			res.emplace(Type::DOWNLOAD);
		}
		else if ( pos = command.find("ls"); pos != std::string::npos ) {
			command.erase(pos, 2);
			res.emplace(Type::LIST);
		}
		else if ( pos = command.find("rm"); pos != std::string::npos ) {
			command.erase(pos, 2);
			res.emplace(Type::REMOVE);
		}

		if ( pos = command.find('q'); pos != std::string::npos ) {
			command.erase(pos, 1);
			res.emplace(Type::QUIET);
		}

		if ( res.empty() || !command.empty() )
			return {Type::INVALID};

		return res;
	}
}

