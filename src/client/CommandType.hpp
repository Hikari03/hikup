#pragma once

#include <set>
#include <string>

namespace Command {

	enum class Type {
		UPLOAD,
		DOWNLOAD,
		REMOVE,
		LIST,
		BATCH,
		QUIET,
		INVALID
	};

	std::string toString ( const Type command );
	std::set<Type> resolveCommand ( std::string command );

	/**
	 *
	 * @param commands
	 * @return will return only basic command type i.e. upload, download, list, remove, if found
	 */
	Type selectBasic ( const std::set<Type>& commands );

	bool isValid ( const std::set<Type> & commands );

}