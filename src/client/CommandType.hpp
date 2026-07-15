#pragma once

#include <string>

namespace Command {

	enum class Type {
		UPLOAD,
		DOWNLOAD,
		REMOVE,
		LIST,
		BATCH,
		BATCH_UPLOAD,
		BATCH_DOWNLOAD,
		BATCH_REMOVE,
		INVALID
	};

	std::string toString ( const Type command );
	Type resolveCommand ( const std::string& command );
	Type operator+ (Command::Type one, Command::Type two);

}