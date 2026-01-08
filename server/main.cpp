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

void receiveFile ( ConnectionServer& connection ) {
	const auto size = stoll(connection.receiveInternal().substr(strlen("size:")));
	auto fileName = connection.receiveInternal().substr(strlen("filename:"));
	const auto hashFromClient = connection.receiveInternal().substr(strlen("hash:"));

	const auto oldFileName = fileName;

	// convert all '.' to '$' in the filename
	std::ranges::replace(fileName, '.', '<');

	std::filesystem::path _path = std::filesystem::current_path() / "storage" / ( fileName + '.' + hashFromClient );

	std::cout << _path << std::endl;

	if ( std::filesystem::exists(_path) ) {
		std::cout << "main: file already exists" << std::endl;
		connection.sendInternal("NO");
		return;
	}

	connection.sendInternal("OK");

	std::ofstream file(_path, std::ios::binary);

	std::cout << "main: starting download of size: " << size << std::endl;

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
			std::filesystem::remove(fileName);
			return;
		}

		if ( message.starts_with(_internal"DONE") )
			break;

		file.write(message.c_str(), message.size());
		sizeWritten += message.size();

		connection.sendInternal("confirm");

		crypto_generichash_update(&state, reinterpret_cast<const unsigned char*>(message.c_str()), message.size());

		std::cout << '\r' << "main: " << humanReadableSize(sizeWritten) << " / " << humanReadableSize(size) <<
				" bytes written" << std::flush;
	}
	std::cout << std::endl;
	file.close();

	crypto_generichash_final(&state, hash, sizeof hash);

	auto hashString = binToHex(hash, sizeof hash);

	auto newPath = std::filesystem::absolute(_path.string() + "." + hashString);

	std::filesystem::rename(_path, newPath);

	std::filesystem::create_symlink(newPath, std::filesystem::current_path()/ "links" / oldFileName);

	connection.sendInternal(hashString);
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

	connectionServer.sendInternal("OK");

	auto fileSize = std::filesystem::file_size(fileName);

	constexpr size_t chunkSize = 256 * 1024;
	auto buffer = std::make_unique<char[]>(chunkSize);

	const unsigned long long totalChunks = fileSize / chunkSize + 1;
	const size_t lastChunkSize = fileSize % chunkSize;

	connectionServer.sendInternal(std::to_string(fileSize));

	auto clientFileName = fileName.substr(0, fileName.find_last_of('.'));
	std::ranges::replace(clientFileName, '<', '.');

	connectionServer.sendInternal(clientFileName);

	for ( unsigned long long i = 0; i < totalChunks - 1; ++i ) {
		file.read(buffer.get(), chunkSize);
		connectionServer.send(std::string(buffer.get(), chunkSize));
		if ( connectionServer.receiveInternal() != "confirm" )
			throw std::runtime_error("main: client did not confirm the chunk");
	}

	file.read(buffer.get(), static_cast<std::streamsize>(lastChunkSize));
	connectionServer.send(std::string(buffer.get(), lastChunkSize));
	if ( connectionServer.receiveInternal() != "confirm" )
		throw std::runtime_error("main: client did not confirm the chunk");

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

	auto symlinkName = fileName.filename().string();
	std::ranges::replace(symlinkName, '<', '.');

	std::filesystem::remove(std::filesystem::current_path() / "links" / symlinkName);
	std::filesystem::remove(fileName);

	connectionServer.sendInternal("OK");
}

void serveConnection ( ClientInfo client ) {
	std::cout << "main: serving client " << client.getIp() << std::endl;

	ConnectionServer connection(std::move(client));

	connection.init();

	const auto message = connection.receiveInternal();

	std::cout << "main: received message: " << message << std::endl;

	if ( message == "command:UPLOAD" )
		receiveFile(connection);
	else if ( message == "command:DOWNLOAD" )
		sendFile(connection);
	else if ( message == "command:REMOVE" )
		removeFile(connection);
}

int main () {
	std::signal(SIGINT, signalHandler);
	std::signal(SIGTERM, signalHandler);
	std::signal(SIGPIPE, SIG_IGN);

	std::cout << "main: starting server" << std::endl;

	std::filesystem::create_directory("storage");
	std::filesystem::create_directory("links");


	std::ifstream authFile("auth");

	if ( !authFile.is_open() ) {
		std::cerr << "main: could not open auth file, please supply `auth` file in " << std::filesystem::current_path() << " directory." << std::endl;
		return 1;
	}

	/* expected format:
	 * userName
	 * userPass
	*/
	std::string authUser;
	std::string authPass;
	std::getline(authFile, authUser);
	std::getline(authFile, authPass);
	authFile.close();

	if ( authUser.empty() || authPass.empty() ) {
		std::cerr << "main: auth file is incorrect, please supply 'auth' file in " << std::filesystem::current_path() <<
			" directory in format:\n\tuserName\n\tuserPass" << std::endl;
		return 1;
	}

	std::cout << "main: auth file read successfully" << std::endl;

	const int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

	sockaddr_in serverAddress = {AF_INET, htons(6998), {INADDR_ANY}, {0}};

	if ( bind(serverSocket, reinterpret_cast<struct sockaddr*>(&serverAddress), sizeof( serverAddress )) < 0 ) {
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

	HTTPFileServer httpFileServer(
		turnOff,
		std::filesystem::absolute(std::filesystem::current_path() / "links").string()
		);

	auto httpThread = httpFileServer.run(authUser, authPass);

	std::cout << "main: entering main loop, server started" << std::endl;

	while ( true ) {
		std::unique_lock lock(mutex);
		callBack.wait(lock);

		if ( newClientAccepted ) {
			if ( !newClientAccepted )
				continue;

			clientSocket = acceptedClient.getSocket();

			try { serveConnection(std::move(acceptedClient)); }
			catch ( const std::exception& e ) { std::cerr << "main: error serving client: " << e.what() << std::endl; }
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
			httpThread.join();
			std::cout << "main: http closed" << std::endl;
			break;
		}
	}

	std::cout << "main: closing server" << std::endl;

	return 0;
}
