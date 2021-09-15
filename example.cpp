#include "simplewsclient.hpp"

#include <assert.h>
#include <stdio.h>
#include <string>
#include <memory>
#include <thread>

void SendPcmAudio(simplewsclient::WebSocketClient& client, int sampleRate, int sampleBits, int channel, const char* file)
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
		client.SendBinaryMessage(vec);
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

class WebSocketCB : public simplewsclient::IWebSocketCB {
public:
	void OnRecvMessage(simplewsclient::OpCodeType opcode, const std::string& msg) {
		
		static int total_binary_size = 0;
		if (opcode == simplewsclient::TEXT_FRAME) {
			printf("Receive Text, size:%d, data:%s\n", msg.size(), msg.c_str());
		}if (opcode == simplewsclient::BINARY_FRAME) {
			total_binary_size += msg.size();
			printf("Receive Binary, size:%d, total:%d\n", msg.size(), total_binary_size);
			DebugWriteFile(msg);
		}
	}

	void OnDisconnected(const std::string& msg) {
		printf("OnDisconnected: %s\n", msg.c_str());
	}
};


int main()
{
	simplewsclient::WebSocketClient client;
	WebSocketCB cb;

	while (true) {
		int option = 0;
		printf("\n---------------------------------\n");
		printf("1. connect\n");
		printf("2. disconnect\n");
		printf("3. set param as user, subscribe all\n");
		printf("4. send pcm audio 8000_16_1_01.pcm\n");
		printf("5. quick test\n");
		printf("6. quit\n");
		printf("\n---------------------------------\n");
		printf("Input your choice: ");
		scanf("%d", &option);

		if (option == 1) {
			bool ret = client.Connect("ws://127.0.0.1:12071/mix", &cb);
			if (!ret) {
				printf("connect failed, err:%s\n", client.GetLastError().c_str());
			}
		}

		else if (option == 2){
			client.Disconnect();
		}

		else if (option == 3){
			client.SendTextMessage("{\"id\":2,\"role\":\"user\",\"audio_format\":\"pcm\",\"sample_rate\":8000,\"sample_bits\":16,\"channel_cnt\":1,\"need_subscribe\":true,\"subscribe_list\":[\"user\",\"seat\",\"manager\"]}");
		}

		else if (option == 4) {
			SendPcmAudio(client, 8000, 16, 1, "C:\\8000_16_1_01.pcm");
		}
		
		else if (option == 5){
			simplewsclient::WebSocketClient client1;
			WebSocketCB cb1;

			bool ret = client1.Connect("ws://127.0.0.1:12071/mix", &cb);
			if (!ret) {
				printf("connect failed, err:%s\n", client1.GetLastError().c_str());
				continue;
			}

			client1.SendTextMessage("{\"id\":2,\"role\":\"user\",\"audio_format\":\"pcm\",\"sample_rate\":8000,\"sample_bits\":16,\"channel_cnt\":1,\"need_subscribe\":true,\"subscribe_list\":[\"user\",\"seat\",\"manager\"]}");
			//SendPcmAudio(client1, 8000, 16, 1, "C:\\8000_16_1_01.pcm");	
		}

		else if (option == 6) {
			break;
		}
	}

    return 0;
}
