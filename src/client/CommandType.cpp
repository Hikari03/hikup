#include "CommandType.hpp"

#include <stdexcept>
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
			case Type::BATCH_UPLOAD:
				return "BATCH_UPLOAD";
			case Type::BATCH_DOWNLOAD:
				return "BATCH_DOWNLOAD";
			case Type::BATCH_REMOVE:
				return "BATCH_REMOVE";
			case Type::INVALID:
				return "INVALID";
			default: // cannot happen
				std::unreachable();
		}
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

	Type operator+ (Type one, Type two) {
		if ( one != Type::BATCH && two != Type::BATCH)
			return Type::INVALID;

		if ( two == Type::BATCH )
			std::swap(one, two);

		if ( two == Type::UPLOAD )
			return Type::BATCH_UPLOAD;

		if ( two == Type::DOWNLOAD)
			return Type::BATCH_DOWNLOAD;

		if ( two == Type::REMOVE )
			return Type::BATCH_REMOVE;

		return Type::INVALID;
	}
}

