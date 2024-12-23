#include "Connection.h"

#include <iostream>

Connection::Connection () {
#ifdef __linux__
	_socket = socket(AF_INET, SOCK_STREAM, 0);
	if ( _socket == -1 ) { throw std::runtime_error("Could not create socket"); }
#elif _WIN32
	WSADATA _wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &_wsaData) != 0) {
		throw std::runtime_error("Could not initialize Winsock");
	}

	ZeroMemory( &_hints, sizeof(_hints) );
	_hints.ai_family   = AF_INET;
	_hints.ai_socktype = SOCK_STREAM;
	_hints.ai_protocol = IPPROTO_TCP;
#endif

	if ( sodium_init() < 0 ) { throw std::runtime_error("Could not initialize sodium"); }

	crypto_box_keypair(_keyPair.publicKey, _keyPair.secretKey);
	memset(_buffer, '\0', 4096);
}

void Connection::connectToServer ( std::string ip, int port ) {
	if ( ip == "localhost" || ip.empty() )
		ip = "127.0.0.1";

#ifdef __linux__
	_server.sin_family = AF_INET;
	_server.sin_port = htons(port);


	if ( inet_pton(AF_INET, ip.c_str(), &_server.sin_addr) <= 0 ) {
		if ( auto result = dnsLookup(ip); result.empty() ) {
			throw std::runtime_error("Invalid address / Address not supported");
		}
		else {
			ip = result[0];
			if ( inet_pton(AF_INET, ip.c_str(), &_server.sin_addr) <= 0 ) {
				throw std::runtime_error("Invalid address / Address not supported");
			}
		}
	}

	if ( connect(_socket, (struct sockaddr*)&_server, sizeof( _server )) < 0 ) {
		throw std::runtime_error("Could not connect to server");
	}

	receive(); // encryption initialization


#elif _WIN32
	if(getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &_hints, &_result) != 0) {
		WSACleanup();
		throw std::runtime_error("Could not get address info");
	}

    _ptr = _result;

	_socket = socket(_ptr->ai_family, _ptr->ai_socktype, _ptr->ai_protocol);
	if (_socket == INVALID_SOCKET) {
		WSACleanup();
		throw std::runtime_error("Could not create socket");
	}

	if (connect(_socket, _ptr->ai_addr, (int)_ptr->ai_addrlen) == SOCKET_ERROR) {
		closesocket(_socket);
		_socket = INVALID_SOCKET;
		freeaddrinfo(_result);
		throw std::runtime_error("Could not connect to server");
	}
	freeaddrinfo(_result);

#endif
}

void Connection::_send ( const char* message, size_t length ) {
	std::lock_guard<std::mutex> lock(_sendMutex);
#ifdef __linux__
	if ( ::send(_socket, message, length, 0) < 0 ) { throw std::runtime_error("Could not send message"); }
#elif _WIN32
	if(::send(_socket, message, length, 0) == SOCKET_ERROR) {
		throw std::runtime_error("Could not send message: " + WSAGetLastError());
	}
#endif
}

std::string Connection::_receive () {
	std::string message;

	while ( !message.ends_with(_end) ) {
		clearBuffer();

		_sizeOfPreviousMessage = recv(_socket, _buffer, 64, 0);

		if ( _sizeOfPreviousMessage < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != MSG_WAITALL ) {
			throw std::runtime_error("Could not receive message from server: " + std::string(strerror(errno)));
		}

		message += _buffer;
	}

	return message;
}

void Connection::send ( const std::string& message ) {
	auto messageToSend = message;

#ifdef HIKUP_DEBUG
	printf("SEND | %s\n", messageToSend.c_str());
#endif

	if ( _encrypted )
		_secretSeal(messageToSend);

	if (messageToSend.size() % 2 != 0)
		throw std::runtime_error("Invalid message to send");

	messageToSend += _end;

	//std::cout << "\nSEND | " << messageToSend << std::endl;

	_send(messageToSend.c_str(), messageToSend.size());
}

void Connection::sendData ( const std::string& message ) { send(_data + message); }

void Connection::sendInternal ( const std::string& message ) { send(_internal + message); }

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

	if ( _encrypted ) {
		_secretOpen(message);
		for ( auto& messageEnc: _messagesBuffer )
			_secretOpen(messageEnc);
	}

	if ( _messagesBuffer.empty() )
		_moreInBuffer = false;
	else
		_moreInBuffer = true;


#ifdef HIKUP_DEBUG
	std::cout << "RECEIVE | " << message << std::endl;
	std::cout << "RECEIVE BUFFER | "
			  << std::accumulate(_messagesBuffer.begin(), _messagesBuffer.end(), std::string(),
								 [](const std::string &a, const std::string &b) {
									 return a + b + " | ";
								 })
			  << std::endl;
#endif


	if ( message.contains(_internal"publicKey:") ) {
		std::string publicKey = message.substr(strlen(_internal"publicKey:"));

		if ( sodium_hex2bin(_remotePublicKey, crypto_box_PUBLICKEYBYTES, publicKey.c_str(), publicKey.size(), nullptr,
		                    nullptr, nullptr) < 0 ) { throw std::runtime_error("Could not decode public key"); }

		auto pk_hex = std::make_unique<char[]>(crypto_box_PUBLICKEYBYTES * 2 + 1);

		sodium_bin2hex(pk_hex.get(), crypto_box_PUBLICKEYBYTES * 2 + 1, _keyPair.publicKey, crypto_box_PUBLICKEYBYTES);

		//std::cout << "public key: " << pk_base64.get() << std::endl;

		send(_internal"publicKey:" + std::string(pk_hex.get(), crypto_box_PUBLICKEYBYTES * 2));

		_encrypted = true;
	}

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

	auto start = std::chrono::high_resolution_clock::now();
	auto message = _receive();
	auto end = std::chrono::high_resolution_clock::now();

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

	if ( _encrypted ) {
		_secretOpen(message);
		for ( auto& messageEnc: _messagesBuffer )
			_secretOpen(messageEnc);
	}

	if ( _messagesBuffer.empty() )
		_moreInBuffer = false;
	else
		_moreInBuffer = true;


