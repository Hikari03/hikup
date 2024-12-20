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

	return 0;

	connection.sendInternal("size:" + std::to_string(fileSize));
	connection.sendInternal("command:" + toString(command));

	if (connection.receive() != "OK") {
		std::cerr << colorize("Server did not accept the file", Color::RED) << std::endl;
		return 1;
	}

	std::cout << colorize("Server ready!\n", Color::GREEN) + colorize("Starting upload of size: ", Color::BLUE) +
		colorize(humanReadableSize(fileSize), Color::CYAN) << std::endl;

	return 0;
}
