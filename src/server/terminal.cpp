#include <condition_variable>
#include <iostream>

#include "utils.hpp"

inline void terminal ( std::condition_variable& callBack, bool& turnOff ) {
	while ( !turnOff ) {
		std::string input;
		std::cin >> input;
		if ( input == "q" ) {
			turnOff = true;
			Utils::log(std::string("terminal: ") + "turning off server, please wait up to 15 seconds");
			callBack.notify_one();
			return;
		}
	}
}
