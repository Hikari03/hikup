#include <condition_variable>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <list>
#include <thread>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>
#include <filesystem>

#include "terminal.cpp"
#include "accepter.cpp"
#include "ConnectionServer.h"
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

	// convert all '.' to '$' in the filename
	std::replace(fileName.begin(), fileName.end(), '.', '<');

	std::filesystem::path _path = std::filesystem::absolute(fileName + '.' + hashFromClient);

	std::cout << _path << std::endl;

	if ( std::filesystem::exists(_path)) {
		std::cout << "main: file already exists" << std::endl;
		connection.sendInternal("NO");
		return;
	}

	connection.sendInternal("OK");

	std::ofstream file(fileName, std::ios::binary);

	std::cout << "main: starting download of size: " << size << std::endl;

	std::string message;
	long long sizeWritten = 0;

	unsigned char hash[crypto_generichash_BYTES];
	crypto_generichash_state state;
	crypto_generichash_init(&state, nullptr, 0, sizeof hash);

	while ( true ) {
		message = connection.receive();

		if ( message.starts_with(_internal"DONE") )
			break;

		file.write(message.c_str(), message.size());
		sizeWritten += message.size();

		crypto_generichash_update(&state, reinterpret_cast<const unsigned char*>(message.c_str()), message.size());

		std::cout << '\r' << "main: " << humanReadableSize(sizeWritten) << " / " << humanReadableSize(size) << " bytes written" << std::flush;
	}
	std::cout << std::endl;
	file.close();

	crypto_generichash_final(&state, hash, sizeof hash);

	std::ifstream fileStream(fileName, std::ios::binary);

	auto hashString = binToHex(hash, sizeof hash);

	std::filesystem::rename(fileName, fileName + "." + hashString);

	connection.sendInternal(hashString);
}

void serveConnection ( ClientInfo client ) {
	std::cout << "main: serving client " << client.getIp() << std::endl;

	ConnectionServer connection(std::move(client));

	connection.init();

	auto message = connection.receiveInternal();

	std::cout << "main: received message: " << message << std::endl;

	if ( message == "command:UPLOAD" )
		receiveFile(connection);
}

int main () {
	std::signal(SIGINT, signalHandler);
	std::signal(SIGTERM, signalHandler);

	std::cout << "main: starting server" << std::endl;

	const int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

	sockaddr_in serverAddress = {AF_INET, htons(6998), INADDR_ANY, {0}};

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

	std::thread accepterThread(accepter, std::ref(callBack), std::ref(serverSocket), std::ref(acceptedClient),
	                           std::ref(newClientAccepted), std::ref(turnOff));

	std::cout << "main: entering main loop, server started" << std::endl;

	while ( true ) {
		std::unique_lock lock(mutex);
		callBack.wait(lock);

		if ( newClientAccepted ) {
			if ( !newClientAccepted )
				continue;

			clientSocket = acceptedClient.getSocket();

			try {
				serveConnection(std::move(acceptedClient));
			}
			catch ( const std::exception& e ) {
				std::cerr << "main: error serving client: " << e.what() << std::endl;
			}
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
			break;
		}
	}

	std::cout << "main: closing server" << std::endl;

	return 0;
}
