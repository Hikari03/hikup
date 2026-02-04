#pragma once

#include <iostream>
#include <memory>
#include <sodium.h>

#include "ClientInfo.hpp"


#define _end "::--///--$$$"
#define _internal "INTERNAL::"
#define _data "DATA::"




class ConnectionServer {
public:

	explicit ConnectionServer ( ClientInfo clientInfo );

	ConnectionServer ( ClientInfo clientInfo, unsigned long bufferSize );

	~ConnectionServer ();

	void init ();

	void send ( const std::string& message ) const;

	void sendInternal ( const std::string& message ) const;

	void sendData ( const std::string& message ) const;

	std::string receive ();

	std::string receiveInternal ();

	std::string receiveData ();

	void resizeBuffer ( unsigned long newSize );

	[[nodiscard]] bool isActive () const;

private:
	struct KeyPair {
		unsigned char publicKey[crypto_box_PUBLICKEYBYTES];
		unsigned char secretKey[crypto_box_SECRETKEYBYTES];
	};

	std::unique_ptr<char[]> _buffer;
	KeyPair _keyPair;
	ClientInfo _clientInfo;
	std::vector<std::string> _messagesBuffer;


	long int _sizeOfPreviousMessage = 0;
	unsigned long _bufferSize = 4*1024*1024;
	std::string _message;

	unsigned char _remotePublicKey[crypto_box_PUBLICKEYBYTES];
	bool _active = true;
	bool _encrypted = false;
	bool _moreInBuffer = false;

	void initEncryption ();

	void clearBuffer () const;

	void secretOpen ( std::string& message ) const;

	void secretSeal ( std::string& message ) const;

};
