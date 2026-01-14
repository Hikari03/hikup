#include <stdexcept>
#include <string>

namespace Command {

	enum class Type {
		UPLOAD,
		DOWNLOAD,
		REMOVE,
		LIST,
		INVALID
	};

	inline std::string toString ( const Type command ) {
		switch ( command ) {
			case Type::UPLOAD:
				return "UPLOAD";
			case Type::DOWNLOAD:
				return "DOWNLOAD";
			case Type::REMOVE:
				return "REMOVE";
			case Type::LIST:
				return "LIST";
			case Type::INVALID:
				return "INVALID";
			default: // cannot happen
				return "";
		}
	}

	inline Type resolveCommand ( const std::string& command ) {
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