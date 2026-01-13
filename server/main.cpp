#include <algorithm>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <thread>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>

#include "accepter.cpp"
#include "ConnectionServer.hpp"
#include "HTTPFileServer.hpp"
#include "terminal.cpp"
#include "utilServer.cpp"
#include "includes/toml.hpp"

sig_atomic_t stopRequested = 0;
std::condition_variable callBack;
int clientSocket = -1;

// used for graceful shutdown in docker
void signalHandler ( const int signal ) {
	std::cout << "Interrupt signal (" << signal << ") received." << std::endl;
	if ( signal == SIGINT || signal == SIGTERM )
		stopRequested = 1;

	shutdown(clientSocket, SHUT_RDWR);
	close(clientSocket);

	callBack.notify_one();
}

void receiveFile ( ConnectionServer& connection, const Settings& settings ) {
	const auto fileSize = stoll(connection.receiveInternal().substr(strlen("size:")));
	auto fileName = connection.receiveInternal().substr(strlen("filename:"));
	const auto hashFromClient = connection.receiveInternal().substr(strlen("hash:"));

	const auto oldFileName = fileName;

	auto freeRam = std::min(getFreeMemory() / 4, static_cast<unsigned long>(fileSize / 16));

	connection.resizeBuffer(freeRam);

	// convert all '.' to '$' in the filename
	std::ranges::replace(fileName, '.', '<');

	std::filesystem::path _path = std::filesystem::current_path() / "storage" / ( fileName + '.' + hashFromClient );

	std::cout << _path << std::endl;

	if ( std::filesystem::exists(_path) ) {
		std::cout << "main: file already exists" << std::endl;
		connection.sendInternal("NO");
		connection.sendInternal(settings.httpProtocol + "://" + settings.hostname + "/" + oldFileName);
		return;
	}

	connection.sendInternal("OK");

	std::ofstream file(_path, std::ios::binary);

	std::cout << "main: starting download of size: " << fileSize << std::endl;

	std::string message;
	long long sizeWritten = 0;

	unsigned char hash[crypto_generichash_BYTES];
	crypto_generichash_state state;
	crypto_generichash_init(&state, nullptr, 0, sizeof hash);

	while ( true ) {
		try { message = connection.receive(); }
		catch ( const std::exception& e ) {
			std::cerr << "main: error receiving message: " << e.what() << std::endl;
			file.close();
			std::filesystem::remove(_path);
			connection.sendInternal("fail");
			return;
		}

		if ( message.starts_with(_internal"DONE") )
			break;

		file.write(message.data(), message.size());
		sizeWritten += message.size();

		connection.sendInternal("confirm");

		crypto_generichash_update(&state, reinterpret_cast<const unsigned char*>(message.data()), message.size());

		std::cout << '\r' << "main: " << humanReadableSize(sizeWritten) << " / " << humanReadableSize(fileSize) <<
				" bytes written" << std::flush;
	}
	std::cout << std::endl;
	file.close();

	crypto_generichash_final(&state, hash, sizeof hash);

	auto hashString = binToHex(hash, sizeof hash);

	std::filesystem::create_symlink(_path, std::filesystem::current_path()/ "links" / oldFileName);

	connection.sendInternal(hashString);
	connection.sendInternal(std::to_string(settings.wantHttp));
	if ( settings.wantHttp )
		connection.sendInternal(settings.httpProtocol + "://" + settings.hostname + "/" + oldFileName);
}

void sendFile ( ConnectionServer& connectionServer ) {
	auto hash = connectionServer.receiveInternal().substr(strlen("hash:"));
	std::string fileName;

	for ( const auto& file: std::filesystem::directory_iterator("storage") )
		if ( file.path().extension() == '.' + hash )
			fileName = file.path();

	if ( fileName.empty() ) {
		std::cout << "main: file not found" << std::endl;
		connectionServer.sendInternal("NO");
		return;
	}

	std::ifstream file(fileName, std::ios::binary);

	if ( !file.good() ) {
		std::cout << "main: could not open file" << std::endl;
		connectionServer.sendInternal("NO");
		return;
	}

	const auto freeRam = getFreeMemory() / 4;

	connectionServer.sendInternal("OK");

	const auto fileSize = std::filesystem::file_size(fileName);

	size_t chunkSize = 4 * 1024 * 1024;
	auto buffer = std::make_unique<char[]>(chunkSize);

	connectionServer.sendInternal(std::to_string(fileSize));

	auto lastOfSlash = fileName.find_last_of('/');
	auto lastOfDot = fileName.find_last_of('.');

	auto clientFileName = fileName.substr(lastOfSlash+1, lastOfDot - lastOfSlash-1 );
	std::ranges::replace(clientFileName, '<', '.');

	connectionServer.sendInternal(clientFileName);

	std::cout << "main: starting upload of size: " << humanReadableSize(fileSize) << std::endl;

	size_t sizeRead = 0;

	while ( true ) {
		file.read(buffer.get(), chunkSize);

		const auto startUploadTime = std::chrono::high_resolution_clock::now();
		connectionServer.send(std::string(buffer.get(), file.gcount()));
		const auto endUploadTime = std::chrono::high_resolution_clock::now();

		std::chrono::duration<double> duration = endUploadTime - startUploadTime;

		sizeRead += file.gcount();

		if ( connectionServer.receiveInternal() != "confirm" )
			throw std::runtime_error("main: client did not confirm the chunk");

		if ( sizeRead == static_cast<unsigned long long>(fileSize) )
			break;

		// adjust chunk size based on duration
		if (duration.count() > 1.4) {
			// decrease chunkSize by 2%, ensure integer rounding
			chunkSize = static_cast<int>(chunkSize * 0.75);

			buffer = std::make_unique<char[]>(chunkSize);
		}
		else if ( duration.count() < 0.3 && freeRam >= chunkSize * 2 ) {
			chunkSize = static_cast<int>(chunkSize * 2);
			buffer = std::make_unique<char[]>(chunkSize);

		}
		else if ( duration.count() < 0.6 && freeRam >= chunkSize * 2 ) {
			chunkSize = static_cast<int>(chunkSize * 1.25);
			buffer = std::make_unique<char[]>(chunkSize);
		}
	}

	connectionServer.sendInternal("DONE");
}

