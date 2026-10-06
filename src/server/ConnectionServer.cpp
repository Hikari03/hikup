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
	if ( !_active )
		return;
	_active = false;

	if ( SSL_stream_conclude(_clientInfo.getConn(), 0) != 1 ) {
		std::cerr << "Unable to conclude stream\n";
		SSL_free(_clientInfo.getConn());
		return;
	}

	while ( SSL_shutdown(_clientInfo.getConn()) != 1 ) {
		// std::cerr << "Re-attempting SSL shutdown\n";
	}

	SSL_free(_clientInfo.getConn());
}

void ConnectionServer::init () {
	/*timeval timeout{};
	timeout.tv_sec = 20; // Timeout in seconds
	timeout.tv_usec = 0; // Timeout in microseconds

	if ( setsockopt(SSL_get_fd(_clientInfo.getConn()), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout )) < 0 ) {
		throw std::runtime_error("setsockopt failed");
	}*/

	_active = true;
}

void ConnectionServer::clearBuffer () const { memset(_buffer.get(), '\0', _bufferSize); }

std::string ConnectionServer::receive () {
	_message.clear();
	_message.reserve(_bufferSize);

	if ( _moreInBuffer ) {
		_message = _messagesBuffer[0];
		_messagesBuffer.erase(_messagesBuffer.begin());
		if ( _messagesBuffer.empty() )
			_moreInBuffer = false;
		return _message;
	}

	while ( true ) {
		if ( const auto ret = SSL_read_ex(_clientInfo.getConn(), _buffer.get(), _bufferSize, &_sizeOfPreviousMessage); ret <= 0 ) {
			std::string retStr;
			switch ( const int err = SSL_get_error(_clientInfo.getConn(), ret) ) {
				case SSL_ERROR_WANT_READ:
				case SSL_ERROR_WANT_WRITE:
					continue;

				case SSL_ERROR_SYSCALL:
					if ( errno != EAGAIN && errno != EWOULDBLOCK )
						throw std::runtime_error("client disconnected or could not receive message");
					if ( errno == EAGAIN || errno == EWOULDBLOCK )
						throw std::runtime_error("timeout");
					break;

				case SSL_ERROR_ZERO_RETURN:
					retStr = "The TLS/SSL peer has closed the connection for writing by sending the close_notify alert";
					_active = false;
					break;
				case SSL_ERROR_WANT_X509_LOOKUP:
					retStr = "SSL_ERROR_WANT_X509_LOOKUP";
					break;
				case SSL_ERROR_SSL:
					retStr = "SSL_ERROR_SSL";
					ERR_print_errors_fp(stderr);
					_active = false;
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

				default:
					ERR_print_errors_fp(stderr);
					throw std::logic_error("unexpected error: " + std::to_string(err));
			}
			throw std::runtime_error(retStr);
		}

		_message.append(_buffer.get(), _sizeOfPreviousMessage);
		if (_message.size() >= strlen(_end) && memcmp(_message.data() + _message.size() - strlen(_end), _end, strlen(_end)) == 0) {
			break;
		}
	}

	//std::cout << "RECEIVE |  " << _clientInfo.getSocket() << ": " << _message << std::endl;

	// cut off the _end

	std::vector<std::string> messages;
	std::string tmpMessage;
	size_t sPos = 0, ePos = 0;
	bool first = true;
	const size_t endLen = strlen(_end);

	while ( ( ePos = _message.find(_end, sPos) ) != std::string::npos ) {
		if ( first ) {
			tmpMessage = _message.substr(sPos, ePos - sPos);
			first = false;
		}
		else
			messages.push_back(_message.substr(sPos, ePos - sPos));
		sPos = ePos + endLen;
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

void ConnectionServer::receiveStream ( const std::function<void(const char*, size_t)>& callback ) {
    if ( _moreInBuffer ) {
        for ( const auto& msg : _messagesBuffer ) {
            callback(msg.data(), msg.size());
        }
        _messagesBuffer.clear();
        _moreInBuffer = false;
    }

    while ( true ) {
        size_t nRead = 0;
        if ( const auto ret = SSL_read_ex(_clientInfo.getConn(), _buffer.get(), _bufferSize, &nRead); ret <= 0 ) {
            std::string retStr;
            switch ( const int err = SSL_get_error(_clientInfo.getConn(), ret) ) {
                case SSL_ERROR_WANT_READ:
                case SSL_ERROR_WANT_WRITE:
                    continue;

                case SSL_ERROR_SYSCALL:
                    if ( errno != EAGAIN && errno != EWOULDBLOCK )
                        throw std::runtime_error("client disconnected or could not receive message");
                    if ( errno == EAGAIN || errno == EWOULDBLOCK )
                        throw std::runtime_error("timeout");
                    break;

                case SSL_ERROR_ZERO_RETURN:
                    retStr = "The TLS/SSL peer has closed the connection for writing by sending the close_notify alert";
                    _active = false;
                    break;
                case SSL_ERROR_WANT_X509_LOOKUP:
                    retStr = "SSL_ERROR_WANT_X509_LOOKUP";
                    break;
                case SSL_ERROR_SSL:
                    retStr = "SSL_ERROR_SSL";
                    ERR_print_errors_fp(stderr);
                    _active = false;
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

                default:
                    ERR_print_errors_fp(stderr);
                    throw std::logic_error("unexpected error: " + std::to_string(err));
            }
            throw std::runtime_error(retStr);
        }

        const size_t endLen = strlen(_end);
        if (nRead >= endLen && memcmp(_buffer.get() + nRead - endLen, _end, endLen) == 0) {
            if (nRead > endLen) {
                callback(_buffer.get(), nRead - endLen);
            }
            break;
        }
        callback(_buffer.get(), nRead);
    }
}

void ConnectionServer::receiveExact ( size_t length, const std::function<void(const char*, size_t)>& callback ) {
    size_t totalReceived = 0;

    if ( _moreInBuffer ) {
        for ( auto it = _messagesBuffer.begin(); it != _messagesBuffer.end(); ) {
            size_t remaining = length - totalReceived;
            if ( it->size() <= remaining ) {
                callback(it->data(), it->size());
                totalReceived += it->size();
                it = _messagesBuffer.erase(it);
            } else {
                callback(it->data(), remaining);
                totalReceived += remaining;
                *it = it->substr(remaining);
                break;
            }
        }
        if ( _messagesBuffer.empty() )
            _moreInBuffer = false;
    }

    while ( totalReceived < length ) {
        size_t nRead = 0;
        size_t toRead = std::min(static_cast<size_t>(_bufferSize), length - totalReceived);
        if ( const auto ret = SSL_read_ex(_clientInfo.getConn(), _buffer.get(), toRead, &nRead); ret <= 0 ) {
            std::string retStr;
            switch ( const int err = SSL_get_error(_clientInfo.getConn(), ret) ) {
                case SSL_ERROR_WANT_READ:
                case SSL_ERROR_WANT_WRITE:
                    continue;

                case SSL_ERROR_SYSCALL:
                    if ( errno != EAGAIN && errno != EWOULDBLOCK )
                        throw std::runtime_error("client disconnected or could not receive message");
                    if ( errno == EAGAIN || errno == EWOULDBLOCK )
                        throw std::runtime_error("timeout");
                    break;

                case SSL_ERROR_ZERO_RETURN:
                    retStr = "The TLS/SSL peer has closed the connection for writing by sending the close_notify alert";
                    _active = false;
                    break;
                case SSL_ERROR_WANT_X509_LOOKUP:
                    retStr = "SSL_ERROR_WANT_X509_LOOKUP";
                    break;
                case SSL_ERROR_SSL:
                    retStr = "SSL_ERROR_SSL";
                    ERR_print_errors_fp(stderr);
                    _active = false;
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

                default:
                    ERR_print_errors_fp(stderr);
                    throw std::logic_error("unexpected error: " + std::to_string(err));
            }
            throw std::runtime_error(retStr);
        }
        callback(_buffer.get(), nRead);
        totalReceived += nRead;
    }
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
	const unsigned long maxSize = 64 * 1024 * 1024;
	const unsigned long finalSize = std::min(newSize, maxSize);
	_buffer = std::make_unique<char[]>(finalSize);
	_bufferSize = finalSize;
}

bool ConnectionServer::isActive () const { return _active; }

void ConnectionServer::send ( const std::string& message ) const {
    send(message.data(), message.size());
}

void ConnectionServer::send ( const char* data, size_t length ) const {
	if ( !_active )
		return;
	try {
		size_t nWritten = 0;
		if ( SSL_write_ex(_clientInfo.getConn(), data, length, &nWritten) <= 0 || nWritten != length ) {
			throw std::runtime_error("Could not send data to client");
		}
		if ( SSL_write_ex(_clientInfo.getConn(), _end, strlen(_end), &nWritten) <= 0 || nWritten != strlen(_end) ) {
			throw std::runtime_error("Could not send end marker to client");
		}
	}
	catch ( std::exception& ) { throw std::runtime_error("Could not send message to client"); }
}

void ConnectionServer::sendRaw ( const char* data, size_t length ) const {
	if ( !_active )
		return;
	try {
		size_t nWritten = 0;
		if ( SSL_write_ex(_clientInfo.getConn(), data, length, &nWritten) <= 0 || nWritten != length ) {
			throw std::runtime_error("Could not send data to client");
		}
	}
	catch ( std::exception& ) { throw std::runtime_error("Could not send message to client"); }
}

void ConnectionServer::sendData ( const std::string& message ) const { sendData(message.data(), message.size()); }
void ConnectionServer::sendData ( const char* data, size_t length ) const {
    size_t nWritten = 0;
    SSL_write_ex(_clientInfo.getConn(), _data, strlen(_data), &nWritten);
    send(data, length);
}

void ConnectionServer::sendInternal ( const std::string& message ) const {
    size_t nWritten = 0;
    SSL_write_ex(_clientInfo.getConn(), _internal, strlen(_internal), &nWritten);
    send(message.data(), message.size());
}

