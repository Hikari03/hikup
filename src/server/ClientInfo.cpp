#include "ClientInfo.hpp"

bool ClientInfo::init(const std::string & _ip, const int & socket) {
	if(initialized)
		return false;

	this->ip = _ip;
	this->socket_ = socket;
	this->initialized = true;
	return true;
}


std::string ClientInfo::getIp() const {
	return ip;
}

int ClientInfo::getSocket() const {
	return socket_;
}

std::string ClientInfo::convertAddrToString(const sockaddr_in &addr) {
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &addr.sin_addr, ip, INET_ADDRSTRLEN);
	return {ip};
}
