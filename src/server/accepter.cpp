#include <condition_variable>
#include <iostream>
#include <openssl/ssl.h>

#include "ClientInfo.hpp"
#include "utils.hpp"

inline void accepter ( SSL_CTX* ctx,
					   std::condition_variable& callBack,
                       const int& serverSocket,
                       ClientInfo& acceptedClient,
                       bool& newClientAccepted,
                       const bool& turnOff ) {

	SSL *listener;

	if ( (listener = SSL_new_listener(ctx, 0)) == NULL ) {
		throw std::logic_error("accepter: could not create listener");
	}

	if ( !SSL_set_fd(listener, serverSocket) ) {
		throw std::logic_error("accepter: could not bind socket to listener");
	}

	if ( !SSL_listen(listener) ) {
		throw std::logic_error("accepter: could not start listening");
	}


	while ( true ) {
		if ( turnOff ) {
			SSL_free(listener);
			return;
		}

		if ( !newClientAccepted ) {
			std::this_thread::sleep_for(std::chrono::seconds(2));

			const auto conn = SSL_accept_connection(listener, SSL_ACCEPT_CONNECTION_NO_BLOCK);

			SSL_set_blocking_mode(conn, 1);

			if ( turnOff )
				return;

			if ( conn == nullptr ) {
				continue;
			}

			acceptedClient = ClientInfo(conn);

			Utils::log("main: accepted client number " + std::to_string(SSL_get_fd(acceptedClient.getConn())) + " with addr " + acceptedClient.getIp());

			newClientAccepted = true;
			callBack.notify_one();
		}
	}
}
