#include <string>

namespace Command {

	enum class Type {
		UPLOAD,
		DOWNLOAD
	};

	inline std::string toString ( Type command ) {
		switch ( command ) {
			case Type::UPLOAD:
				return "UPLOAD";
			case Type::DOWNLOAD:
				return "DOWNLOAD";
			default: // cannot happen
				return "";
		}
	}
}