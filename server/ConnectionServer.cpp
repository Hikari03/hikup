#include "ConnectionServer.h"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <sys/socket.h>

ConnectionServer::ConnectionServer ( ClientInfo clientInfo )
	: _clientInfo(std::move(clientInfo)) {}

ConnectionServer::~ConnectionServer () {
	_active = false;
	shutdown(_clientInfo.socket_, SHUT_RDWR);
	close(_clientInfo.socket_);
}

void ConnectionServer::init () {
	timeval timeout{};
	timeout.tv_sec = 20; // Timeout in seconds
	timeout.tv_usec = 0; // Timeout in microseconds

	if ( setsockopt(_clientInfo.socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout )) < 0 ) {
		throw std::runtime_error("setsockopt failed");
	}

	initEncryption();

	_active = true;
}

void ConnectionServer::initEncryption () {
	std::cout << "initializing encryption with client " << _clientInfo.socket_ << std::endl;
	if ( sodium_init() < 0 )
		throw std::runtime_error("Could not initialize sodium");

	if ( crypto_box_keypair(_keyPair.publicKey, _keyPair.secretKey) < 0 )
		throw std::runtime_error("Could not generate keypair");

	const auto pk_hex = std::make_unique<char[]>(crypto_box_PUBLICKEYBYTES * 2 + 1);

	sodium_bin2hex(pk_hex.get(), crypto_box_PUBLICKEYBYTES * 2 + 1, _keyPair.publicKey, crypto_box_PUBLICKEYBYTES);

	std::cout << "public key: " << pk_hex.get() << std::endl;

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	send(_internal"publicKey:" + std::string(pk_hex.get(), crypto_box_PUBLICKEYBYTES * 2));

	receive();

	if ( !_message.contains(_internal"publicKey:") )
		throw std::runtime_error("Could not receive pubKey");

	const auto pubKey_hex = _message.substr(strlen(_internal"publicKey:"));

	if ( sodium_hex2bin(_remotePublicKey, crypto_box_PUBLICKEYBYTES, pubKey_hex.c_str(), pubKey_hex.size(), nullptr,
	                    nullptr, nullptr) < 0 ) { throw std::runtime_error("Could not decode public key"); }

	//std::cout << "pubKey: " << _remotePublicKey << std::endl;

	for ( auto& string: _messagesBuffer )
		secretOpen(string);

	_encrypted = true;
}

void ConnectionServer::clearBuffer () { memset(_buffer, '\0', _sizeOfPreviousMessage); }

void ConnectionServer::_send ( const std::string& messageToSend ) const {
	if ( ::send(_clientInfo.socket_, messageToSend.c_str(), messageToSend.length(), 0) < 0 ) {
		throw std::runtime_error("Could not send message to client");
	}
}

std::string ConnectionServer::_receive () {
	std::string message;

	while ( !message.ends_with(_end) ) {
		_rawReceive();
		message += _buffer;
	}

	return message;
}

std::string ConnectionServer::_receiveInternal () {
	const auto message = receive();

	if ( !message.contains(_internal) ) {
		_messagesBuffer.emplace(_messagesBuffer.begin(), message);
		return "";
	}

	return message.substr(strlen(_internal));
}

std::string ConnectionServer::receive () {
	_message.clear();

	if ( _moreInBuffer ) {
		_message = _messagesBuffer[0];
		_messagesBuffer.erase(_messagesBuffer.begin());
		if ( _messagesBuffer.empty() )
			_moreInBuffer = false;
		return _message;
	}

	_message = _receive();

#ifdef HIKUP_DEBUG
	std::cout << "RECEIVE |  " << _clientInfo.socket_ << ": " << _message << std::endl;
#endif

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

	if ( _encrypted ) {
		secretOpen(_message);
		for ( auto& messageEnc: _messagesBuffer )
			secretOpen(messageEnc);
	}

	if ( _messagesBuffer.empty() )
		_moreInBuffer = false;
	else
		_moreInBuffer = true;

	//std::cout << "RECEIVE2 |  " << _clientInfo.socket_ << ": " << _message << std::endl;

	return _message;
}

std::string ConnectionServer::receiveInternal () {
	if ( _rawSendIn )
		throw std::runtime_error("Cannot receive formatted data in raw mode");

	return _receiveInternal();
}

void ConnectionServer::rawSendInit ( const int64_t expectedSize = 1024 ) {
	sendInternal("RAWSEND_INIT");
	if ( receiveInternal() != "RAWSEND_CONFIRM" )
		throw std::runtime_error("Could not initialize raw send");

	_rawModeBuffer.clear();
	_rawModeBuffer.reserve(expectedSize);

	_rawSendOut = true;
}

void ConnectionServer::rawSend ( std::string& message, const int64_t chunkSize = 1024 ) {
	if ( !_rawSendOut )
		throw std::runtime_error("Raw send not initialized");

	auto internalString = message;

	secretSeal(internalString);

	_rawModeBuffer += internalString;

	if ( _rawModeBuffer.size() >= static_cast<std::string::size_type>(chunkSize) ) {
		_sendInternal(std::to_string(_rawModeBuffer.size()));
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		_send(_rawModeBuffer);
		_rawModeBuffer.clear();
	}
}

void ConnectionServer::rawSendClose () {
	if ( !_rawSendOut )
		throw std::runtime_error("Raw send not initialized");

	if ( !_rawModeBuffer.empty() ) {
		_sendInternal(std::to_string(_rawModeBuffer.size()));
		_send(_rawModeBuffer);
	}

	_sendInternal("RAWSEND_CLOSE");
	if ( _receiveInternal() != "RAWSEND_CONFIRM" )
		throw std::runtime_error("Could not close raw send");

	_rawSendOut = false;
}

