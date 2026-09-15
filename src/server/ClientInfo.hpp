#pragma once

#include <string>
#include <netinet/in.h>
#include <openssl/types.h>

struct ClientInfo {
	ClientInfo();
	explicit ClientInfo(SSL* _conn);

	[[nodiscard]] std::string getIp() const;
	[[nodiscard]] SSL* getConn() const;

	static std::string convertAddrToString( const sockaddr_in & addr );
	static std::string convertConnToString( const SSL * conn );

private:
	std::string ip;
	SSL* conn = nullptr;
	bool valid = false;
};
