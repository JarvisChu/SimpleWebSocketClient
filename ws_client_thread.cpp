#include "ws_client_thread.h"

WSClientThread::WSClientThread(sws::IWebSocketCB* cb)
    :m_cb(cb){
}

WSClientThread::WSClientThread(const std::string& uri, sws::IWebSocketCB* cb)
	: m_uri(uri), m_cb(cb) {
}

WSClientThread::~WSClientThread(){
    Stop();
}

void WSClientThread::SetURI(const std::string& uri) {
	m_uri = uri;
}

void WSClientThread::Start(){
    std::thread tmpThread(&WSClientThread::Run, this);
    m_thread.swap(tmpThread);
}

void WSClientThread::Stop(){
    if(m_running){
        m_running = false;
        if(m_thread.joinable()){
            m_thread.join();
        }
        m_wsclient.Disconnect();
        m_connected = false;
    }
}

void WSClientThread::Run(){
    m_running = true;
    m_connected = false;

    // try to connect WebSocket Server until connected
	while(m_running){
        if( m_wsclient.Connect(m_uri, this) ){
            m_connected = true;
            printf("connect ws server success, %s\n", m_uri.c_str());
            return;
        }

        printf("try connect to ws server [%s] failed, will retry\n", m_uri.c_str());

        if(!m_running) break;

        // sleep and retry later
        Sleep(500);
    }
}

void WSClientThread::ReStart(){
	// Restart (Stop + Start) MUST in another Thread, to avoid deadlock
	// m_wsclient running thread -> OnDisconnect() -> ReStart() -> Stop() -> m_wsclient.Disconnect() -> Waiting m_wsclient running thread to end, DEADLOCK!
    std::thread([&](){
        Stop();
        Start();
    }).detach();
}

bool WSClientThread::SendTextMessage(const std::string& msg){
    return m_wsclient.SendTextMessage(msg);
}

bool WSClientThread::SendBinaryMessage(const std::vector<uint8_t>& msg){
    return m_wsclient.SendBinaryMessage(msg);
}

void WSClientThread::OnRecvMessage(sws::OpCodeType opcode, const std::string& msg)
{
    if(m_cb){
        m_cb->OnRecvMessage(opcode, msg);
    } 
}

void WSClientThread::OnDisconnected(const std::string& msg)
{
    printf("disconnected, retry now\n");
    ReStart();
}