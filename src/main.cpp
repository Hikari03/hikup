#include <iostream>

#include "CommandType.cpp"
#include "Connection.h"
#include "util.cpp"

int main ( int argc, char* argv[] ) {
	if ( argc < 4 ) {
		std::cerr << "Usage: " << argv[0] << " <up <file> | down <hash> > <server> \n\n"
					 "If file is successfully uploaded, you will get file hash"
					 "which you need to input if you want to download it" << std::endl;
		return 1;
	}

	CommandType command;
	Connection connection;
	std::ifstream file;
	size_t fileSize;

	try {
		command = resolveCommand(argv[1]);
		auto [_file,_fileSize] = resolveFile(argv[2]);
		file = std::move(_file);
		fileSize = _fileSize;
		connection.connectToServer(argv[3], 6998);
		std::cout << colorize("Connected to server", Color::GREEN) << std::endl;
	}
	catch ( std::runtime_error& e ) {
		std::cerr << colorize(e.what(), Color::RED) << std::endl;
		return 1;
	}



}
