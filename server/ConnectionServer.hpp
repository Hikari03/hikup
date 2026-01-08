#pragma once

#include <iostream>
#include <sodium.h>

#include "ClientInfo.hpp"


#define _end "::--///--$$$"
#define _internal "INTERNAL::"
#define _data "DATA::"


class ConnectionServer {
public:
	explicit ConnectionServer ( ClientInfo clientInfo );

	~ConnectionServer ();

	void init ();

	void send ( const std::string& message ) const;

	void sendInternal ( const std::string& message ) const;

	void sendData ( const std::string& message ) const;

	std::string receive ();

	std::string receiveInternal ();

	std::string receiveData ();

private:
	char _buffer[256*1024+1] = {0};
	std::vector<std::string> _messagesBuffer;
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

	void initEncryption ();

	void clearBuffer ();

	void secretOpen ( std::string& message ) const;

	void secretSeal ( std::string& message ) const;
};
