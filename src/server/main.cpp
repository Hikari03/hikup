#include <algorithm>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/quic.h>
#include <sys/socket.h>

#include "accepter.cpp"
#include "CertGen.hpp"
#include "ConnectionHandler.hpp"
#include "HTTPFileServer.hpp"
#include "Settings.hpp"
#include "terminal.cpp"
#include "utils.hpp"

sig_atomic_t stopRequested = 0;
std::condition_variable callBack;

// used for graceful shutdown in docker
static void signalHandler ( const int signal ) {
	Utils::log("Interrupt signal (" + std::to_string(signal) + ") received.");
	if ( signal == SIGINT || signal == SIGTERM )
		stopRequested = 1;

	callBack.notify_one();
}

/*
* ALPN strings for TLS handshake. Only 'hikup/1.0' is accepted.
*/
static constexpr unsigned char alpn_ossltest[] = {
	9, 'h', 'i', 'k', 'u', 'p', '/', '1', '.', '0',
};

static int select_alpn ( [[maybe_unused]] SSL* ssl,
                         const unsigned char** out,
                         unsigned char* out_len,
                         const unsigned char* in,
                         const unsigned int in_len,
                         [[maybe_unused]] void* arg ) {
	if ( SSL_select_next_proto(const_cast<unsigned char**>(out), out_len, alpn_ossltest, sizeof( alpn_ossltest ), in,
	                           in_len) == OPENSSL_NPN_NEGOTIATED )
		return SSL_TLSEXT_ERR_OK;
	return SSL_TLSEXT_ERR_ALERT_FATAL;
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

	auto ctx = SSL_CTX_new(OSSL_QUIC_server_method());
	if ( ctx == nullptr ) {
		std::cerr << "main: could not init SSL" << std::endl;
		return 1;
	}

	if ( !std::filesystem::exists("ca_key.pem") || !std::filesystem::exists("ca_cert.pem") )
		CertGen::generate();

	if ( SSL_CTX_use_certificate_chain_file(ctx, "ca_cert.pem") <= 0 ) {
		std::cerr << "main: couldn't load certificate file: ca_cert.pem" << std::endl;
		return 1;
	}

	if ( SSL_CTX_use_PrivateKey_file(ctx, "ca_key.pem", SSL_FILETYPE_PEM) <= 0 ) {
		std::cerr << "main: couldn't load private key file: ca_key.pem" << std::endl;
		return 1;
	}

	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

	SSL_CTX_set_alpn_select_cb(ctx, select_alpn, NULL);


	const int serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if ( serverSocket < 0 ) {
		std::cerr << "main: couldn't init socket" << std::endl;
		return 1;
	}

	sockaddr_in serverAddress = {AF_INET, htons(6998), {INADDR_ANY}, {0}};

	// Allow both IPv4 and IPv6 on this one socket
	int no = 0;
	if ( setsockopt(serverSocket, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no)) < 0 ) {
		std::cerr << "warning: could not set IPV6_V6ONLY=0 (" << strerror(errno) << "), relying on system default\n";
	}

	// allow quick rebind during restarts
	int yes = 1;
	setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	struct sockaddr_in6 addr{};
	addr.sin6_family = AF_INET6;
	addr.sin6_addr = in6addr_any; // binds to ::  (all interfaces, v4 + v6)
	addr.sin6_port = htons(serverSocket);

	if ( bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof( serverAddress )) < 0 ) {
		std::cerr << "main: could not bind server socket" << std::endl;
		BIO_closesocket(serverSocket);
		return 1;
	}

	std::mutex mutex;

	bool turnOff = false;

	std::thread terminalThread(terminal, std::ref(callBack), std::ref(turnOff));


	bool newClientAccepted = false;
	ClientInfo acceptedClient;

	std::thread accepterThread(accepter, ctx, std::ref(callBack), std::ref(serverSocket), std::ref(acceptedClient),
	                           std::ref(newClientAccepted), std::ref(turnOff));

	std::thread httpThread;

	if ( settings.wantHttp ) {
		HTTPFileServer httpFileServer(
			turnOff, std::filesystem::absolute(std::filesystem::current_path() / "links").string(),
			settings.httpDisplayInBrowser);

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
			accepterThread.join();
			SSL_CTX_free(ctx);
			BIO_closesocket(serverSocket);
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
