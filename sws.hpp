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
    readyStateValues getReadyState() const { return readyState; }

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
    struct BytesCallbackImp { virtual void operator()(OpCodeType opcode, const std::vector<uint8_t>& message) = 0; };

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

    // For callbacks that accept a std::vector<uint8_t> argument.
    // this is compatible with both C++11 lambdas, functors and C function pointers
    // Callable must have signature: void(OpCodeType opcode, const std::vector<uint8_t> & message).
    template<class Callable>
    void dispatchBinary(Callable callable) {
        struct _Callback : public BytesCallbackImp {
            Callable& callable;
            _Callback(Callable& callable) : callable(callable) { }
            void operator()(OpCodeType opcode, const std::vector<uint8_t>& message) override { callable(opcode, message); }
        };
        _Callback callback(callable);
        dispatchBinaryInternal(callback);
    }

private:
    static socket_t connectByHostName(const std::string& hostname, int port);
    static WebSocket* fromURL(std::string& errmsg, const std::string& url, bool useMask, const std::string& origin = std::string());

    template<class Iterator>
    void sendData(OpCodeType type, uint64_t message_size, Iterator message_begin, Iterator message_end);

    void dispatchInternal(CallbackImp& callable);
    void dispatchBinaryInternal(BytesCallbackImp& callable);

private:
    std::vector<uint8_t> rxbuf;
    std::vector<uint8_t> txbuf;

    std::vector<uint8_t> recved_frame;
    OpCodeType last_opcode = sws::TEXT_FRAME; // record last opcode type for processing CONTINUATION frames

    std::mutex m_mtx_rxbuf;
    std::mutex m_mtx_txbuf;

    socket_t sockfd;
    readyStateValues readyState;
    bool useMask;
    bool isRxBad;
};

WebSocket::WebSocket(socket_t sockfd, bool useMask)
    : sockfd(sockfd)
    , readyState(OPEN)
    , useMask(useMask)
    , isRxBad(false) {
}

WebSocket::~WebSocket() {}

socket_t WebSocket::connectByHostName(const std::string& hostname, int port){
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *p;
    int ret;
    socket_t sockfd = INVALID_SOCKET;
    char sport[16];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(sport, 16, "%d", port);
    if ((ret = getaddrinfo(hostname.c_str(), sport, &hints, &result)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerrorA(ret));
        return 1;
    }
    for (p = result; p != NULL; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == INVALID_SOCKET) { continue; }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) != SOCKET_ERROR) {
            break;
        }
        closesocket(sockfd);
        sockfd = INVALID_SOCKET;
    }
    freeaddrinfo(result);
    return sockfd;
}

