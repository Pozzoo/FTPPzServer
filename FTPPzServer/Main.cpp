#include <iostream>
#include <WS2tcpip.h>
#include <json.hpp>
#include <thread>
#include <fstream>
#include <filesystem>

#pragma comment (lib, "ws2_32.lib")

constexpr auto DELIMITER = "\r\n\r\n";

nlohmann::json handlePutOperation(std::string content, nlohmann::json json) {
	uint64_t calculatedHash = std::hash<std::string>{}(content);

	std::string messageStr;
	nlohmann::json messageJson;
	std::string file = json.at("file");

	if (json.at("hash") != calculatedHash) {
		std::cout << "Error on file transfer: Hash does not Match!" << std::endl;

		messageJson = {
			{"file", file},
			{"operation", "put"},
			{"status", "fail"}
		};

	} else {	
		std::ofstream copy("archive/" + file, std::ios::binary);
		copy << content;
	
		messageJson = {
			{"file", file},
			{"operation", "put"},
			{"status", "success"}
		};
	}
	
	return messageJson;
}

std::tuple<nlohmann::json, std::string> handleGetOperation(std::string fileName) {
	std::ifstream file("archive/" + fileName, std::ios::binary);

	if (!file) {
		std::cerr << "Error opening file" << std::endl;
		return { nullptr, " "};
	}

	std::string content(std::istreambuf_iterator<char>(file), {});

	nlohmann::json messageJson = {
		{"file", fileName},
		{ "operation", "get" },
		{ "hash",  std::hash<std::string>{}(content + DELIMITER)},
	};

	return { messageJson, content };
}

nlohmann::json handleListOperation() {
	std::vector<std::string> files;

	for (const auto& entry : std::filesystem::directory_iterator("./archive")) {
		files.push_back(entry.path().string());
	}

	nlohmann::json messageJson = {
		{"operation", "list"},
		{"items", files},
	};

	return messageJson;
}

void handleConnection(SOCKET clientSocket) {
	while (true) {
		int stat;

		char buf[1024] = { 0 };
		std::string output = { 0 };
		nlohmann::json commandInfo;

		while ((stat = recv(clientSocket, buf, sizeof(buf) - 1, 0)) > 0) {
			output.append(buf, stat);

			//FOR TESTING POURPOSES
			std::cout << stat << std::endl;

			if (output.find(DELIMITER) != std::string::npos) {
				break;
			}
		}

		if (stat == 0) {
			std::cout << "Connection closed by client." << std::endl;
		} else if (stat < 0) {
			std::cerr << "Receive error: " << WSAGetLastError() << std::endl;
		}

		std::string content = output.substr(output.find_first_of("}") + 1);
		std::string jsonStr = { 0 };

		jsonStr = output.substr(1, output.find_first_of("}"));
		nlohmann::json json;

		try {
			json = nlohmann::json::parse(jsonStr);
		} catch (const nlohmann::json::exception& e) {
			std::cerr << e.what() << std::endl;
			closesocket(clientSocket);
			return;
		}

		std::cout << json << std::endl;

		nlohmann::json replyJson = nullptr;
		std::string rplContent = " ";

		if (json.at("command") == "put") {
			replyJson = handlePutOperation(content, json);
		} else if (json.at("command") == "get") {
			std::tie(replyJson, rplContent) = handleGetOperation(json.at("file"));
		} else {
			replyJson = handleListOperation();
		}

		if (replyJson != nullptr) {
			std::string messageStr = replyJson.dump();
			messageStr.append(rplContent + DELIMITER);

			send(clientSocket, messageStr.c_str(), messageStr.size(), 0);
		}
	}
	
}

void main() {

	WSADATA wsData;
	WORD ver = MAKEWORD(2, 2);

	int wsErr = WSAStartup(ver, &wsData);

	if (wsErr != 0) {
		std::cerr << "Can´t Initialize Winsock!" << std::endl;
		return;
	}

	SOCKET listeningSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listeningSock == INVALID_SOCKET) {
		std::cerr << "Can't create a socket: " << WSAGetLastError() << std::endl;
		WSACleanup();
		return;
	}

	sockaddr_in hint;
	hint.sin_family = AF_INET;
	hint.sin_port = htons(5400);
	hint.sin_addr.S_un.S_addr = INADDR_ANY;

	if (bind(listeningSock, (sockaddr*)&hint, sizeof(hint)) == SOCKET_ERROR) {
		std::cerr << "Can't bind socket: " << WSAGetLastError() << std::endl;
		WSACleanup();
		return;
	}

	while (true) {
		if (listen(listeningSock, SOMAXCONN) == SOCKET_ERROR) {
			std::cerr << "Can't listen on socket: " << WSAGetLastError() << std::endl;
			WSACleanup();
			return;
		}

		sockaddr_in client;
		int clientSize = sizeof(client);

		SOCKET clientSocket = accept(listeningSock, (sockaddr*)&client, &clientSize);
		if (clientSocket == INVALID_SOCKET) {
			std::cerr << "Can't accept connection: " << WSAGetLastError() << std::endl;
		} else {
			std::cout << "A client connected on port " << ntohs(client.sin_port) << std::endl;

			std::thread nThread(handleConnection, clientSocket);
			nThread.detach();
		}
	}



	WSACleanup();
}