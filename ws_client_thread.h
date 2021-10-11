#pragma once

#include "sws.h"
#include <memory>
#include <thread>
#include <string>
#include <vector>

// WSClientThread
// Based on sws::WebSocketClient
// Connect WebSocket Server in a separated Thread, will try to connect until connected, and automatically ReConnect if disconnect

class WSClientThread: public sws::IWebSocketCB {
public:
	WSClientThread(sws::IWebSocketCB* cb);
	WSClientThread(const std::string& uri, sws::IWebSocketCB* cb);
    virtual ~WSClientThread();

	// uri should set before Start() if you construct WSClientThread without uri parameter
	void SetURI(const std::string& uri);

    void Start();
    void Stop();
    void ReStart();

    bool SendTextMessage(const std::string& msg);
    bool SendBinaryMessage(const std::vector<uint8_t>& msg);

    // IWebSocketCB
    void OnRecvMessage(sws::OpCodeType opcode, const std::string& msg) override;
	void OnDisconnected(const std::string& msg) override;

    bool IsRunning() const { return m_running; }
    bool IsConnected() const { return m_connected; }
    sws::IWebSocketCB* GetCB() const { return m_cb; }

private: 
    void Run();

private:
    sws::IWebSocketCB* m_cb = nullptr;
    std::thread m_thread;
	std::string m_uri;
    sws::WebSocketClient m_wsclient;
    bool m_running = false;
    bool m_connected = false;
};