WebSocket* WebSocket::fromURL(std::string& errmsg, const std::string& url, bool useMask, const std::string& origin /*= std::string()*/) {
    if (url.size() >= 512) {
        errmsg = "ERROR: url size limit exceeded: " + url;
        return nullptr;
    }

    if (origin.size() >= 200) {
        errmsg = "ERROR: origin size limit exceeded: " + origin;
        return nullptr;
    }

    // parse url
    char host[512];
    int port = 0;
    char path[512];
    if (sscanf(url.c_str(), "ws://%[^:/]:%d/%s", host, &port, path) == 3) {
    }
    else if (sscanf(url.c_str(), "ws://%[^:/]/%s", host, path) == 2) {
        port = 80;
    }
    else if (sscanf(url.c_str(), "ws://%[^:/]:%d", host, &port) == 2) {
        path[0] = '\0';
    }
    else if (sscanf(url.c_str(), "ws://%[^:/]", host) == 1) {
        port = 80;
        path[0] = '\0';
    }
    else {
        errmsg = "ERROR: Could not parse WebSocket url: " + url;
        return nullptr;
    }

    printf("swsclient: connecting: host=%s port=%d path=/%s\n", host, port, path);

    socket_t sockfd = connectByHostName(host, port);
    if (sockfd == INVALID_SOCKET) {
        errmsg = "Unable to connect to " + std::string(host) + ":" + std::to_string(port);
        return nullptr;
    }

    {
        // XXX: this should be done non-blocking,
        char line[1024];
        int status;
        int i;
        snprintf(line, 1024, "GET /%s HTTP/1.1\r\n", path); ::send(sockfd, line, strlen(line), 0);
        if (port == 80) {
            snprintf(line, 1024, "Host: %s\r\n", host); ::send(sockfd, line, strlen(line), 0);
        }
        else {
            snprintf(line, 1024, "Host: %s:%d\r\n", host, port); ::send(sockfd, line, strlen(line), 0);
        }
        snprintf(line, 1024, "Upgrade: websocket\r\n"); ::send(sockfd, line, strlen(line), 0);
        snprintf(line, 1024, "Connection: Upgrade\r\n"); ::send(sockfd, line, strlen(line), 0);
        if (!origin.empty()) {
            snprintf(line, 1024, "Origin: %s\r\n", origin.c_str()); ::send(sockfd, line, strlen(line), 0);
        }
        snprintf(line, 1024, "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"); ::send(sockfd, line, strlen(line), 0);
        snprintf(line, 1024, "Sec-WebSocket-Version: 13\r\n"); ::send(sockfd, line, strlen(line), 0);
        snprintf(line, 1024, "\r\n"); ::send(sockfd, line, strlen(line), 0);
        for (i = 0; i < 2 || (i < 1023 && line[i - 2] != '\r' && line[i - 1] != '\n'); ++i) { if (recv(sockfd, line + i, 1, 0) == 0) { return NULL; } }
        line[i] = 0;
        if (i == 1023) { fprintf(stderr, "ERROR: Got invalid status line connecting to: %s\n", url.c_str()); return NULL; }
        if (sscanf(line, "HTTP/1.1 %d", &status) != 1 || status != 101) { fprintf(stderr, "ERROR: Got bad status connecting to %s: %s", url.c_str(), line); return NULL; }
        // TODO: verify response headers,
        while (true) {
            for (i = 0; i < 2 || (i < 1023 && line[i - 2] != '\r' && line[i - 1] != '\n'); ++i) { if (recv(sockfd, line + i, 1, 0) == 0) { return NULL; } }
            if (line[0] == '\r' && line[1] == '\n') { break; }
        }
    }

    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag)); // Disable Nagle's algorithm
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(sockfd, FIONBIO, &on);
#else
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
#endif
    printf("Connected to: %s\n", url.c_str());
    return (WebSocket*)(new WebSocket(sockfd, useMask));
}

void WebSocket::poll(int timeout /*= 0*/) { // timeout in milliseconds
    if (readyState == CLOSED) {
        if (timeout > 0) {
            timeval tv = { timeout / 1000, (timeout % 1000) * 1000 };
            select(0, NULL, NULL, NULL, &tv);
        }
        return;
    }
    if (timeout != 0) {
        fd_set rfds;
        fd_set wfds;
        timeval tv = { timeout / 1000, (timeout % 1000) * 1000 };
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(sockfd, &rfds);
        if (txbuf.size()) { FD_SET(sockfd, &wfds); }
        select(sockfd + 1, &rfds, &wfds, 0, timeout > 0 ? &tv : 0);
    }


    // recv
    std::vector<uint8_t> tmpBuf;
    tmpBuf.resize(1500);
    while (true) {
        // FD_ISSET(0, &rfds) will be true

        ssize_t ret = recv(sockfd, (char*)&tmpBuf[0], 1500, 0);
        if (ret < 0 && (socketerrno == SOCKET_EWOULDBLOCK || socketerrno == SOCKET_EAGAIN_EINPROGRESS)) {
            break;
        }
        else if (ret <= 0) {
            closesocket(sockfd);
            readyState = CLOSED;
            fputs(ret < 0 ? "Connection error!\n" : "Connection closed!\n", stderr);
            break;
        }
        else {
            m_mtx_rxbuf.lock();
            rxbuf.insert(rxbuf.end(), tmpBuf.begin(), tmpBuf.begin() + ret);
            m_mtx_rxbuf.unlock();
        }
    }

    // send
    tmpBuf.clear();
    m_mtx_txbuf.lock();
    txbuf.swap(tmpBuf);
    m_mtx_txbuf.unlock();

    while (tmpBuf.size()) {
        int ret = ::send(sockfd, (char*)&tmpBuf[0], tmpBuf.size(), 0);
        if (ret < 0 && (socketerrno == SOCKET_EWOULDBLOCK || socketerrno == SOCKET_EAGAIN_EINPROGRESS)) {
            break;
        }
        else if (ret <= 0) {
            closesocket(sockfd);
            readyState = CLOSED;
            fputs(ret < 0 ? "Connection error!\n" : "Connection closed!\n", stderr);
            break;
        }
        else {
            tmpBuf.erase(tmpBuf.begin(), tmpBuf.begin() + ret);
        }
    }

    if (!tmpBuf.size() && readyState == CLOSING) {
        closesocket(sockfd);
        readyState = CLOSED;
    }
}

