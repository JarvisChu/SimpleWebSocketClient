// Compile with:
// g++ -std=gnu++0x example-client-cpp11.cpp -o example-client-cpp11
#include "easywsclient.hpp"
//#include "easywsclient.cpp" // <-- include only if you don't want compile separately
#ifdef _WIN32
#pragma comment( lib, "ws2_32" )
#include <WinSock2.h>
#endif
#include <assert.h>
#include <stdio.h>
#include <string>
#include <memory>
#include <thread>


void SendPcmAudio(std::shared_ptr<easywsclient::WebSocket> ws, int sampleRate, int sampleBits, int channel, const char* file)
{
	FILE* fp = nullptr;
	int ret = fopen_s(&fp, file, "rb");
	if (ret != 0 || !fp) {
		printf("open pcm file failed\n");
		return;
	}

	printf("sending [%s] ... \n", file);

	int bytesPerMillSecond = sampleRate * sampleBits * channel / 2 / 1000;
	int bytesPer20MillSecond = bytesPerMillSecond * 20;

	int totalSend = 0;
	uint8_t* buf = new uint8_t[bytesPer20MillSecond];
	while (true) {
		int readCnt = fread(buf, sizeof(uint8_t), bytesPer20MillSecond, fp);
		if (readCnt <= 0) {
			printf("send over\n");
			break;
		}

		std::vector<uint8_t> vec;
		vec.insert(vec.end(), buf, buf + readCnt);
		ws->sendBinary(vec);
		totalSend += readCnt;

		Sleep(20);
	}
	delete[]buf;

	printf("pcm data total bytes send: %d\n", totalSend);
	fclose(fp);
}

void DebugWriteFile(const std::string& data) {
	FILE* fp = nullptr;
	char file[128] = { 0 };
	snprintf(file, 128, "C:\\recvd_mixed_client_%d.pcm", 1);
	int ret = fopen_s(&fp, file, "ab");
	if (ret == 0 && fp) {
		//fwrite(pcmData, sizeof(short), len, fp);
		fwrite(data.data(), sizeof(uint8_t), data.size(), fp);
		fflush(fp);
		fclose(fp);
	}
}

int main()
{
#ifdef _WIN32
    INT rc;
    WSADATA wsaData;

    rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (rc) {
        printf("WSAStartup Failed.\n");
        return 1;
    }
#endif

	//std::shared_ptr<easywsclient::WebSocket> ws(easywsclient::WebSocket::from_url("ws://127.0.0.1:12061/Mixer"));
	std::shared_ptr<easywsclient::WebSocket> ws(easywsclient::WebSocket::from_url("ws://127.0.0.1:9100/mix"));
	if (ws == nullptr) {
		printf("create ws failed\n");
		return 1;
	}

	// SetParam
	ws->send("{\"id\":2,\"role\":\"user\",\"audio_format\":\"pcm\",\"sample_rate\":8000,\"sample_bits\":16,\"channel_cnt\":1,\"need_subscribe\":true,\"subscribe_list\":[\"user\",\"seat\",\"manager\"]}");

	std::thread t1(SendPcmAudio, ws, 8000, 16, 1, "C:\\8000_16_1_01.pcm");

	int total_binary_size = 0;
    while (ws->getReadyState() != easywsclient::WebSocket::CLOSED) {
        //WebSocket::pointer wsp = &*ws; // <-- because a unique_ptr cannot be copied into a lambda
        ws->poll();
        ws->dispatch([&total_binary_size, ws](easywsclient::opcode_type opcode, const std::string & message) {
			if (opcode == easywsclient::TEXT_FRAME) {
				printf("Receive Text, size:%d, data:%s\n", message.size(), message.c_str());
			}if (opcode == easywsclient::BINARY_FRAME) {
				total_binary_size += message.size();
				printf("Receive Binary, size:%d, total:%d\n", message.size(), total_binary_size);
				DebugWriteFile(message);
			}	
			if (message == "quit") { /*wsp->close();*/ ws->close(); }
        });
    }

	if (t1.joinable()) {
		t1.join();
	}

#ifdef _WIN32
    WSACleanup();
#endif
    // N.B. - unique_ptr will free the WebSocket instance upon return:
    return 0;
}
