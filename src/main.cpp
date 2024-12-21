#include <iostream>

#include "Connection.h"
#include "util.cpp"

void sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection ) {
	// we will send the file in chunks of 128KB
	constexpr size_t chunkSize = 128 * 1000;
	std::vector<char> buffer(chunkSize);

	const unsigned long long totalChunks = fileSize / chunkSize + 1;
	const size_t lastChunkSize = fileSize % chunkSize;

	double readSpeed = 0.0, uploadSpeed = 0.0;

	std::cout << colorize("Starting upload of size: ", Color::BLUE) << colorize(
		humanReadableSize(fileSize), Color::CYAN) << "\n" << std::endl;

	for ( unsigned long long i = 0; i < totalChunks - 1; ++i ) {
		auto startReadTime = std::chrono::high_resolution_clock::now();
		file.read(buffer.data(), chunkSize);
		auto endReadTime = std::chrono::high_resolution_clock::now();

		std::chrono::duration<double> duration = endReadTime - startReadTime;
		readSpeed = static_cast<double>(chunkSize) / duration.count();

		auto startUploadTime = std::chrono::high_resolution_clock::now();


		connection.send(std::string(buffer.data(), chunkSize));


		auto endUploadTime = std::chrono::high_resolution_clock::now();

		duration = endUploadTime - startUploadTime;
		uploadSpeed = static_cast<double>(chunkSize) / duration.count();

		std::cout << "\r" << colorize("Sending chunk ", Color::BLUE) + colorize(std::to_string(i + 1), Color::CYAN) +
				colorize("/", Color::BLUE) + colorize(std::to_string(totalChunks + 1), Color::CYAN) + colorize(
					std::string(" (") +
					std::to_string(( static_cast<double>(i + 1) / static_cast<double>(totalChunks) ) * 100.0).
					substr(0, 5) + " %)",
					Color::PURPLE) + " ┃ " + colorize("Read: " + humanReadableSpeed(readSpeed), Color::LL_BLUE) + " ━━ "
				+ colorize("Up: " + humanReadableSpeed(uploadSpeed), Color::GREEN) + "  " << std::flush;
	}

	file.read(buffer.data(), static_cast<std::streamsize>(lastChunkSize));
	std::cout << "\r" << colorize("Sending chunk ", Color::BLUE) + colorize(std::to_string(totalChunks), Color::CYAN) +
			colorize("/", Color::BLUE) + colorize(std::to_string(totalChunks), Color::CYAN) + colorize(
				" (100 %)  ", Color::PURPLE) << std::endl;

	connection.sendInternal("DONE");
	auto hash = connection.receiveInternal();
}

void downloadFile ( Connection& connection ) {
	// get total chunks
	auto totalChunks = std::stoll(connection.receiveInternal());
	auto fileSize = std::stoll(connection.receiveInternal());
	auto fileName = connection.receiveInternal();


	std::cout << colorize("Downloading file: ", Color::BLUE) + colorize(fileName, Color::CYAN) << colorize(
		" of size: ", Color::BLUE) << colorize(humanReadableSize(fileSize), Color::CYAN) << std::endl;

	// create file
	std::ofstream file(fileName, std::ios::binary);

	for ( unsigned long long i = 0; i < totalChunks; ++i ) {
		auto startDownloadTime = std::chrono::high_resolution_clock::now();
		auto chunk = connection.receiveData();
		auto endDownloadTime = std::chrono::high_resolution_clock::now();

		// calculate download speed
		std::chrono::duration<double> duration = endDownloadTime - startDownloadTime;
		auto downloadSpeed = static_cast<double>(chunk.size()) / duration.count();

		auto writeStart = std::chrono::high_resolution_clock::now();
		file.write(chunk.c_str(), static_cast<long>(chunk.size()));
		auto writeEnd = std::chrono::high_resolution_clock::now();

		duration = writeEnd - writeStart;
		auto writeSpeed = static_cast<double>(chunk.size()) / duration.count();

		std::cout << "\r" << colorize("Sending chunk ", Color::BLUE) + colorize(std::to_string(i + 1), Color::CYAN) +
				colorize("/", Color::BLUE) + colorize(std::to_string(totalChunks), Color::CYAN) + colorize(
					std::string(" (") +
					std::to_string(( static_cast<double>(i + 1) / static_cast<double>(totalChunks) ) * 100.0).
					substr(0, 5) + " %)",
					Color::PURPLE) + " ┃ " + colorize("Write: " + humanReadableSpeed(writeSpeed), Color::LL_BLUE) +
				" ━━ " + colorize("Down: " + humanReadableSpeed(downloadSpeed), Color::GREEN) + "  " << std::flush;
	}
}

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
	std::string fileName;

	try {
		command = resolveCommand(argv[1]);
		if ( command == Command::Type::UPLOAD ) {
			auto [_file, _fileSize, _fileName] = resolveFile(argv[2]);
			file = std::move(_file);
			fileSize = _fileSize;
			fileName = _fileName;
		}
		connection.connectToServer(argv[3], 6998);
		std::cout << colorize("Connected to server", Color::GREEN) << std::endl;
	}
	catch ( std::runtime_error& e ) {
		std::cerr << colorize(e.what(), Color::RED) << std::endl;
		return 1;
	}


	connection.sendInternal("size:" + std::to_string(fileSize));
	connection.sendInternal("command:" + Command::toString(command));
	if ( command == Command::Type::UPLOAD ) { connection.sendInternal("filename:" + fileName); }
	else if ( command == Command::Type::DOWNLOAD ) {
		fileName = argv[2];
		connection.sendInternal("hash:" + fileName);
	}

	if ( connection.receive() != "OK" ) {
		std::cerr << colorize("Server did not accept the request - ", Color::RED) << std::endl;
		if ( command == Command::Type::UPLOAD ) {
			std::cout << colorize("File already exists", Color::RED) << std::endl;
		}
		else { std::cout << colorize("File does not exist", Color::RED) << std::endl; }
		return 1;
	}

	std::cout << colorize("Server ready!\n", Color::GREEN) << std::endl;

	if ( command == Command::Type::UPLOAD )
		sendFile(file, fileSize, connection);
	else if ( command == Command::Type::DOWNLOAD )
		downloadFile(connection);

	return 0;
}