void WebSocket::send(const std::string& message) {
    sendData(sws::TEXT_FRAME, message.size(), message.begin(), message.end());
}

void WebSocket::sendBinary(const std::string& message) {
    sendData(sws::BINARY_FRAME, message.size(), message.begin(), message.end());
}

void WebSocket::sendBinary(const std::vector<uint8_t>& message) {
    sendData(sws::BINARY_FRAME, message.size(), message.begin(), message.end());
}

void WebSocket::sendPing() {
    std::string empty;
    sendData(sws::PING, empty.size(), empty.begin(), empty.end());
}

template<class Iterator>
void WebSocket::sendData(OpCodeType type, uint64_t message_size, Iterator message_begin, Iterator message_end) {
    // TODO:
    // Masking key should (must) be derived from a high quality random
    // number generator, to mitigate attacks on non-WebSocket friendly
    // middleware:
    const uint8_t masking_key[4] = { 0x12, 0x34, 0x56, 0x78 };

    if (readyState == CLOSING || readyState == CLOSED) { return; }
    std::vector<uint8_t> header;
    header.assign(2 + (message_size >= 126 ? 2 : 0) + (message_size >= 65536 ? 6 : 0) + (useMask ? 4 : 0), 0);
    header[0] = 0x80 | type;
    if (false) {}
    else if (message_size < 126) {
        header[1] = (message_size & 0xff) | (useMask ? 0x80 : 0);
        if (useMask) {
            header[2] = masking_key[0];
            header[3] = masking_key[1];
            header[4] = masking_key[2];
            header[5] = masking_key[3];
        }
    }
    else if (message_size < 65536) {
        header[1] = 126 | (useMask ? 0x80 : 0);
        header[2] = (message_size >> 8) & 0xff;
        header[3] = (message_size >> 0) & 0xff;
        if (useMask) {
            header[4] = masking_key[0];
            header[5] = masking_key[1];
            header[6] = masking_key[2];
            header[7] = masking_key[3];
        }
    }
    else { // TODO: run coverage testing here
        header[1] = 127 | (useMask ? 0x80 : 0);
        header[2] = (message_size >> 56) & 0xff;
        header[3] = (message_size >> 48) & 0xff;
        header[4] = (message_size >> 40) & 0xff;
        header[5] = (message_size >> 32) & 0xff;
        header[6] = (message_size >> 24) & 0xff;
        header[7] = (message_size >> 16) & 0xff;
        header[8] = (message_size >> 8) & 0xff;
        header[9] = (message_size >> 0) & 0xff;
        if (useMask) {
            header[10] = masking_key[0];
            header[11] = masking_key[1];
            header[12] = masking_key[2];
            header[13] = masking_key[3];
        }
    }

    // N.B. - txbuf will keep growing until it can be transmitted over the socket:

    m_mtx_txbuf.lock();
    txbuf.insert(txbuf.end(), header.begin(), header.end());
    txbuf.insert(txbuf.end(), message_begin, message_end);
    if (useMask) {
        size_t message_offset = txbuf.size() - message_size;
        for (size_t i = 0; i != message_size; ++i) {
            txbuf[message_offset + i] ^= masking_key[i & 0x3];
        }
    }
    m_mtx_txbuf.unlock();

}

void WebSocket::close() {
    if (readyState == CLOSING || readyState == CLOSED) { return; }
    readyState = CLOSING;
    uint8_t closeFrame[6] = { 0x88, 0x80, 0x00, 0x00, 0x00, 0x00 }; // last 4 bytes are a masking key
    std::vector<uint8_t> header(closeFrame, closeFrame + 6);

    m_mtx_txbuf.lock();
    txbuf.insert(txbuf.end(), header.begin(), header.end());
    m_mtx_txbuf.unlock();
}

void WebSocket::dispatchInternal(CallbackImp & callable) {

    // Adapt void(const std::string<uint8_t>&) to void(const std::string&)
    struct CallbackAdapter : public BytesCallbackImp {
        CallbackImp& callable;
        CallbackAdapter(CallbackImp& callable) : callable(callable) { }
        void operator()(OpCodeType opcode, const std::vector<uint8_t>& message) override {
            std::string stringMessage(message.begin(), message.end());
            callable(opcode, stringMessage);
        }
    };

    CallbackAdapter bytesCallback(callable);
    dispatchBinaryInternal(bytesCallback);
}

