#include <condition_variable>
#include <iostream>

#include "ClientInfo.hpp"
#include "utils.hpp"

inline void accepter ( std::condition_variable& callBack,
                       const int& serverSocket,
                       ClientInfo& acceptedClient,
                       bool& newClientAccepted,
                       const bool& turnOff ) {
	while ( true ) {
		if ( turnOff )
			return;

		if ( !newClientAccepted ) {
			sockaddr_in clientAddress{};
			socklen_t clientAddressSize = sizeof( clientAddress );

			const auto socket = accept(serverSocket, reinterpret_cast<struct sockaddr*>(&clientAddress),
			                                &clientAddressSize);

			if ( turnOff )
				return;

			if ( socket < 0 )
				continue;

			acceptedClient = ClientInfo();
			acceptedClient.init(ClientInfo::convertAddrToString(clientAddress), socket);

			Utils::log("main: accepted client number " + std::to_string(acceptedClient.getSocket()) + " with addr " + acceptedClient.getIp());

			newClientAccepted = true;
			callBack.notify_one();
		}
	}
}
