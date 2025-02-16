#pragma once

#include <iostream>
#include <sodium.h>

#include "ClientInfo.h"


#define _end "::--///--$$$"
#define _internal "INTERNAL::"
#define _data "DATA::"
#define _bufferSize 256 * 1024


class ConnectionServer {
public:
	ConnectionServer ( ClientInfo clientInfo );

	~ConnectionServer ();

	void init ();

	void send ( const std::string& message ) const;

	void sendInternal ( const std::string& message ) const;

	void sendData ( const std::string& message ) const;

	std::string receive ();

	std::string receiveInternal ();

	void rawSendInit ( int64_t expectedSize );

	void rawSend ( std::string& message, int64_t chunkSize );

	void rawSendClose ();

	std::string receiveData ();

	std::string receiveRaw ( bool chunked );

private:
	char _buffer[_bufferSize + 1] = {0};
	std::vector<std::string> _messagesBuffer;
	std::string _rawModeBuffer;
	ClientInfo _clientInfo;

	struct KeyPair {
		unsigned char publicKey[crypto_box_PUBLICKEYBYTES];
		unsigned char secretKey[crypto_box_SECRETKEYBYTES];
	};

	int _sizeOfPreviousMessage = 0;
	std::string _message;

	KeyPair _keyPair;
	unsigned char _remotePublicKey[crypto_box_PUBLICKEYBYTES];
	bool _active = true;
	bool _encrypted = false;
	bool _moreInBuffer = false;
	bool _rawSendOut = false;
	bool _rawSendIn = false;

	void initEncryption ();

	void clearBuffer ();

	void _send ( const std::string& messageToSend ) const;

	[[nodiscard]] std::string _receive ();

	std::string _receiveInternal ();

	void _sendInternal ( const std::string& message ) const;

	void _rawReceive ( int64_t size = 1024 * 256 );

	std::string _receiveSize ( int64_t size );

	void secretOpen ( std::string& message ) const;

	void secretSeal ( std::string& message ) const;
};
