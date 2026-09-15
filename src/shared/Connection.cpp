#include "Connection.hpp"

#include <iostream>
#include <openssl/err.h>
#include <openssl/ssl.h>

Connection::Connection ( const unsigned long bufferSize ) : _buffer(std::make_unique<char[]>(bufferSize)), _bufferSize(bufferSize) {
	_ctx = SSL_CTX_new(OSSL_QUIC_client_method());
	if ( _ctx == nullptr ) {
		throw std::runtime_error("Failed to create the SSL_CTX");
	}

	memset(_buffer.get(), '\0', _bufferSize);
}

void Connection::connectToServer ( std::string ip, const int port, const bool forceServerCertVerify ) {
	// implementation heavily inspired by https://github.com/openssl/openssl/blob/master/demos/guide/quic-client-block.c#L30

	int sock = -1;
	BIO_ADDRINFO *res;
	const BIO_ADDRINFO* ai;
	BIO_ADDR *peerAddr = nullptr;

	if ( !BIO_lookup_ex(ip.c_str(), std::to_string(port).c_str(), BIO_LOOKUP_CLIENT, AF_INET, SOCK_DGRAM, 0, &res) ) {
		throw std::runtime_error("Could not resolve the target server.");
	}

	/*
	 * Loop through all the possible addresses for the server and find one
	 * we can connect to.
	 */
	for ( ai = res; ai != nullptr; ai = BIO_ADDRINFO_next(ai)) {
		/*
		 * Create a TCP socket. We could equally use non-OpenSSL calls such
		 * as "socket" here for this and the subsequent connect and close
		 * functions. But for portability reasons and also so that we get
		 * errors on the OpenSSL stack in the event of a failure we use
		 * OpenSSL's versions of these functions.
		 */
		sock = BIO_socket(BIO_ADDRINFO_family(ai), SOCK_DGRAM, 0, 0);
		if ( sock == -1 )
			continue;

		/* Connect the socket to the server's address */
		if ( !BIO_connect(sock, BIO_ADDRINFO_address(ai), 0) ) {
			BIO_closesocket(sock);
			sock = -1;
			continue;
		}

		/* Set to nonblocking mode */
		if (!BIO_socket_nbio(sock, 1)) {
			BIO_closesocket(sock);
			sock = -1;
			continue;
		}

		break;
	}

	if ( sock != -1 ) {
		peerAddr = BIO_ADDR_dup(BIO_ADDRINFO_address(ai));
		if ( peerAddr == nullptr ) {
			BIO_closesocket(sock);
			return;
		}
	}

	/* Free the address information resources we allocated earlier */
	BIO_ADDRINFO_free(res);

	if (sock == -1)
		throw std::runtime_error("Could not connect");

	/* Create a BIO to wrap the socket */
	_bio = BIO_new(BIO_s_datagram());
	if (_bio == nullptr ) {
		BIO_closesocket(sock);
		throw std::runtime_error("Could not connect");
	}

	/*
	 * Associate the newly created BIO with the underlying socket. By
	 * passing BIO_CLOSE here the socket will be automatically closed when
	 * the BIO is freed. Alternatively you can use BIO_NOCLOSE, in which
	 * case you must close the socket explicitly when it is no longer
	 * needed.
	 */
	BIO_set_fd(_bio, sock, BIO_CLOSE);

	_ssl = SSL_new(_ctx);

	if (!SSL_set_tlsext_host_name(_ssl, ip.c_str())) {
		throw std::runtime_error("Failed to set the SNI hostname\n");
	}

	unsigned char alpn[] = { 9, 'h', 'i', 'k', 'u', 'p', '/', '1', '.', '0' };

	/* SSL_set_alpn_protos returns 0 for success! */
	if (SSL_set_alpn_protos(_ssl, alpn, sizeof(alpn)) != 0) {
		throw std::runtime_error("Failed to set the ALPN for the connection\n");
	}

	/* Set the IP address of the remote peer */
	if (!SSL_set1_initial_peer_addr(_ssl, peerAddr)) {
		throw std::runtime_error("Failed to set the initial peer address\n");
	}

	BIO_ADDR_free(peerAddr);

	if ( !forceServerCertVerify )
		SSL_CTX_set_verify(_ctx, SSL_VERIFY_NONE, nullptr);

	SSL_set_bio(_ssl, _bio, _bio);

	/* Do the handshake with the server */
	if ( const auto ret = SSL_connect(_ssl); ret < 1) {
		std::string retStr;
		switch ( const auto err = SSL_get_error(_ssl, ret) ) {
			case SSL_ERROR_ZERO_RETURN:
				retStr = "The TLS/SSL peer has closed the connection for writing by sending the close_notify alert";
				break;
			case SSL_ERROR_WANT_X509_LOOKUP:
				retStr = "SSL_ERROR_WANT_X509_LOOKUP";
				break;
			case SSL_ERROR_SYSCALL:
				retStr = "SSL_ERROR_SYSCALL";
				break;
			case SSL_ERROR_SSL:
				retStr = "SSL_ERROR_SSL";
				ERR_print_errors_fp(stderr);
				break;
			case SSL_ERROR_WANT_CLIENT_HELLO_CB:
				retStr = "SSL_ERROR_WANT_CLIENT_HELLO_CB";
				break;
			case SSL_ERROR_WANT_ASYNC_JOB:
				retStr = "SSL_ERROR_WANT_ASYNC_JOB";
				break;
			case SSL_ERROR_WANT_ASYNC:
				retStr = "SSL_ERROR_WANT_ASYNC";
				break;
			case SSL_ERROR_WANT_CONNECT:
			case SSL_ERROR_WANT_ACCEPT:
				retStr = "SSL_ERROR_WANT_ACCEPT/CONNECT";
				break;
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				retStr = "SSL_ERROR_WANT_WRITE/READ";
				break;
			default:
				retStr = "This shouldn't have happened: " + std::to_string(err);
		}
		/*
		 * If the failure is due to a verification error we can get more
		 * information about it from SSL_get_verify_result().
		 */
		if (SSL_get_verify_result(_ssl) != X509_V_OK)
			throw std::runtime_error("Certificate verify error: " + std::string(X509_verify_cert_error_string(SSL_get_verify_result(_ssl))));
		throw std::runtime_error("Failed to connect to the server: " + retStr);
	}
}

