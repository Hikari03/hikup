#include <iostream>

#include "Connection.h"
#include "util.cpp"

int main ( int argc, char* argv[] ) {
	if ( argc < 4 ) {
		std::cout << "Usage: " << argv[0] << " <up <file> | down <hash>> <server> \n\n"
				"If file is successfully uploaded, you will get file hash\n"
				"which you need to input if you want to download it" << std::endl;
		return 1;
	}

	Command::Type command;
	Connection connection;
	std::ifstream file;
	std::ifstream::pos_type fileSize;

	try {
		command = resolveCommand(argv[1]);
		auto [_file, _fileSize] = resolveFile(argv[2]);
		file = std::move(_file);
		fileSize = _fileSize;
		//connection.connectToServer(argv[3], 6998);
		std::cout << colorize("Connected to server", Color::GREEN) << std::endl;
	}
	catch ( std::runtime_error& e ) {
		std::cerr << colorize(e.what(), Color::RED) << std::endl;
		return 1;
	}


	//connection.sendInternal("size:" + std::to_string(fileSize));
	//connection.sendInternal("command:" + toString(command));

	/*if (connection.receive() != "OK") {
		std::cerr << colorize("Server did not accept the file", Color::RED) << std::endl;
		return 1;
	}*/

	std::cout << colorize("Server ready!\n", Color::GREEN) + colorize("Starting upload of size: ", Color::BLUE) +
			colorize(humanReadableSize(fileSize), Color::CYAN) << std::endl;

	// we will send the file in chunks of 64KB
	constexpr size_t chunkSize = 64 * 1024;
	std::vector<char> buffer(chunkSize);

	unsigned long long totalChunks = fileSize / chunkSize + 1;
	size_t lastChunkSize = fileSize % chunkSize;

	for ( unsigned long long i = 0; i < totalChunks - 1; ++i ) {
		file.read(buffer.data(), chunkSize);
		std::cout << "\r" << colorize("Sending chunk ", Color::BLUE) + colorize(std::to_string(i + 1), Color::CYAN) +
				colorize("/", Color::BLUE) + colorize(std::to_string(totalChunks + 1), Color::CYAN) + colorize(
					std::string(" (") + std::to_string(( static_cast<double>(i) / static_cast<double>(totalChunks) ) * 100.0).substr(0,5) + " %)", Color::PURPLE) << std::flush;


		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		/*try {
			connection.send(std::string(buffer.data(), chunkSize));
		}
		catch ( std::runtime_error& e ) {
			std::cerr << colorize("ERROR: ", Color::RED) + colorize(e.what(), Color::RED) << std::endl;
			return 1;
		}*/
	}

	file.read(buffer.data(), static_cast<std::streamsize>(lastChunkSize));
	std::cout << "\r" << colorize("Sending chunk ", Color::BLUE) + colorize(std::to_string(totalChunks), Color::CYAN) +
			colorize("/", Color::BLUE) + colorize(std::to_string(totalChunks), Color::CYAN) + colorize(
				" (100 %)  ", Color::PURPLE) << std::endl;


	return 0;
}
