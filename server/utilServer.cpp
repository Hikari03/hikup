#include <iomanip>
#include <ios>
#include <memory>
#include <string>
#include <sodium.h>
#include <fstream>

inline std::string humanReadableSize ( size_t size ) {
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

inline std::string binToHex ( const unsigned char* bin, const size_t size ) {
	auto hex = std::make_unique<char[]>(size * 2 + 1);

	sodium_bin2hex(hex.get(), size * 2 + 1, bin, size);

	return {hex.get(), size * 2};;
}