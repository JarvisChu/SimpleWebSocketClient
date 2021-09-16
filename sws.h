
#ifndef SIMPLE_WSCLIENT_H
#define SIMPLE_WSCLIENT_H

#ifdef _WIN32
    #if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
        #define _CRT_SECURE_NO_WARNINGS // _CRT_SECURE_NO_WARNINGS for sscanf errors in MSVC2013 Express
    #endif

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    #include <fcntl.h>
    #include <WinSock2.h>
    #include <WS2tcpip.h>

    #pragma comment( lib, "ws2_32" )

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <sys/types.h>
    #include <io.h>

    #ifndef _SSIZE_T_DEFINED
        typedef int ssize_t;
        #define _SSIZE_T_DEFINED
    #endif

    #ifndef _SOCKET_T_DEFINED
        typedef SOCKET socket_t;
        #define _SOCKET_T_DEFINED
    #endif

    #ifndef snprintf
        #define snprintf _snprintf_s
    #endif

    #if _MSC_VER >=1600 // vs2010 or later
        #include <stdint.h>
    #else
        typedef __int8 int8_t;
        typedef unsigned __int8 uint8_t;
        typedef __int32 int32_t;
        typedef unsigned __int32 uint32_t;
        typedef __int64 int64_t;
        typedef unsigned __int64 uint64_t;
    #endif

    #define socketerrno WSAGetLastError()
    #define SOCKET_EAGAIN_EINPROGRESS WSAEINPROGRESS
    #define SOCKET_EWOULDBLOCK WSAEWOULDBLOCK
#else
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <sys/types.h>
    #include <unistd.h>
    #include <stdint.h>

    #ifndef _SOCKET_T_DEFINED
        typedef int socket_t;
        #define _SOCKET_T_DEFINED
    #endif

    #ifndef INVALID_SOCKET
        #define INVALID_SOCKET (-1)
    #endif

    #ifndef SOCKET_ERROR
        #define SOCKET_ERROR   (-1)
    #endif

    #define closesocket(s) ::close(s)
    #include <errno.h>
    #define socketerrno errno
    #define SOCKET_EAGAIN_EINPROGRESS EAGAIN
    #define SOCKET_EWOULDBLOCK EWOULDBLOCK
#endif

#include <vector>
#include <string>
#include <mutex>
#include <thread>


namespace sws { // simple websocket client

// WebSocket Protocol Op Code
typedef enum OpCodeType {
    CONTINUATION = 0x0,
    TEXT_FRAME = 0x1,
    BINARY_FRAME = 0x2,
    CLOSE = 8,
    PING = 9,
    PONG = 0xa,
} OpCodeType;

//////////////////////////////////////////////////////////////
// WebSocket
// Low level implementation of WebSocket Client
// You can use WebSocketClient instead of WebSocket

class WebSocket {
public:
    typedef enum readyStateValues { CLOSING, CLOSED, CONNECTING, OPEN } readyStateValues;

    // http://tools.ietf.org/html/rfc6455#section-5.2  Base Framing Protocol
    //
    //  0                   1                   2                   3
    //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-------+-+-------------+-------------------------------+
    // |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
    // |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
    // |N|V|V|V|       |S|             |   (if payload len==126/127)   |
    // | |1|2|3|       |K|             |                               |
    // +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
    // |     Extended payload length continued, if payload len == 127  |
    // + - - - - - - - - - - - - - - - +-------------------------------+
    // |                               |Masking-key, if MASK set to 1  |
    // +-------------------------------+-------------------------------+
    // | Masking-key (continued)       |          Payload Data         |
    // +-------------------------------- - - - - - - - - - - - - - - - +
    // :                     Payload Data continued ...                :
    // + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
    // |                     Payload Data continued ...                |
    // +---------------------------------------------------------------+
    struct wsheader_type {
        unsigned header_size;
        bool fin;
        bool mask;
        OpCodeType opcode;
        int N0;
        uint64_t N;
        uint8_t masking_key[4];
    };

    WebSocket(socket_t sockfd, bool useMask);
    ~WebSocket();
    readyStateValues getReadyState() const;

    // Factory functions
    static WebSocket* fromURL(std::string& errmsg, const std::string& url, const std::string& origin = std::string()) {
        return fromURL(errmsg, url, true, origin);
    }
    static WebSocket* fromURLNoMast(std::string& errmsg, const std::string& url, const std::string& origin = std::string()) {
        return fromURL(errmsg, url, false, origin);
    }

    void poll(int timeout = 0); // timeout in milliseconds
    void send(const std::string& message); // send text
    void sendBinary(const std::string& message);
    void sendBinary(const std::vector<uint8_t>& message);
    void sendPing();
    void close();

    struct CallbackImp { virtual void operator()(OpCodeType opcode, const std::string& message) = 0; };

    // For callbacks that accept a string argument.
    // this is compatible with both C++11 lambdas, functors and C function pointers
    // Callable must have signature: void(OpCodeType opcode, const std::string & message).
    template<class Callable>
    void dispatch(Callable callable) {
        struct _Callback : public CallbackImp {
            Callable& callable;
            _Callback(Callable& callable) : callable(callable) { }
            void operator()(OpCodeType opcode, const std::string& message) override { callable(opcode, message); }
        };
        _Callback callback(callable);
        dispatchInternal(callback);
    }

private:
    static socket_t connectByHostName(const std::string& hostname, int port);
    static WebSocket* fromURL(std::string& errmsg, const std::string& url, bool useMask, const std::string& origin = std::string());

    template<class Iterator>
    void sendData(OpCodeType type, uint64_t message_size, Iterator message_begin, Iterator message_end);

	void dispatchInternal(CallbackImp& callable);

private:
    std::vector<uint8_t> rxbuf;
    std::vector<uint8_t> txbuf;

    std::vector<uint8_t> recved_frame;
    OpCodeType last_opcode = TEXT_FRAME; // record last opcode type for processing CONTINUATION frames

    std::mutex m_mtx_rxbuf;
    std::mutex m_mtx_txbuf;

    socket_t sockfd;
    readyStateValues readyState;
    bool useMask;
    bool isRxBad;
};

//////////////////////////////////////////////////////////////
// WebSocketClient
// High level class based on WebSocket, easy to using

class IWebSocketCB {
public:
    virtual void OnRecvMessage(OpCodeType opcode, const std::string& msg) = 0;
    virtual void OnDisconnected(const std::string& msg) = 0;
};

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    bool Connect(const std::string& wsURI, IWebSocketCB* cb);
    void Disconnect();
    bool SendTextMessage(const std::string& msg);
    bool SendBinaryMessage(const std::vector<uint8_t>& msg);
    std::string GetLastError() const;
private:
    void Run();

private:
    IWebSocketCB* m_cb = nullptr;
    std::shared_ptr<WebSocket> m_ws = nullptr;
    std::string m_wsURI;
    std::thread m_thread;
    bool m_running = false;
    std::string m_errmsg;
};

} // namespace sws

#endif // SIMPLE_WSCLIENT_H