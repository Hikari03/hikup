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
		INVALID
	};

	std::string toString ( const Type command );
	Type resolveCommand ( const std::string& command );

	/**
	 *
	 * @param commands
	 * @return will return only basic command type i.e. upload, download, list, remove, if found
	 */
	Type selectBasic ( const std::set<Type>& commands );

	bool isValid ( const std::set<Type> & commands );

}