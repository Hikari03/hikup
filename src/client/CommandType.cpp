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

	Type resolveCommand ( const std::string& command ) {
		if ( command == "up" )
			return Type::UPLOAD;
		if ( command == "down" )
			return Type::DOWNLOAD;
		if ( command == "ls" )
			return Type::LIST;
		if ( command == "rm" )
			return Type::REMOVE;

		return Type::INVALID;
	}
}