std::string ConnectionServer::receiveData () {
	const auto message = receive();

	if ( !message.contains(_data) )
		throw std::runtime_error("Invalid message received (data):" + message);

	return message.substr(strlen(_data));
}

std::string ConnectionServer::receiveRaw ( const bool chunked ) {
	if ( !_rawSendIn ) {
		if ( _receiveInternal() != "RAWSEND_INIT" )
			throw std::runtime_error("Could not initialize raw receive");

		_sendInternal("RAWSEND_CONFIRM");

		_rawSendIn = true;
	}

	std::string message, internal;

	if ( chunked ) {
		internal = _receiveInternal();

		if ( internal == "RAWSEND_CLOSE" ) {
			std::cout << "RAWSEND_CLOSE" << std::endl;
			_sendInternal("RAWSEND_CONFIRM");
			_rawSendIn = false;
			return "";
		}

		//std::cout << "expected size: " << internal << std::endl;

		message = receive();
	}
	else {
		while ( internal != "RAWSEND_CLOSE" ) {
			message += receive();
			internal = _receiveInternal();
		}

		_sendInternal("RAWSEND_CONFIRM");
		_rawSendIn = false;
	}

	return message;
}

void ConnectionServer::send ( const std::string& message ) const {
	auto messageToSend = message;

#ifdef HIKUP_DEBUG
	std::cout << "SEND1 |  " << _clientInfo.socket_  << ": " << messageToSend << std::endl;
#endif

	if ( _encrypted ) { secretSeal(messageToSend); }
	messageToSend += _end;

#ifdef HIKUP_DEBUG
	std::cout << "SEND2 |  " << _clientInfo.socket_  << ": " << messageToSend << std::endl;
#endif

	if ( !_active )
		return;
	try { _send(messageToSend); }
	catch ( std::exception& e ) { throw std::runtime_error("Could not send message to client"); }
}

void ConnectionServer::sendData ( const std::string& message ) const { send(_data + message); }

void ConnectionServer::sendInternal ( const std::string& message ) const {
	if ( _rawSendOut )
		throw std::runtime_error("Cannot send data in raw mode");
	_sendInternal(message);
}

void ConnectionServer::_sendInternal ( const std::string& message ) const { send(_internal + message); }

void ConnectionServer::_rawReceive ( const int64_t size ) {
	if ( size > _bufferSize )
		throw std::runtime_error("receive size too big");

	clearBuffer();

	_sizeOfPreviousMessage = recv(_clientInfo.socket_, _buffer, size, 0);

	if ( _sizeOfPreviousMessage < 0 ) {
		if ( errno != EAGAIN && errno != EWOULDBLOCK )
			throw std::runtime_error(
				"client disconnected or could not receive message: " + std::string(strerror(errno)));

		if ( errno == EAGAIN || errno == EWOULDBLOCK )
			throw std::runtime_error("timeout");
	}
}

std::string ConnectionServer::_receiveSize ( const int64_t size ) {
	if ( size <= 0 )
		return "";
	std::string message;

	while ( message.size() < static_cast<std::string::size_type>(size) ) {
		_rawReceive(( size - message.size() ) > _bufferSize ? _bufferSize : size - message.size());
		message += _buffer;
	}

	//std::cout << "received size: " << message.size() << std::endl;

	secretOpen(message);

	return message;
}

void ConnectionServer::secretSeal ( std::string& message ) const {
	const auto cypherText = std::make_unique<unsigned char[]>(crypto_box_SEALBYTES + message.size());

	if ( crypto_box_seal(cypherText.get(), reinterpret_cast<const unsigned char*>(message.c_str()), message.size(),
	                     _remotePublicKey) < 0 )
		throw std::runtime_error("Could not encrypt message");

	const auto messageHex = std::make_unique<unsigned char[]>(( crypto_box_SEALBYTES + message.size() ) * 2 + 1);

	sodium_bin2hex(reinterpret_cast<char*>(messageHex.get()), ( crypto_box_SEALBYTES + message.size() ) * 2 + 1,
	               cypherText.get(), crypto_box_SEALBYTES + message.size());

	message = std::string(reinterpret_cast<char*>(messageHex.get()), ( crypto_box_SEALBYTES + message.size() ) * 2);
#ifdef HIKUP_DEBUG
	std::cout << "SEAL (enc)| " << message << std::endl;
#endif
}

void ConnectionServer::secretOpen ( std::string& message ) const {
	const auto cypherTextBin = std::make_unique<unsigned char[]>(message.size() / 2);

	if ( sodium_hex2bin(cypherTextBin.get(), message.size() / 2, message.c_str(), message.size(), nullptr, nullptr,
	                    nullptr) < 0 )
		throw std::runtime_error("Could not decode message");

	const auto decrypted = std::make_unique<unsigned char[]>(message.size() / 2 - crypto_box_SEALBYTES);

	if ( crypto_box_seal_open(decrypted.get(), cypherTextBin.get(), message.size() / 2, _keyPair.publicKey,
	                          _keyPair.secretKey) < 0 )
		throw std::runtime_error("Could not decrypt message");

	message = std::string(reinterpret_cast<char*>(decrypted.get()), message.size() / 2 - crypto_box_SEALBYTES);
#ifdef HIKUP_DEBUG
	std::cout << "OPEN (dec)| " << message << std::endl;
#endif
}
