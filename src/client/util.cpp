#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sodium.h>
#include <string>
#include <vector>
#include <openssl/evp.h>


#include "Color.hpp"
#include "Style.hpp"
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

inline std::string computeHash ( std::ifstream& file, const size_t fileSize, const bool quiet = false,
                                 const ui::ProgressFn& progress = {} ) {
	using clock = std::chrono::steady_clock;

	file.clear();
	file.seekg(0);

	constexpr size_t chunkSize = 8 * 1024 * 1024;
	const auto buffer = std::make_unique<char[]>(chunkSize);

	// RAII: freed on every path, including the throw below
	const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
	if ( !ctx || EVP_DigestInit_ex(ctx.get(), EVP_blake2s256(), nullptr) != 1 )
		throw std::runtime_error("could not initialise hash context");

	const bool showProgress = !quiet && ui::g_tty;
	const size_t termW = ui::terminalWidth();

	const auto start = clock::now();
	auto lastDraw = start - std::chrono::seconds(1);
	unsigned long long hashed = 0;

	while ( file ) {
		file.read(buffer.get(), chunkSize);
		const auto got = static_cast<size_t>(file.gcount());
		if ( got == 0 ) break;

		if ( EVP_DigestUpdate(ctx.get(), buffer.get(), got) != 1 )
			throw std::runtime_error("hashing failed");
		hashed += got;

		if ( clock::now() - lastDraw >= std::chrono::milliseconds(80) ) {
			const std::chrono::duration<double> el = clock::now() - start;
			const double speed = el.count() > 0 ? static_cast<double>(hashed) / el.count() : 0.0;

			if ( progress )
				// a batch progress block renders the row instead
				progress(hashed, fileSize, speed);
			else if ( showProgress )
				ui::drawBar("hashing",
				            fileSize ? static_cast<double>(hashed) / double(fileSize) : 1.0,
				            humanReadableSize(hashed) + "/" + humanReadableSize(fileSize),
				            humanReadableSpeed(speed),
				            speed > 0 ? "eta " + ui::formatDuration(static_cast<double>(fileSize - hashed) / speed) : "eta --",
				            termW);
			lastDraw = clock::now();
		}
	}

	// eof is expected; any other stream error is a real read failure
	if ( file.bad() || ( file.fail() && !file.eof() ) )
		throw std::runtime_error("error reading file");

	std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
	unsigned int digestSize = 0;
	if ( EVP_DigestFinal_ex(ctx.get(), digest.data(), &digestSize) != 1 )
		throw std::runtime_error("could not finalise hash");

	if ( showProgress && !progress ) ui::clearLine();

	file.clear();
	file.seekg(0);

	return bytesToHex(digest.data(), digestSize);
}

inline std::vector<std::string> cutStringIntoVector(std::string_view str) {
    std::vector<std::string> ret;
    constexpr auto sep = " 	\n\r";

    // scan token by token: skip separator runs, then take up to the next one
    size_t start = str.find_first_not_of(sep);
    while ( start != std::string_view::npos ) {
        size_t end = str.find_first_of(sep, start);
        if ( end == std::string_view::npos )
            end = str.size();

        ret.emplace_back(str.substr(start, end - start));
        start = str.find_first_not_of(sep, end);
    }

    return ret;
}