#ifdef HIKUP_DEBUG
	std::cout << "RECEIVE | " << message << std::endl;
	std::cout << "RECEIVE BUFFER | "
			  << std::accumulate(_messagesBuffer.begin(), _messagesBuffer.end(), std::string(),
								 [](const std::string &a, const std::string &b) {
									 return a + b + " | ";
								 })
			  << std::endl;
#endif


	if ( message.contains(_internal"publicKey:") ) {
		std::string publicKey = message.substr(strlen(_internal"publicKey:"));

		if ( sodium_hex2bin(_remotePublicKey, crypto_box_PUBLICKEYBYTES, publicKey.c_str(), publicKey.size(), nullptr,
							nullptr, nullptr) < 0 ) { throw std::runtime_error("Could not decode public key"); }

		auto pk_hex = std::make_unique<char[]>(crypto_box_PUBLICKEYBYTES * 2 + 1);

		sodium_bin2hex(pk_hex.get(), crypto_box_PUBLICKEYBYTES * 2 + 1, _keyPair.publicKey, crypto_box_PUBLICKEYBYTES);

		//std::cout << "public key: " << pk_base64.get() << std::endl;

		send(_internal"publicKey:" + std::string(pk_hex.get(), crypto_box_PUBLICKEYBYTES * 2));

		_encrypted = true;
	}

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

void Connection::close () {
#ifdef __linux__
	shutdown(_socket, 0);
#elif _WIN32
	WSACleanup();
	shutdown(_socket, SD_SEND);
	closesocket(_socket);
#endif

	_active = false;
}

Connection::~Connection () {
	if ( _active )
		close();
}

void Connection::clearBuffer () { memset(_buffer, '\0', _sizeOfPreviousMessage); }

std::vector<std::string> Connection::dnsLookup ( const std::string& domain, int ipv ) {
	// credit to http://www.zedwood.com/article/cpp-dns-lookup-ipv4-and-ipv6

	std::vector<std::string> output;

	struct addrinfo hints, *res, *p;
	int status, ai_family;
	char ip_address[INET6_ADDRSTRLEN];

	ai_family = ipv == 6 ? AF_INET6 : AF_INET; //v4 vs v6?
	ai_family = ipv == 0 ? AF_UNSPEC : ai_family; // AF_UNSPEC (any), or chosen
	memset(&hints, 0, sizeof hints);
	hints.ai_family = ai_family;
	hints.ai_socktype = SOCK_STREAM;

	if ( ( status = getaddrinfo(domain.c_str(), NULL, &hints, &res) ) != 0 ) {
		//cerr << "getaddrinfo: "<< gai_strerror(status) << endl;
		return output;
	}

	//cout << "DNS Lookup: " << host_name << " ipv:" << ipv << endl;

	for ( p = res; p != NULL; p = p->ai_next ) {
		void* addr;
		if ( p->ai_family == AF_INET ) { // IPv4
			auto* ipv4 = (struct sockaddr_in*)p->ai_addr;
			addr = &( ipv4->sin_addr );
		}
		else { // IPv6
			auto* ipv6 = (struct sockaddr_in6*)p->ai_addr;
			addr = &( ipv6->sin6_addr );
		}

		// convert the IP to a string
		inet_ntop(p->ai_family, addr, ip_address, sizeof ip_address);
		output.emplace_back(ip_address);
	}

	freeaddrinfo(res); // free the linked list

	return output;
}

void Connection::_secretSeal ( std::string& message ) {
	auto cypherText = std::make_unique<unsigned char[]>(crypto_box_SEALBYTES + message.size());

	if ( crypto_box_seal(cypherText.get(), reinterpret_cast<const unsigned char*>(message.c_str()), message.size(),
	                     _remotePublicKey) < 0 )
		throw std::runtime_error("Could not encrypt message");

	auto messageHex = std::make_unique<unsigned char[]>(( crypto_box_SEALBYTES + message.size() ) * 2 + 1);

	sodium_bin2hex(reinterpret_cast<char*>(messageHex.get()), ( crypto_box_SEALBYTES + message.size() ) * 2 + 1,
	               cypherText.get(), crypto_box_SEALBYTES + message.size());

	message = std::string(reinterpret_cast<char*>(messageHex.get()), ( crypto_box_SEALBYTES + message.size() ) * 2);
}

void Connection::_secretOpen ( std::string& message ) {

	if (message.length() % 2 != 0)
		throw std::runtime_error("Invalid message to decrypt: " + message);

	auto cypherTextBin = std::make_unique<unsigned char[]>(message.size() / 2);

	if ( sodium_hex2bin(cypherTextBin.get(), message.size() / 2, reinterpret_cast<const char*>(message.c_str()),
	                    message.size(), nullptr, nullptr, nullptr) < 0 )
		throw std::runtime_error("Could not decode message: " + message);

	auto decrypted = std::make_unique<unsigned char[]>(message.size() / 2 - crypto_box_SEALBYTES);

	if ( crypto_box_seal_open(decrypted.get(), cypherTextBin.get(), message.size() / 2, _keyPair.publicKey,
	                          _keyPair.secretKey) < 0 )
		throw std::runtime_error("Could not decrypt message");

	message = std::string(reinterpret_cast<char*>(decrypted.get()), message.size() / 2 - crypto_box_SEALBYTES);
}
