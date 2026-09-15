#pragma once

#include <iostream>
#include <memory>
#include <vector>

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

	std::unique_ptr<char[]> _buffer;
	ClientInfo _clientInfo;
	std::vector<std::string> _messagesBuffer;


	size_t _sizeOfPreviousMessage = 0;
	unsigned long _bufferSize = 4*1024*1024;
	std::string _message;

	bool _active = true;
	bool _moreInBuffer = false;


	void clearBuffer () const;

};
