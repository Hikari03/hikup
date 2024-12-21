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
inline std::tuple<std::ifstream, std::ifstream::pos_type, std::string> resolveFile ( const std::string& path ) {
	std::filesystem::path _path = std::filesystem::absolute(path); // Resolves the absolute path of the input file

	if ( is_directory(_path) )
		throw std::runtime_error("File is a directory");

	if ( !is_regular_file(_path) )
		throw std::runtime_error("File is not regular/does not exist");

	std::ifstream file(_path, std::ios::binary);

	if ( !file.is_open() || !file.good() )
		throw std::runtime_error("Could not open file");

	file.seekg(0, std::ios::end);
	auto size = file.tellg();

	file.seekg(0, std::ios::beg);

	return {std::move(file), size, _path.filename()};
}

inline Command::Type resolveCommand ( const std::string& command ) {
	if ( command == "up" )
		return Command::Type::UPLOAD;
	if ( command == "down" )
		return Command::Type::DOWNLOAD;

	throw std::runtime_error("Invalid command: " + command);
}

inline std::string colorize(const std::string & text, Color color) {

	switch ( color ) {
		case Color::RED:
			return "\033[38;5;196m" + text + "\033[0m";

		case Color::GREEN:
			return "\033[38;5;47m" + text + "\033[0m";

		case Color::BLUE:
			return "\033[38;5;75m" + text + "\033[0m";

		case Color::PURPLE:
			return "\033[38;5;141m" + text + "\033[0m";

		case Color::CYAN:
			return "\033[38;5;45m" + text + "\033[0m";

		case Color::LL_BLUE:
			return "\033[38;5;153m" + text + "\033[0m";

		default:
			return text;
	}
}

inline std::string humanReadableSize( std::ifstream::pos_type size ) {

	const char* units[] = {"B", "KB", "MB", "GB", "TB"};
	auto sizeDouble = static_cast<double>(size);
	int unitIndex = 0;

	// Calculate the appropriate unit
	while (sizeDouble >= 1000.0 && unitIndex < std::size(units) - 1) {
		sizeDouble /= 1000.0;
		unitIndex++;
	}

	// Format the size to 2 decimal places and append unit
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(2) << sizeDouble << " " << units[unitIndex];
	return oss.str();
}

inline std::string humanReadableSpeed( double speed ) {
	const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s", "TB/s"};
	int unitIndex = 0;

	// Calculate the appropriate unit
	while (speed >= 1000.0 && unitIndex < std::size(units) - 1) {
		speed /= 1000.0;
		unitIndex++;
	}

	// Format the speed to 2 decimal places and append unit
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(2) << speed << " " << units[unitIndex];
	return oss.str();
}