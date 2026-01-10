#include <iostream>

#include "CommandType.cpp"
#include "Connection.hpp"
#include "util.cpp"

void sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection ) {
	if ( !file.good() ) {
		std::cerr << colorize("Could not open file", Color::RED) << std::endl;
		return;
	}

	// we will send the file in chunks of 128KB
	constexpr size_t chunkSize = 4 * 1024 * 1024;
	const auto buffer = std::make_unique<char[]>(chunkSize);

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

		if ( connection.receiveInternal() != "confirm" )
			throw std::runtime_error("Server did not confirm the chunk");

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

	if ( connection.receiveInternal() != "confirm" )
		throw std::runtime_error("Server did not confirm the chunk");

	connection.sendInternal("DONE");
	const auto hash = connection.receiveInternal();
	const bool httpExists = std::stoi(connection.receiveInternal());

	std::cout << colorize("File uploaded successfully with hash: ", Color::GREEN) + colorize(hash, Color::CYAN) <<
			std::endl;

	if ( httpExists == true ) {
		connection.sendInternal("getHttpLink");
		const auto httpLink = connection.receiveInternal();
		std::cout << colorize("HTTP link: ", Color::GREEN) + colorize(httpLink, Color::CYAN) << std::endl;
	}
	else { std::cout << colorize("HTTP link: ", Color::GREEN) + colorize("not available", Color::CYAN) << std::endl; }

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
		auto [chunk,duration] = connection.receiveWTime();

		if ( chunk == _internal"DONE" )
			break;

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

		connection.sendInternal("confirm");

		std::cout << "\r" << colorize("Receiving data: ", Color::BLUE) <<
				colorize(humanReadableSize(sizeWritten), Color::CYAN) << colorize("/", Color::BLUE) <<
				colorize(humanReadableSize(fileSize), Color::CYAN) << colorize(
					std::string(" (") +
					std::to_string(( static_cast<double>(sizeWritten) / static_cast<double>(fileSize) ) * 100.0).
					substr(0, 5) + " %)",
					Color::PURPLE) << " ┃ " << colorize("Write: " + humanReadableSpeed(writeSpeed), Color::LL_BLUE) <<
				" ━━ " + colorize("Down: " + humanReadableSpeed(downloadSpeed), Color::GREEN) << "  " << std::flush;
	}

	std::cout << std::endl;
}

int start( int argc, char* argv[] ) {
	if ( argc < 4 ) {
		std::cout << "Usage: " << argv[0] << " <up <file> | down <hash> | rm <hash>> <server> \n\n"
				"If file is successfully uploaded, you will get file hash\n"
				"which you need to input if you want to download it\n\n"
		        "If server has HTTP server, you will get link for download.\n"
				"You can append '?inplace=yes' to the link to view the file in browser." << std::endl;
		return 1;
	}

	auto command = Command::resolveCommand(argv[1]);

	unsigned long freeRam = 1024;

	if ( command == Command::Type::DOWNLOAD ) {
		freeRam = getFreeMemory();
		freeRam = freeRam > 1024 * 1024 * 512 ? 1024 * 1024 * 512 : freeRam; // be under 512 MiB
		std::cout << colorize("Ram usage will be: ", Color::BLUE) << colorize(humanReadableSize(freeRam), Color::LL_BLUE) << std::endl;
	}


	Connection connection(freeRam / 4);
	std::ifstream file;
	std::ifstream::pos_type fileSize;
	std::string fileName;
	std::string hash;

	try {
		if ( command == Command::Type::UPLOAD ) {
			auto [_file, _fileSize, _fileName] = resolveFile(argv[2]);
			file = std::move(_file);
			fileSize = _fileSize;
			fileName = _fileName;

			std::cout << colorize("Computing hash", Color::GREEN) << std::endl;
			hash = computeHash(file);
			std::cout << colorize("Hash computed", Color::GREEN) << std::endl;
		}

		std::cout << colorize("Connecting to server", Color::GREEN) << std::endl;
		connection.connectToServer(argv[3], 6998);
		std::cout << colorize("Connected to server", Color::GREEN) << std::endl;
	}
	catch ( std::runtime_error& e ) {
		std::cerr << colorize(e.what(), Color::RED) << std::endl;
		return 1;
	}

	connection.sendInternal("command:" + Command::toString(command));
	if ( command == Command::Type::UPLOAD ) {
		connection.sendInternal("size:" + std::to_string(fileSize));
		connection.sendInternal("filename:" + fileName);
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
			const auto httpLink = connection.receiveInternal();
			std::cout << colorize("HTTP link: ", Color::PURPLE) << colorize(httpLink , Color::CYAN) << std::endl;
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

int main ( const int argc, char* argv[] ) {

	std::ios::sync_with_stdio(false);
	std::cin.tie(nullptr);

	try {
		return start(argc, argv);
	} catch ( std::exception& e ) {
		std::cerr << colorize(e.what(), Color::RED) << std::endl;
		return 1;
	}
}
