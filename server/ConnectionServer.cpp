#include "ConnectionServer.hpp"

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
	timeout.tv_sec = 5; // Timeout in seconds
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

	auto pk_hex = std::make_unique<char[]>(crypto_box_PUBLICKEYBYTES * 2 + 1);

	sodium_bin2hex(pk_hex.get(), crypto_box_PUBLICKEYBYTES * 2 + 1, _keyPair.publicKey, crypto_box_PUBLICKEYBYTES);

	std::cout << "public key: " << pk_hex.get() << std::endl;

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	send(_internal"publicKey:" + std::string(pk_hex.get(), crypto_box_PUBLICKEYBYTES * 2));

	receive();

	if ( !_message.contains(_internal"publicKey:") )
		throw std::runtime_error("Could not receive pubKey");

	auto pubKey_hex = _message.substr(strlen(_internal"publicKey:"));

	if ( sodium_hex2bin(_remotePublicKey, crypto_box_PUBLICKEYBYTES, pubKey_hex.c_str(), pubKey_hex.size(), nullptr,
	                    nullptr, nullptr) < 0 ) { throw std::runtime_error("Could not decode public key"); }

	//std::cout << "pubKey: " << _remotePublicKey << std::endl;

	for ( auto& string: _messagesBuffer )
		secretOpen(string);

	_encrypted = true;
}

void ConnectionServer::clearBuffer () { memset(_buffer, '\0', _sizeOfPreviousMessage); }

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
		//std::cout << "RECEIVE BUFFER BEFORE CLEAR|  " << _clientInfo.socket_ << ": " << _buffer << std::endl;
		clearBuffer();

		_sizeOfPreviousMessage = recv(_clientInfo.socket_, _buffer, 256*1024, 0);


		if ( _sizeOfPreviousMessage < 0 ) {
			if ( errno != EAGAIN && errno != EWOULDBLOCK )
				throw std::runtime_error("client disconnected or could not receive message");

			if ( errno == EAGAIN || errno == EWOULDBLOCK )
				throw std::runtime_error("timeout");
		}

		_message += _buffer;

		//std::cout << "RECEIVE |  " << _clientInfo.socket_ << (_clientInfo.name.empty() ? "" : "/" + _clientInfo.name ) << ": " << _message << std::endl;
	}

	//std::cout << "RECEIVE |  " << _clientInfo.socket_ << ": " << _message << std::endl;

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

void ConnectionServer::send ( const std::string& message ) const {
	auto messageToSend = message;

	//std::cout << "SEND1 |  " << _clientInfo.socket_ << (_clientInfo.name.empty() ? "" : "/" + _clientInfo.name ) << ": " << messageToSend << std::endl;

	if ( _encrypted ) { secretSeal(messageToSend); }
	messageToSend += _end;

	//std::cout << "SEND2 |  " << _clientInfo.socket_ << (_clientInfo.name.empty() ? "" : "/" + _clientInfo.name ) << ": " << messageToSend << std::endl;
	if ( !_active )
		return;
	try {
		if ( ::send(_clientInfo.socket_, messageToSend.c_str(), messageToSend.length(), 0) < 0 ) {
			throw std::runtime_error("Could not send message to client");
		}
	}
	catch ( std::exception& ) { throw std::runtime_error("Could not send message to client"); }
}

void ConnectionServer::sendData ( const std::string& message ) const { send(_data + message); }

void ConnectionServer::sendInternal ( const std::string& message ) const { send(_internal + message); }

void ConnectionServer::secretSeal ( std::string& message ) const {
	const auto cypherText = std::make_unique<unsigned char[]>(crypto_box_SEALBYTES + message.size());

	if ( crypto_box_seal(cypherText.get(), reinterpret_cast<const unsigned char*>(message.c_str()), message.size(),
	                     _remotePublicKey) < 0 )
		throw std::runtime_error("Could not encrypt message");

	const auto messageHex = std::make_unique<unsigned char[]>(( crypto_box_SEALBYTES + message.size() ) * 2 + 1);

	sodium_bin2hex(reinterpret_cast<char*>(messageHex.get()), ( crypto_box_SEALBYTES + message.size() ) * 2 + 1,
	               cypherText.get(), crypto_box_SEALBYTES + message.size());

	message = std::string(reinterpret_cast<char*>(messageHex.get()), ( crypto_box_SEALBYTES + message.size() ) * 2);
}

void ConnectionServer::secretOpen ( std::string& message ) const {
	const auto cypherTextBin = std::make_unique<unsigned char[]>(message.size() / 2);

	if ( sodium_hex2bin(cypherTextBin.get(), message.size() / 2, reinterpret_cast<const char*>(message.c_str()),
	                    message.size(), nullptr, nullptr, nullptr) < 0 )
		throw std::runtime_error("Could not decode message");

	const auto decrypted = std::make_unique<unsigned char[]>(message.size() / 2 - crypto_box_SEALBYTES);

	if ( crypto_box_seal_open(decrypted.get(), cypherTextBin.get(), message.size() / 2, _keyPair.publicKey,
	                          _keyPair.secretKey) < 0 )
		throw std::runtime_error("Could not decrypt message");

	message = std::string(reinterpret_cast<char*>(decrypted.get()), message.size() / 2 - crypto_box_SEALBYTES);
}
