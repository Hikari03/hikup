#pragma once

#include <cstring>
#include <string>
#include <stdexcept>
#include <cerrno>
#include <vector>
#include <mutex>
#include <memory>
#include <thread>
#include <functional>
#include <filesystem>
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
	Connection& send ( const char* data, size_t length );
	Connection& sendRaw ( const char* data, size_t length );

	Connection& sendData ( const std::string& message );
	Connection& sendData ( const char* data, size_t length );

	Connection& sendInternal ( const std::string& message );

	std::string receive ();
	void receiveStream ( const std::function<void(const char*, size_t)>& callback );
	void receiveExact ( size_t length, const std::function<void(const char*, size_t)>& callback );

	std::tuple<std::string, std::chrono::duration<double>> receiveWTime();

	std::string receiveInternal ();

	std::string receiveData ();

	bool isConnected () const;

	// the server's certificate chain in PEM form (leaf + issuers), empty
	// when none was presented (after the handshake — used by fetch-cert)
	std::string peerCertificatePem () const;

	// true when the loaded trust anchors came from the working directory
	// (the last-resort store) — clients warn about it when verifying
	bool trustStoreFromWorkingDir () const;

	// the X509 verify result of the last handshake attempt (X509_V_OK when
	// it succeeded or never ran) — lets callers tell a missing trust anchor
	// apart from other verification failures
	long lastVerifyResult () const;

	// directory an automatically pinned trust anchor should be written to
	// so that the next Connection constructed actually loads it (the store
	// that wins the candidate chain); empty when that store is a single
	// certificate file or not writable by this user — a pin anywhere else
	// would never be read, so automatic pinning cannot work then
	static std::filesystem::path pinnableTrustStoreDir ();

	void resizeBuffer ( unsigned long newSize );

	void close ();

private:

	std::unique_ptr<char[]> _buffer;
	std::vector<std::string> _messagesBuffer;
	unsigned long _bufferSize = 4*1024*1024;
	std::mutex _sendMutex;
	SSL_CTX *_ctx = nullptr;
	SSL *_ssl = nullptr;
	BIO *_bio = nullptr;

	size_t _sizeOfPreviousMessage = 0;
	bool _active = true;
	bool _moreInBuffer = false;
	bool _trustStoreFromWorkingDir = false;
	long _lastVerifyResult = 0;   // X509_V_OK

	void clearBuffer () const;

	void _send ( const char* message, size_t length );

	std::string _receive ();

	void _secretOpen ( std::string& message ) const;

	void _secretSeal ( std::string& message ) const;
};