void WebSocket::dispatchBinaryInternal(BytesCallbackImp & callable) {

    if (isRxBad) {
        return;
    }

    while (true) {
        if (rxbuf.size() < 2) break; // Need at least 2

        wsheader_type ws;
        const uint8_t * data = (uint8_t *)&rxbuf[0]; // peek, but don't consume
        ws.fin = (data[0] & 0x80) == 0x80;
        ws.opcode = (OpCodeType)(data[0] & 0x0f);
        ws.mask = (data[1] & 0x80) == 0x80;
        ws.N0 = (data[1] & 0x7f);
        ws.header_size = 2 + (ws.N0 == 126 ? 2 : 0) + (ws.N0 == 127 ? 8 : 0) + (ws.mask ? 4 : 0);

        if (rxbuf.size() < ws.header_size) break; // Need: ws.header_size >= rxbuf.size()

        int i = 0;
        if (ws.N0 < 126) {
            ws.N = ws.N0;
            i = 2;
        }
        else if (ws.N0 == 126) {
            ws.N = 0;
            ws.N |= ((uint64_t)data[2]) << 8;
            ws.N |= ((uint64_t)data[3]) << 0;
            i = 4;
        }
        else if (ws.N0 == 127) {
            ws.N = 0;
            ws.N |= ((uint64_t)data[2]) << 56;
            ws.N |= ((uint64_t)data[3]) << 48;
            ws.N |= ((uint64_t)data[4]) << 40;
            ws.N |= ((uint64_t)data[5]) << 32;
            ws.N |= ((uint64_t)data[6]) << 24;
            ws.N |= ((uint64_t)data[7]) << 16;
            ws.N |= ((uint64_t)data[8]) << 8;
            ws.N |= ((uint64_t)data[9]) << 0;
            i = 10;
            if (ws.N & 0x8000000000000000ull) {
                // https://tools.ietf.org/html/rfc6455 writes the "the most
                // significant bit MUST be 0."
                //
                // We can't drop the frame, because (1) we don't we don't
                // know how much data to skip over to find the next header,
                // and (2) this would be an impractically long length, even
                // if it were valid. So just close() and return immediately
                // for now.
                isRxBad = true;
                fprintf(stderr, "ERROR: Frame has invalid frame length. Closing.\n");
                close();
                break;
            }
        }
        if (ws.mask) {
            ws.masking_key[0] = ((uint8_t)data[i + 0]) << 0;
            ws.masking_key[1] = ((uint8_t)data[i + 1]) << 0;
            ws.masking_key[2] = ((uint8_t)data[i + 2]) << 0;
            ws.masking_key[3] = ((uint8_t)data[i + 3]) << 0;
        }
        else {
            ws.masking_key[0] = 0;
            ws.masking_key[1] = 0;
            ws.masking_key[2] = 0;
            ws.masking_key[3] = 0;
        }

        // Note: The checks above should hopefully ensure this addition cannot overflow:
        if (rxbuf.size() < ws.header_size + ws.N)  break; // Need: ws.header_size+ws.N - rxbuf.size()

        // We got a whole message, now do something with it:
        if (ws.opcode == sws::TEXT_FRAME || ws.opcode == sws::BINARY_FRAME || ws.opcode == sws::CONTINUATION) {

            OpCodeType opcode = sws::CONTINUATION;

            m_mtx_rxbuf.lock();
            if (ws.mask) { for (size_t i = 0; i != ws.N; ++i) { rxbuf[i + ws.header_size] ^= ws.masking_key[i & 0x3]; } }

            if (ws.opcode == sws::TEXT_FRAME || (ws.opcode == sws::CONTINUATION && last_opcode == sws::TEXT_FRAME)) {
                opcode = sws::TEXT_FRAME;
                recved_frame.insert(recved_frame.end(), rxbuf.begin() + ws.header_size, rxbuf.begin() + ws.header_size + (size_t)ws.N);// just feed
            }
            else if (ws.opcode == sws::BINARY_FRAME || (ws.opcode == sws::CONTINUATION && last_opcode == sws::BINARY_FRAME)) {
                opcode = sws::BINARY_FRAME;
                recved_frame.insert(recved_frame.end(), rxbuf.begin() + ws.header_size, rxbuf.begin() + ws.header_size + (size_t)ws.N);// just feed
            }

            rxbuf.erase(rxbuf.begin(), rxbuf.begin() + ws.header_size + (size_t)ws.N);
            m_mtx_rxbuf.unlock();

            if (ws.fin) {
                callable(opcode, (const std::vector<uint8_t>) recved_frame);
                recved_frame.clear();
                std::vector<uint8_t>().swap(recved_frame);// free memory
            }
        }

        else if (ws.opcode == sws::PING) {
            m_mtx_rxbuf.lock();
            if (ws.mask) { for (size_t i = 0; i != ws.N; ++i) { rxbuf[i + ws.header_size] ^= ws.masking_key[i & 0x3]; } }
            std::string data(rxbuf.begin() + ws.header_size, rxbuf.begin() + ws.header_size + (size_t)ws.N);
            rxbuf.erase(rxbuf.begin(), rxbuf.begin() + ws.header_size + (size_t)ws.N);
            m_mtx_rxbuf.unlock();

            sendData(sws::PONG, data.size(), data.begin(), data.end());
        }

        else if (ws.opcode == sws::PONG) {
            m_mtx_rxbuf.lock();
            rxbuf.erase(rxbuf.begin(), rxbuf.begin() + ws.header_size + (size_t)ws.N);
            m_mtx_rxbuf.unlock();
        }
        else if (ws.opcode == sws::CLOSE) {
            m_mtx_rxbuf.lock();
            rxbuf.erase(rxbuf.begin(), rxbuf.begin() + ws.header_size + (size_t)ws.N);
            m_mtx_rxbuf.unlock();
            close();
        }
        else {
            m_mtx_rxbuf.lock();
            rxbuf.erase(rxbuf.begin(), rxbuf.begin() + ws.header_size + (size_t)ws.N);
            m_mtx_rxbuf.unlock();
            fprintf(stderr, "ERROR: Got unexpected WebSocket message.\n"); close();
        }
    }
}

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

