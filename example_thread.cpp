#include "ws_client_thread.h"
#include <iostream>

class WebSocketCB : public sws::IWebSocketCB {
public:
    void OnRecvMessage(sws::OpCodeType opcode, const std::string& msg) {
        if (opcode == sws::TEXT_FRAME) {
            std::cout << "Receive Text: " << msg << std::endl;
        }
        else if (opcode == sws::BINARY_FRAME) {
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
	WSClientThread client("ws://echo.websocket.org", &cb);

	client.Start(); // will running on a separated thread

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
