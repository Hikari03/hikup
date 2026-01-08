#pragma once

#include <string>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>

struct ClientInfo {

	ClientInfo() = default;

	bool init(const std::string & _ip, const int & socket);

	[[nodiscard]] std::string getIp() const;
	[[nodiscard]] int getSocket() const;

	static std::string convertAddrToString(const sockaddr_in & addr);

	std::string ip;
	int socket_ = 0;

	bool initialized = false;

private:
	std::vector<std::string> onlineUsers;

};
