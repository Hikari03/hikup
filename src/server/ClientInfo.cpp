#include "ClientInfo.hpp"

#include <iostream>
#include <stdexcept>
#include <arpa/inet.h>
#include <openssl/err.h>
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
	std::string ip;

#if OPENSSL_VERSION_MAJOR > 3
	BIO_ADDR *peer = BIO_ADDR_new();

	if (peer && SSL_get_peer_addr(conn, peer) == 1) {
		// Extract numeric IP and port (1 = numeric format)
		char *ip_str = BIO_ADDR_hostname_string(peer, 1);
		char *port_str = BIO_ADDR_service_string(peer, 1);

		if (ip_str) {
			std::cout << "Peer IP: " << ip_str << ":" << (port_str ? port_str : "") << std::endl;
			ip = ip_str;
			// OpenSSL allocated these strings, so free them with OPENSSL_free
			OPENSSL_free(ip_str);
			OPENSSL_free(port_str);
		}
	} else {
		std::cerr << "Could not retrieve QUIC peer address" << std::endl;
	}

	BIO_ADDR_free(peer);
#else
	SSL_client_version(conn);
	ip = std::to_string(rand()) + "(unknown)";
#endif



	return {ip};
}
