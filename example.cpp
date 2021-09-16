#include "sws.h"
#include <iostream>

class WebSocketCB : public sws::IWebSocketCB {
public:
	void OnRecvMessage(sws::OpCodeType opcode, const std::string& msg) {
		if (opcode == sws::TEXT_FRAME) {
			std::cout << "Receive Text: " << msg << std::endl;
		}else if (opcode == sws::BINARY_FRAME) {
			std::cout << "Receive Binary, size:" << msg.size() << std::endl;
		}
	}

	void OnDisconnected(const std::string& msg) {
		std::cout << "OnDisconnected: " << msg << std::endl;
	}
};


int main()
{
	WebSocketCB cb;
	sws::WebSocketClient client;
	bool ret = client.Connect("ws://echo.websocket.org", &cb);
	if (!ret) {
		printf("connect failed, err:%s\n", client.GetLastError().c_str());
		return 1;
	}
	
	while (true) {
		std::string input;
		std::cout << "Enter message to send: ";
		std::getline(std::cin, input);
		if (input == "quit") break;
		else {
			client.SendTextMessage(input);
		}
	}

    return 0;
}
