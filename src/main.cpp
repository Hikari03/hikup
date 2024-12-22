#include <iostream>

#include "Connection.h"
#include "util.cpp"
#include "CommandType.cpp"

void sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection ) {
	if ( !file.good() ) {
		std::cerr << colorize("Could not open file", Color::RED) << std::endl;
		return;
	}

	// we will send the file in chunks of 128KB
	constexpr size_t chunkSize = 128 * 1000;
	auto buffer = std::make_unique<char[]>(chunkSize);

	const unsigned long long totalChunks = fileSize / chunkSize + 1;
	const size_t lastChunkSize = fileSize % chunkSize;

	double readSpeed = 0.0, uploadSpeed = 0.0;

	double totalTimeRead = 0.0, totalTimeUpload = 0.0;

	long long sizeRead = 0;
	unsigned long long sizeUploaded = 0;

	std::cout << colorize("Starting upload of size: ", Color::BLUE) << colorize(
		humanReadableSize(fileSize), Color::CYAN) << "\n" << std::endl;

	for ( unsigned long long i = 0; i < totalChunks - 1; ++i ) {
		auto startReadTime = std::chrono::high_resolution_clock::now();
		file.read(buffer.get(), chunkSize);
		auto endReadTime = std::chrono::high_resolution_clock::now();

		std::chrono::duration<double> duration = endReadTime - startReadTime;

		totalTimeRead += duration.count();
		sizeRead += chunkSize;

		readSpeed = static_cast<double>(sizeRead) / totalTimeRead;

		auto startUploadTime = std::chrono::high_resolution_clock::now();

		connection.send(std::string(buffer.get(), chunkSize));

		auto endUploadTime = std::chrono::high_resolution_clock::now();

		duration = endUploadTime - startUploadTime;

		totalTimeUpload += duration.count();
		sizeUploaded += chunkSize;

		uploadSpeed = static_cast<double>(sizeUploaded) / totalTimeUpload;

		std::cout << "\r" << colorize("Sending data: ", Color::BLUE) +
				colorize(humanReadableSize(( i + 1 ) * chunkSize), Color::CYAN) + colorize("/", Color::BLUE) +
				colorize(humanReadableSize(( totalChunks + 1 ) * chunkSize), Color::CYAN) + colorize(
					std::string(" (") +
					std::to_string(( static_cast<double>(i + 1) / static_cast<double>(totalChunks) ) * 100.0).
					substr(0, 5) + " %)",
					Color::PURPLE) + " ┃ " + colorize("Read: " + humanReadableSpeed(readSpeed), Color::LL_BLUE) + " ━━ "
				+ colorize("Up: " + humanReadableSpeed(uploadSpeed), Color::GREEN) + "  " << std::flush;
	}

	file.read(buffer.get(), static_cast<std::streamsize>(lastChunkSize));
	connection.send(std::string(buffer.get(), lastChunkSize));
	std::cout << "\r" << colorize("Sending data: ", Color::BLUE) +
			colorize(humanReadableSize(totalChunks * chunkSize), Color::CYAN) + colorize("/", Color::BLUE) + colorize(
				humanReadableSize(totalChunks * chunkSize),
				Color::CYAN) + colorize(" (100 %)  ", Color::PURPLE) << std::endl;

	connection.sendInternal("DONE");
	auto hash = connection.receiveInternal();
	std::cout << colorize("File uploaded successfully with hash: ", Color::GREEN) + colorize(hash, Color::CYAN) <<
			std::endl;
}

void downloadFile ( Connection& connection ) {
	auto fileSize = std::stoll(connection.receiveInternal());
	auto fileName = connection.receiveInternal();
	double totalTimeDownload = 0.0, totalTimeWrite = 0.0;

	long long sizeWritten = 0;
	unsigned long long sizeDownloaded = 0;


	std::cout << colorize("Downloading file: ", Color::BLUE) + colorize(fileName, Color::CYAN) << colorize(
		" of size: ", Color::BLUE) << colorize(humanReadableSize(fileSize), Color::CYAN) << std::endl;

	// create file
	std::ofstream file(fileName, std::ios::binary);

	while ( true ) {
		auto startDownloadTime = std::chrono::high_resolution_clock::now();
		auto chunk = connection.receive();
		auto endDownloadTime = std::chrono::high_resolution_clock::now();

		if ( chunk == _internal"DONE" )
			break;

		// calculate download speed
		std::chrono::duration<double> duration = endDownloadTime - startDownloadTime;

		sizeDownloaded += chunk.size();
		totalTimeDownload += duration.count();

		auto downloadSpeed = static_cast<double>(sizeDownloaded) / totalTimeDownload;

		auto writeStart = std::chrono::high_resolution_clock::now();
		file.write(chunk.c_str(), static_cast<long>(chunk.size()));
		auto writeEnd = std::chrono::high_resolution_clock::now();

		duration = writeEnd - writeStart;

		sizeWritten += chunk.size();
		totalTimeWrite += duration.count();

		auto writeSpeed = static_cast<double>(sizeWritten) / totalTimeWrite;

		std::cout << "\r" << colorize("Receiving data: ", Color::BLUE) +
				colorize(humanReadableSize(sizeWritten), Color::CYAN) + colorize("/", Color::BLUE) +
				colorize(humanReadableSize(fileSize), Color::CYAN) + colorize(
					std::string(" (") +
					std::to_string(( static_cast<double>(sizeWritten) / static_cast<double>(fileSize) ) * 100.0).
					substr(0, 5) + " %)",
					Color::PURPLE) + " ┃ " + colorize("Write: " + humanReadableSpeed(writeSpeed), Color::LL_BLUE) +
				" ━━ " + colorize("Down: " + humanReadableSpeed(downloadSpeed), Color::GREEN) + "  " << std::flush;
	}
}

int main ( int argc, char* argv[] ) {
	if ( argc < 4 ) {
		std::cout << "Usage: " << argv[0] << " <up <file> | down <hash> | rm <hash>> <server> \n\n"
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
		command = Command::resolveCommand(argv[1]);
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

	std::string hash;

	connection.sendInternal("command:" + Command::toString(command));
	if ( command == Command::Type::UPLOAD ) {
		connection.sendInternal("size:" + std::to_string(fileSize));
		connection.sendInternal("filename:" + fileName);

		hash = computeHash(file);

		connection.sendInternal("hash:" + hash);
	}
	else if ( command == Command::Type::DOWNLOAD || command == Command::Type::REMOVE ) {
		fileName = argv[2];
		connection.sendInternal("hash:" + fileName);
	}

	if ( connection.receiveInternal() != "OK" ) {
		std::cerr << colorize("Server did not accept the request\n", Color::RED);

		if ( command == Command::Type::UPLOAD ) {
			std::cout << colorize("Reason: file already exists\n", Color::RED) << colorize("Hash: ", Color::PURPLE) <<
					colorize(hash, Color::CYAN) << std::endl;
		}
		else { std::cout << colorize("Reason: file does not exist", Color::RED) << std::endl; }
		return 1;
	}

	if ( command == Command::Type::REMOVE ) {
		std::cout << colorize("File with hash ", Color::GREEN) << colorize(fileName, Color::CYAN) << colorize(
			" removed!", Color::RED) << std::endl;
		return 0;
	}
	std::cout << colorize("Server ready!\n", Color::GREEN) << std::endl;

	if ( command == Command::Type::UPLOAD )
		sendFile(file, fileSize, connection);
	else if ( command == Command::Type::DOWNLOAD )
		downloadFile(connection);

	return 0;
}