void removeFile ( ConnectionServer& connectionServer ) {
	const auto hash = connectionServer.receiveInternal().substr(strlen("hash:"));

	std::filesystem::path fileName;

	for ( const auto& file: std::filesystem::directory_iterator("storage") )
		if ( file.path().extension() == '.' + hash )
			fileName = file.path();

	if ( fileName.empty() ) {
		std::cout << "main: file not found: " << fileName << std::endl;
		connectionServer.sendInternal("NO");
		return;
	}

	auto symlinkName = fileName.filename().string().substr(0,fileName.filename().string().find('.'));
	std::ranges::replace(symlinkName, '<', '.');


	std::cout << "main: removing files: " << (std::filesystem::current_path() / "links" / symlinkName) << ", " << fileName << std::endl;

	std::filesystem::remove(std::filesystem::current_path() / "links" / symlinkName);
	std::filesystem::remove(fileName);

	connectionServer.sendInternal("OK");
}

void serveConnection ( ClientInfo client, const Settings& settings ) {
	std::cout << "main: serving client " << client.getIp() << std::endl;

	ConnectionServer connection(client);
	try {
		connection.init();

		const auto message = connection.receiveInternal();

		std::cout << "main: received message: " << message << std::endl;
		//sleep(10);
		if ( message == "command:UPLOAD" )
			receiveFile(connection, settings);
		else if ( message == "command:DOWNLOAD" )
			sendFile(connection);
		else if ( message == "command:REMOVE" )
			removeFile(connection);
	} catch ( const std::exception& e ) {
		std::cerr << "main: error serving client: " << e.what() << std::endl;
	}
}

int main () {
	std::signal(SIGINT, signalHandler);
	std::signal(SIGTERM, signalHandler);
	std::signal(SIGPIPE, SIG_IGN);

	std::cout << "main: starting server" << std::endl;

	std::filesystem::create_directory("storage");
	std::filesystem::create_directory("links");

	const Settings settings = loadSettings();

	std::cout << "main: settings file read successfully" << std::endl;

	std::cout << settings << std::endl;

	const int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

	sockaddr_in serverAddress = {AF_INET, htons(6998), {INADDR_ANY}, {0}};

	if ( bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof( serverAddress )) < 0 ) {
		std::cerr << "main: could not bind server socket" << std::endl;
		close(serverSocket);
		return 1;
	}

	std::mutex mutex;

	bool turnOff = false;

	std::thread terminalThread(terminal, std::ref(callBack), std::ref(turnOff));

	listen(serverSocket, 10);

	bool newClientAccepted = false;
	ClientInfo acceptedClient;

	std::thread accepterThread(
		accepter,
		std::ref(callBack),
		std::ref(serverSocket),
		std::ref(acceptedClient),
		std::ref(newClientAccepted),
		std::ref(turnOff)
		);

	std::thread httpThread;

	if ( settings.wantHttp ) {
		HTTPFileServer httpFileServer(
		turnOff,
		std::filesystem::absolute(std::filesystem::current_path() / "links").string()
		);

		httpThread = httpFileServer.run(settings.authUser, settings.authPass, settings.httpAddress);

		std::cout << "main: http server started" << std::endl;
	}



	std::cout << "main: entering main loop, server started" << std::endl;

	while ( true ) {
		std::unique_lock lock(mutex);
		callBack.wait(lock);

		if ( newClientAccepted ) {
			if ( !newClientAccepted )
				continue;

			clientSocket = acceptedClient.getSocket();

			serveConnection(std::move(acceptedClient), settings);
			newClientAccepted = false;
		}

		if ( stopRequested )
			turnOff = true;

		if ( turnOff ) {
			// cleanup
			lock.unlock();
			std::cout << "main: cleaning up threads" << std::endl;
			if ( stopRequested ) {
				std::cout << "main: stop requested" << std::endl;
				pthread_cancel(terminalThread.native_handle());
			}
			else
				terminalThread.join();

			std::cout << "main: terminating client" << std::endl;
			if ( clientSocket != -1 ) {
				shutdown(clientSocket, SHUT_RDWR);
				close(clientSocket);
			}

			std::cout << "main: terminal closed" << std::endl;
			shutdown(serverSocket, SHUT_RDWR);
			close(serverSocket);
			accepterThread.join();
			std::cout << "main: accepter closed" << std::endl;
			if ( settings.wantHttp )
				httpThread.join();
			std::cout << "main: http closed" << std::endl;
			break;
		}
	}

	std::cout << "main: closing server" << std::endl;

	return 0;
}
