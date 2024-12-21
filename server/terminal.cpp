#include <condition_variable>
#include <iostream>

#define _terminal "terminal: "

inline void printT(const std::string& message){
	std::cout << _terminal << message << std::endl;
}

inline void terminal(std::condition_variable & callBack, bool & turnOff) {
	while ( !turnOff ) {
		std::string input;
		std::cin >> input;
		if(input == "q") {
			turnOff = true;
			printT("turning off server, please wait up to 15 seconds");
			callBack.notify_one();
			return;
		}
	}

}
