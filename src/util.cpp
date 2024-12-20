#include <filesystem>
#include <fstream>
#include <string>

#include "CommandType.cpp"
#include "Color.cpp"



/**
 * @brief Resolves the path to a file handle
 *
 * @param path
 * @return input file stream of the resolved path and its size
 */
std::pair<std::ifstream, size_t> resolveFile ( const std::string& path ) {
	std::filesystem::path _path = std::filesystem::absolute(path); // Resolves the absolute path of the input file

	if ( is_directory(_path) )
		throw std::runtime_error("File is a directory");

	if ( !is_regular_file(_path) )
		throw std::runtime_error("File is not regular");

	std::ifstream file(_path, std::ios::binary);

	if ( !file.is_open() || !file.good() )
		throw std::runtime_error("Could not open file");

	file.seekg(0, std::ios::end);
	size_t size = file.tellg();

	file.seekg(0, std::ios::beg);

	return {std::move(file), std::move(size)};
}

CommandType resolveCommand ( const std::string& command ) {
	if ( command == "up" )
		return CommandType::UPLOAD;
	if ( command == "down" )
		return CommandType::DOWNLOAD;

	throw std::runtime_error("Invalid command: " + command);
}

std::string colorize(const std::string & text, Color color) {

	switch ( color ) {
		case Color::RED:
			return "\033[38;5;196m" + text + "\033[0m";

		case Color::GREEN:
			return "\033[38;5;47m" + text + "\033[0m";

		case Color::BLUE:
			return "\033[38;5;27m" + text + "\033[0m";

		case Color::PURPLE:
			return "\033[38;5;165m" + text + "\033[0m";

		case Color::CYAN:
			return "\033[38;5;45m" + text + "\033[0m";

		default:
			return text;
	}
}
