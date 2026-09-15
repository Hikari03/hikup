#include "ConnectionServer.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <utility>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>

ConnectionServer::ConnectionServer ( ClientInfo clientInfo ) : ConnectionServer(std::move(clientInfo), 4 * 1024 * 1024) {}

ConnectionServer::ConnectionServer ( ClientInfo clientInfo, const unsigned long bufferSize )
	: _buffer(std::make_unique<char[]>(bufferSize)), _clientInfo(std::move(clientInfo)), _bufferSize(bufferSize) {}

ConnectionServer::~ConnectionServer () {
	_active = false;

	if ( SSL_stream_conclude(_clientInfo.getConn(), 0) != 1 ) {
		std::cerr << "Unable to conclude stream\n";
		SSL_free(_clientInfo.getConn());
		return;
	}

	while ( SSL_shutdown(_clientInfo.getConn()) != 1 ) {
		std::cerr << "Re-attempting SSL shutdown\n";
	}

	SSL_free(_clientInfo.getConn());
}

void ConnectionServer::init () {
	timeval timeout{};
	timeout.tv_sec = 20; // Timeout in seconds
	timeout.tv_usec = 0; // Timeout in microseconds

	if ( setsockopt(SSL_get_fd(_clientInfo.getConn()), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout )) < 0 ) {
		throw std::runtime_error("setsockopt failed");
	}

	_active = true;
}

void ConnectionServer::clearBuffer () const { memset(_buffer.get(), '\0', _bufferSize); }

std::string ConnectionServer::receive () {

	_message.clear();

	if ( _moreInBuffer ) {
		_message = _messagesBuffer[0];
		_messagesBuffer.erase(_messagesBuffer.begin());
		if ( _messagesBuffer.empty() )
			_moreInBuffer = false;
		return _message;
	}

	while ( !_message.ends_with(_end) ) {
		//std::cout << "RECEIVE BUFFER BEFORE CLEAR|  " << _clientInfo.getSocket() << ": " << _buffer << std::endl;
		clearBuffer();

		if ( const auto ret = SSL_read_ex(_clientInfo.getConn(), _buffer.get(), _bufferSize, &_sizeOfPreviousMessage); ret <= 0 ) {
			int err = SSL_get_error(_clientInfo.getConn(), ret);

			switch ( err ) {
				case SSL_ERROR_SYSCALL:
					if ( errno != EAGAIN && errno != EWOULDBLOCK )
						throw std::runtime_error("client disconnected or could not receive message");
					if ( errno == EAGAIN || errno == EWOULDBLOCK )
						throw std::runtime_error("timeout");
					break;

				case SSL_ERROR_ZERO_RETURN:
				case SSL_ERROR_SSL:
					throw std::runtime_error("client disconnected");

				default:
					ERR_print_errors_fp(stderr);
					throw std::logic_error("unexpected error: " + std::to_string(err));
			}
		}

		_message += std::string(_buffer.get(), _sizeOfPreviousMessage);

		//std::cout << "RECEIVE |  " << _clientInfo.getSocket() << (_clientInfo.name.empty() ? "" : "/" + _clientInfo.name ) << ": " << _message << std::endl;
	}

	//std::cout << "RECEIVE |  " << _clientInfo.getSocket() << ": " << _message << std::endl;

	// cut off the _end

	std::vector<std::string> messages;
	std::string tmpMessage;
	size_t pos = 0;
	bool first = true;

	while ( ( pos = _message.find(_end) ) != std::string::npos ) {
		if ( first ) {
			tmpMessage = _message.substr(0, pos);
			first = false;
		}
		else
			messages.push_back(_message.substr(0, pos));
		_message.erase(0, pos + strlen(_end));
	}

	_message = tmpMessage;
	_messagesBuffer = messages;

	if ( _messagesBuffer.empty() )
		_moreInBuffer = false;
	else
		_moreInBuffer = true;

	//std::cout << "RECEIVE2 |  " << _clientInfo.getSocket() << ": " << _message << std::endl;

	return _message;
}

std::string ConnectionServer::receiveInternal () {
	const auto message = receive();

	if ( !message.contains(_internal) )
		throw std::runtime_error("Invalid message received (internal): " + message);

	return message.substr(strlen(_internal));
}

std::string ConnectionServer::receiveData () {
	const auto message = receive();

	if ( !message.contains(_data) )
		throw std::runtime_error("Invalid message received (data):" + message);

	return message.substr(strlen(_data));
}

void ConnectionServer::resizeBuffer ( const unsigned long newSize )  {
	_buffer = std::make_unique<char[]>(newSize);
	_bufferSize = newSize;
}

bool ConnectionServer::isActive () const { return _active; }

void ConnectionServer::send ( const std::string& message ) const {
	auto messageToSend = message;

	//std::cout << "SEND1 |  " << _clientInfo.getSocket() << (_clientInfo.name.empty() ? "" : "/" + _clientInfo.name ) << ": " << messageToSend << std::endl;

	messageToSend += _end;

	//std::cout << "SEND2 |  " << _clientInfo.getSocket() << (_clientInfo.name.empty() ? "" : "/" + _clientInfo.name ) << ": " << messageToSend << std::endl;
	if ( !_active )
		return;
	try {
		size_t nWritten = 0;
		if ( SSL_write_ex(_clientInfo.getConn(), messageToSend.data(), messageToSend.length(), &nWritten) <= 0 || nWritten != messageToSend.length() ) {
			throw std::runtime_error("Could not send message to client");
		}
	}
	catch ( std::exception& ) { throw std::runtime_error("Could not send message to client"); }
}

void ConnectionServer::sendData ( const std::string& message ) const { send(_data + message); }

void ConnectionServer::sendInternal ( const std::string& message ) const { send(_internal + message); }