WebSocketClient::WebSocketClient() {
#ifdef _WIN32
    INT rc;
    WSADATA wsaData;

    rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (rc) {
        printf("WSAStartup Failed.\n");
        return;
    }
#endif
};

WebSocketClient::~WebSocketClient() {
    Disconnect();

#ifdef _WIN32
    WSACleanup();
#endif
}

bool WebSocketClient::Connect(const std::string& wsURI, IWebSocketCB* cb) {
    if (m_running) return true;

    m_ws.reset(WebSocket::fromURL(m_errmsg, wsURI));
    if (!m_ws) {
        printf("create ws failed, errmsg:%s\n", m_errmsg.c_str());
        return false;
    }
    m_wsURI = wsURI;
    m_cb = cb;

    // start internal thread
    m_running = true;

    std::thread tmpThread(&WebSocketClient::Run, this);
    m_thread.swap(tmpThread);
    return true;
}

void WebSocketClient::Disconnect() {
    if (m_running) {
        m_running = false;

        if (m_thread.joinable()) {
            m_thread.join();
        }

        m_ws = nullptr;
        m_cb = nullptr;
    }
}

bool WebSocketClient::SendTextMessage(const std::string& msg) {
    if (m_running && m_ws) {
        m_ws->send(msg);
        return true;
    }

    return false;
}

bool WebSocketClient::SendBinaryMessage(const std::vector<uint8_t>& msg) {
    if (m_running && m_ws) {
        m_ws->sendBinary(msg);
        return true;
    }

    return false;
}

std::string WebSocketClient::GetLastError() const {
    return m_errmsg;
}

void WebSocketClient::Run() {
    while (m_running && m_ws && m_ws->getReadyState() != sws::WebSocket::CLOSED) {
        m_ws->poll();
        m_ws->dispatch([&](OpCodeType opcode, const std::string& msg) {
            printf("recv ws message, opcode:%d, message_size:%d\n", opcode, msg.size());
            if (m_cb) {
                m_cb->OnRecvMessage(opcode, msg);
            }
        });
    }

    // disconnected by WebSocketClient::Disconnect, send close frame 
    if (!m_running && m_ws) {
        m_ws->close();
        m_ws->poll();
    }

    // disconnected by ws error, not by WebSocketClient::Disconnect, call callback
    if (m_running && m_ws && m_ws->getReadyState() == sws::WebSocket::CLOSED && m_cb) {
        m_cb->OnDisconnected("disconnected");
    }
}
} // namespace sws

#endif // SIMPLE_WSCLIENT_H
