#include <algorithm>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>

#include "accepter.cpp"
#include "ConnectionHandler.hpp"
#include "HTTPFileServer.hpp"
#include "Settings.hpp"
#include "terminal.cpp"
#include "utils.hpp"

sig_atomic_t stopRequested = 0;
std::condition_variable callBack;

// used for graceful shutdown in docker
void signalHandler ( const int signal ) {
	Utils::log("Interrupt signal (" + std::to_string(signal) + ") received.");
	if ( signal == SIGINT || signal == SIGTERM )
		stopRequested = 1;

	callBack.notify_one();
}


int main () {
	std::signal(SIGINT, signalHandler);
	std::signal(SIGTERM, signalHandler);
	std::signal(SIGPIPE, SIG_IGN);

	Utils::log("main: starting server");

	std::filesystem::create_directory("storage");
	std::filesystem::create_directory("links");

	const Settings settings = Settings::loadFromFile("settings/settings.toml");

	Utils::log("main: settings file read successfully");

	Utils::log(settings.toString());

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

		Utils::log("main: http server started");
	}

	ConnectionHandler connectionHandler(settings);

	Utils::log("main: entering main loop, server started");

	while ( true ) {
		std::unique_lock lock(mutex);
		callBack.wait(lock);

		if ( newClientAccepted ) {
			if ( !newClientAccepted )
				continue;

			connectionHandler.addClient(acceptedClient);
			newClientAccepted = false;
		}

		if ( stopRequested )
			turnOff = true;

		if ( turnOff ) {
			// cleanup
			lock.unlock();
			Utils::log("main: cleaning up threads");
			if ( stopRequested ) {
				Utils::log("main: stop requested");
				pthread_cancel(terminalThread.native_handle());
			}
			else
				terminalThread.join();

			Utils::log("main: terminating clients, will wait on open transactions");
			connectionHandler.requestStop();

			Utils::log("main: terminal closed");
			shutdown(serverSocket, SHUT_RDWR);
			close(serverSocket);
			accepterThread.join();
			Utils::log("main: accepter closed");
			if ( settings.wantHttp )
				httpThread.join();
			Utils::log("main: http closed");
			break;
		}
	}

	Utils::log("main: closing server");

	return 0;
}
