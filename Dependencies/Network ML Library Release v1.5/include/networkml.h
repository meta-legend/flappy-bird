#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <stdio.h>
namespace ML {
	class File {
	public:
		void createFolder(std::string name);
		void createFile(std::string name, std::string content);
		std::string readFile(std::string name);
		void deleteFile(std::string name);
		int fileCharacterCount(std::string name);
	};

	class Requests {
	public:
		std::string pingDomain(std::string domain);
		std::string getReq(std::string url);
		std::string getReq(std::string url, std::string headers);
		std::string postReq(std::string url, std::string jsonPayload, std::string headers);
		std::string headReq(std::string url);
		std::string headReq(std::string url, std::string headers);
		std::string putReq(std::string url, std::string headers, std::string payload);
		std::string deleteReq(std::string url);
		std::string deleteReq(std::string url, std::string headers);
	};
}