void Connection::_send ( const char* message, const size_t length ) {
	std::lock_guard<std::mutex> lock(_sendMutex);

	size_t written = 0;
	if ( !SSL_write_ex2(_ssl, message, length, SSL_WRITE_FLAG_CONCLUDE, &written) || written != length ) {
		throw std::runtime_error("Could not send message");
	}

}

std::string Connection::_receive () {
	std::string message;


	while ( !message.ends_with(_end) ) {
		clearBuffer();

		if ( const auto ret = SSL_read_ex(_ssl, _buffer.get(), _bufferSize, &_sizeOfPreviousMessage); ret <= 0 ) {
			switch ( const int err = SSL_get_error(_ssl, ret) ) {

				case SSL_ERROR_SYSCALL:
					if ( errno != EAGAIN && errno != EWOULDBLOCK )
						throw std::runtime_error("client disconnected or could not receive message");
					if ( errno == EAGAIN || errno == EWOULDBLOCK )
						throw std::runtime_error("timeout");
					break;

				case SSL_ERROR_ZERO_RETURN:
				case SSL_ERROR_SSL:
					throw std::runtime_error("server disconnected");

				default:
					ERR_print_errors_fp(stderr);
					throw std::logic_error("unexpected error: " + std::to_string(err));
			}
		}

		message += std::string(_buffer.get(), _sizeOfPreviousMessage);
	}

	return message;
}

Connection& Connection::send ( const std::string& message ) {
	auto messageToSend = message;

#ifdef HIKUP_CONN_DEBUG
	printf("SEND | %s\n", messageToSend.c_str());
#endif


	messageToSend += _end;

	//std::cout << "\nSEND | " << messageToSend << std::endl;

	_send(messageToSend.data(), messageToSend.size());

	return *this;
}

Connection& Connection::sendData ( const std::string& message ) { return send(_data + message); }

Connection& Connection::sendInternal ( const std::string& message ) { return send(_internal + message); }

