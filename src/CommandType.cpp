#include <string>
#include <stdexcept>

namespace Command {

	enum class Type {
		UPLOAD,
		DOWNLOAD,
		REMOVE
	};

	inline std::string toString ( Type command ) {
		switch ( command ) {
			case Type::UPLOAD:
				return "UPLOAD";
			case Type::DOWNLOAD:
				return "DOWNLOAD";
			case Type::REMOVE:
				return "REMOVE";
			default: // cannot happen
				return "";
		}
	}

	inline Type resolveCommand ( const std::string& command ) {
		if ( command == "up" )
			return Type::UPLOAD;
		if ( command == "down" )
			return Type::DOWNLOAD;
		if ( command == "rm" )
			return Type::REMOVE;

		throw std::runtime_error("Invalid command: " + command);
	}
}