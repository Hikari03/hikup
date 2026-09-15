#pragma once

#include <cstring>
#include <string>
#include <stdexcept>
#include <cerrno>
#include <vector>
#include <mutex>
#include <memory>
#include <thread>
#include <openssl/bio.h>
#include <openssl/types.h>

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

#define _end "::--///--$$$"
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

	explicit Connection ( unsigned long bufferSize = 4*1024*1024 );

	~Connection ();

	void connectToServer ( std::string ip, int port, bool forceServerCertVerify = true );

	Connection& send ( const std::string& message );

	Connection& sendData ( const std::string& message );

	Connection& sendInternal ( const std::string& message );

	std::string receive ();

	std::tuple<std::string, std::chrono::duration<double>> receiveWTime();

	std::string receiveInternal ();

	std::string receiveData ();

	bool isConnected () const;

	void resizeBuffer ( unsigned long newSize );

	void close ();

private:

	std::unique_ptr<char[]> _buffer;
	std::vector<std::string> _messagesBuffer;
	unsigned long _bufferSize = 4*1024*1024;
	std::mutex _sendMutex;
	SSL_CTX *_ctx;
	SSL *_ssl;
	BIO *_bio;

	size_t _sizeOfPreviousMessage = 0;
	bool _active = true;
	bool _moreInBuffer = false;

	void clearBuffer () const;

	void _send ( const char* message, size_t length );

	std::string _receive ();

	void _secretOpen ( std::string& message ) const;

	void _secretSeal ( std::string& message ) const;
};
