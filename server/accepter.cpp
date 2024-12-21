#include <condition_variable>
#include <iostream>

#include "ClientInfo.h"

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

			acceptedClient.socket_ = accept(serverSocket, reinterpret_cast<struct sockaddr*>(&clientAddress),
			                                &clientAddressSize);

			if ( turnOff )
				return;

			if ( acceptedClient.socket_ < 0 )
				continue;

			acceptedClient.ip = ClientInfo::convertAddrToString(clientAddress);

			std::cout << "main: accepted client number " << acceptedClient.socket_ << " with addr " << acceptedClient.ip
					<< std::endl;

			newClientAccepted = true;
			callBack.notify_one();
			//std::cout << "main: accepted client number " << acceptedSocket << std::endl;
		}
	}
}
