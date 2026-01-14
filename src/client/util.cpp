#include <filesystem>
#include <fstream>
#include <iostream>
#include <sodium.h>
#include <string>

#include "Color.cpp"
#include "../shared/utils.hpp"

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

		case Color::YELLOW: return "\033[38;5;226m" + text + "\033[0m";

		default: return text;
	}
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


