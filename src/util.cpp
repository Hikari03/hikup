#include <filesystem>
#include <fstream>
#include <iostream>
#include <sodium.h>
#include <string>
#include <sys/sysinfo.h>


#include "Color.cpp"

/**
 * @brief Resolves the path to a file handle
 *
 * @param path
 * @return input file stream of the resolved path and its size
 */
inline std::tuple<std::ifstream, uintmax_t, std::string> resolveFile ( const std::string& path ) {
	std::filesystem::path _path = std::filesystem::absolute(path); // Resolves the absolute path of the input file

	if ( is_directory(_path) )
		throw std::runtime_error("File is a directory");

	if ( !is_regular_file(_path) )
		throw std::runtime_error("File is not regular/does not exist");

	std::ifstream file(_path, std::ios::binary);

	if ( !file.is_open() || !file.good() )
		throw std::runtime_error("Could not open file");

	auto size = std::filesystem::file_size(_path);

	return {std::move(file), size, _path.filename()};
}

inline std::string colorize ( const std::string& text, Color color ) {
	switch ( color ) {
		case Color::RED: return "\033[38;5;196m" + text + "\033[0m";

		case Color::GREEN: return "\033[38;5;47m" + text + "\033[0m";

		case Color::BLUE: return "\033[38;5;75m" + text + "\033[0m";

		case Color::PURPLE: return "\033[38;5;141m" + text + "\033[0m";

		case Color::CYAN: return "\033[38;5;45m" + text + "\033[0m";

		case Color::LL_BLUE: return "\033[38;5;153m" + text + "\033[0m";

		default: return text;
	}
}

inline std::string humanReadableSize ( const size_t size ) {
	const char* units[] = {"B", "KB", "MB", "GB", "TB"};
	auto sizeDouble = static_cast<double>(size);
	size_t unitIndex = 0;

	// Calculate the appropriate unit
	while ( sizeDouble >= 1000.0 && unitIndex < std::size(units) - 1 ) {
		sizeDouble /= 1000.0;
		unitIndex++;
	}

	// Format the size to 2 decimal places and append unit
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(2) << sizeDouble << " " << units[unitIndex];
	return oss.str();
}

inline std::string humanReadableSpeed ( double speed ) {
	const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s", "TB/s"};
	size_t unitIndex = 0;

	// Calculate the appropriate unit
	while ( speed >= 1000.0 && unitIndex < std::size(units) - 1 ) {
		speed /= 1000.0;
		unitIndex++;
	}

	// Format the speed to 2 decimal places and append unit
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(2) << speed << " " << units[unitIndex];
	return oss.str();
}

inline std::string binToHex ( const unsigned char* bin, const size_t size ) {
	const auto hex = std::make_unique<char[]>(size * 2 + 1);

	sodium_bin2hex(hex.get(), size * 2 + 1, bin, size);

	return {hex.get(), size * 2};
}

inline std::pair<unsigned char*, size_t> hexToBin ( const std::string& hex ) {
	auto bin = std::make_unique<unsigned char[]>(hex.size() / 2);

	sodium_hex2bin(bin.get(), hex.size() / 2, hex.c_str(), hex.size(), nullptr, nullptr, nullptr);

	return {bin.get(), hex.size() / 2};
}

inline unsigned long getFreeMemory () {
	struct sysinfo memInfo{};
	sysinfo(&memInfo);
	return memInfo.bufferram + memInfo.freeram;
}

inline std::string computeHash ( std::ifstream& file, const size_t allocationSpace, const size_t fileSize ) {
	file.seekg(0);
	const auto buffer = std::make_unique<char[]>(allocationSpace);
	unsigned char hash[crypto_generichash_BYTES];
	crypto_generichash_state state;
	crypto_generichash_init(&state, nullptr, 0, sizeof hash);

	while ( true ) {
		file.read(buffer.get(), allocationSpace);

		if ( file.gcount() == 0 ) {
			if ( file.fail() )
				throw std::runtime_error("Error reading file.");
			break;
		}

		else {
			// Update hash with the bytes read
			crypto_generichash_update(&state, reinterpret_cast<const unsigned char*>(buffer.get()),
			                          file.gcount());
		}

		if ( file.eof() )
			break;

		std::cout << "\r" << colorize("Hashing file... ", Color::BLUE) +
				colorize(humanReadableSize(static_cast<size_t>(file.tellg())) + '/'+ humanReadableSize(fileSize) + "         ", Color::CYAN) << std::flush;

	}

	crypto_generichash_final(&state, hash, sizeof hash);

	std::cout << "\r" << colorize("Hashing file... ", Color::BLUE) +
				colorize(humanReadableSize(fileSize) + '/'+ humanReadableSize(fileSize), Color::CYAN) << std::endl;

	file.clear();
	file.seekg(0);

	return binToHex(hash, sizeof hash);
}


