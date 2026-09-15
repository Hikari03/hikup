#include "ClientInfo.hpp"

#include <stdexcept>
#include <arpa/inet.h>
#include <openssl/ssl.h>

ClientInfo::ClientInfo () {}
ClientInfo::ClientInfo ( SSL* _conn ) : ip(convertConnToString(_conn)), conn(_conn), valid(true) {}

std::string ClientInfo::getIp() const {
	if ( !valid )
		throw std::logic_error("ClientInfo: this instance is not valid.");

	return ip;
}

SSL* ClientInfo::getConn() const {
	if ( !valid )
		throw std::logic_error("ClientInfo: this instance is not valid.");

	return conn;
}

std::string ClientInfo::convertAddrToString(const sockaddr_in &addr) {
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &addr.sin_addr, ip, INET_ADDRSTRLEN);
	return {ip};
}

std::string ClientInfo::convertConnToString ( const SSL* conn ) {
	const int fd = SSL_get_fd(conn);
	sockaddr_in addr;
	socklen_t len = sizeof(addr);
	getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len);
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));

	return {ip};
}