std::string Connection::receive () {

	if ( _moreInBuffer ) {
		auto message = _messagesBuffer[0];
		_messagesBuffer.erase(_messagesBuffer.begin());
		if ( _messagesBuffer.empty() )
			_moreInBuffer = false;
		return message;
	}

	auto message = _receive();

	// remove the _end string

	std::vector<std::string> messages;
	std::string tmpMessage;
	size_t sPos = 0, ePos = 0;
	bool first = true;

	while ( ( ePos = message.find(_end, sPos) ) != std::string::npos ) {
		if ( first ) {
			tmpMessage = message.substr(sPos, ePos-sPos);
			first = false;
		}
		else
			messages.push_back(message.substr(sPos, ePos-sPos));


		sPos = ePos + strlen(_end);
	}

	message = tmpMessage;
	_messagesBuffer = messages;

	if ( _messagesBuffer.empty() )
		_moreInBuffer = false;
	else
		_moreInBuffer = true;


#ifdef HIKUP_CONN_DEBUG
	std::cout << "RECEIVE | " << message << std::endl;
	std::cout << "RECEIVE BUFFER | "
			  << std::accumulate(_messagesBuffer.begin(), _messagesBuffer.end(), std::string(),
								 [](const std::string &a, const std::string &b) {
									 return a + b + " | ";
								 })
			  << std::endl;
#endif


	return message;
}

std::tuple<std::string, std::chrono::duration<double>> Connection::receiveWTime () {

	if ( _moreInBuffer ) {
		auto message = _messagesBuffer[0];
		_messagesBuffer.erase(_messagesBuffer.begin());
		if ( _messagesBuffer.empty() )
			_moreInBuffer = false;
		return {message, std::chrono::duration<double>(0)};
	}

	const auto start = std::chrono::high_resolution_clock::now();
	auto message = _receive();
	const auto end = std::chrono::high_resolution_clock::now();

	// remove the _end string

	std::vector<std::string> messages;
	std::string tmpMessage;
	size_t sPos = 0, ePos = 0;
	bool first = true;

	while ( ( ePos = message.find(_end, sPos) ) != std::string::npos ) {
		if ( first ) {
			tmpMessage = message.substr(sPos, ePos-sPos);
			first = false;
		}
		else
			messages.push_back(message.substr(sPos, ePos-sPos));


		sPos = ePos + strlen(_end);
	}

	message = tmpMessage;
	_messagesBuffer = messages;

	if ( _messagesBuffer.empty() )
		_moreInBuffer = false;
	else
		_moreInBuffer = true;


#ifdef HIKUP_CONN_DEBUG
	std::cout << "RECEIVE | " << message << std::endl;
	std::cout << "RECEIVE BUFFER | "
			  << std::accumulate(_messagesBuffer.begin(), _messagesBuffer.end(), std::string(),
								 [](const std::string &a, const std::string &b) {
									 return a + b + " | ";
								 })
			  << std::endl;
#endif


	return {message, end - start};
}

std::string Connection::receiveInternal () {
	const auto message = receive();

	if ( !message.contains(_internal) )
		throw std::runtime_error("Invalid message received (internal)");

	return message.substr(strlen(_internal));
}

std::string Connection::receiveData () {
	const auto message = receive();

	if ( !message.contains(_data) )
		throw std::runtime_error("Invalid message received (data)");

	return message.substr(strlen(_data));
}

bool Connection::isConnected () const {
	return _active;
}

void Connection::resizeBuffer ( const unsigned long newSize )  {
	_buffer = std::make_unique<char[]>(newSize);
	_bufferSize = newSize;
}

void Connection::close () {
	/*
	 * Repeatedly call SSL_shutdown() until the connection is fully
	 * closed.
	 */
	int ret;
	do {
		ret = SSL_shutdown(_ssl);
		if (ret < 0) {
			throw std::runtime_error("Error shutting down: " + std::to_string(ret));
		}
	} while (ret != 1);

	_active = false;
}

Connection::~Connection () {
	if ( _active )
		close();
}

void Connection::clearBuffer () const { memset(_buffer.get(), '\0', _bufferSize); }
