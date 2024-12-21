#pragma once

#include <cstring>
#include <string>
#include <stdexcept>
#include <cerrno>
#include <vector>
#include <mutex>
#include <memory>
#include <thread>
#include <sodium.h>

#ifdef __linux__
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <resolv.h>
#elif _WIN32
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define _end "::--///-$$$"
#define _internal "INTERNAL::"
#define _data "DATA::"

#ifdef HIKUP_DEBUG
#define DEBUG 1
#include <numeric>
#include <iostream>
#else
#define DEBUG 0
#endif


/**
 * @brief Handles the connection to the server
 */
class Connection {
public:
	Connection ();

	~Connection ();

	void connectToServer ( std::string ip, int port );

	void send ( const std::string& message );

	void sendData ( const std::string& message );

	void sendInternal ( const std::string& message );

	std::string receive ();

	std::string receiveInternal ();

	std::string receiveData ();

	void close ();

private:
	struct KeyPair {
		unsigned char publicKey[crypto_box_PUBLICKEYBYTES];
		unsigned char secretKey[crypto_box_SECRETKEYBYTES];
	};

	char _buffer[4096] = {0};
	std::vector<std::string> _messagesBuffer;
	KeyPair _keyPair;
	unsigned char _remotePublicKey[crypto_box_PUBLICKEYBYTES];
	std::mutex _sendMutex;

#ifdef __linux__
	int _socket;
	sockaddr_in _server;
#elif _WIN32
	WSADATA _wsaData;
	SOCKET _socket = INVALID_SOCKET;
	struct addrinfo *_result = NULL,
                *_ptr = NULL,
                _hints;
#endif

	ssize_t _sizeOfPreviousMessage = 0;
	bool _active = true;
	bool _encrypted = false;
	bool _moreInBuffer = false;

	void clearBuffer ();

	[[nodiscard]] static std::vector<std::string> dnsLookup ( const std::string& domain, int ipv = 4 );

	void _send ( const char* message, size_t length );

	void _secretOpen ( std::string& message );

	void _secretSeal ( std::string& message );
};
