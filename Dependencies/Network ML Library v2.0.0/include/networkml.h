#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <fstream>
#include <stdio.h>
namespace ML {
	// Rich result of an HTTP request: status code + body.
	// ok() is true for any 2xx status.
	struct Response {
		long status = 0;
		std::string body;
		std::map<std::string, std::string> headers;   // response headers, keys lowercased
		bool ok() const { return status >= 200 && status < 300; }
	};

	// One turn in a conversation.
	struct Message {
		std::string role;     // "system" | "user" | "assistant"
		std::string content;
	};

	class File {
	public:
		// --- create / write ---
		void createFolder(std::string name);                       // single level
		void createFolders(std::string path);                      // recursive (mkdir -p)
		void createFile(std::string name, std::string content);    // create / overwrite
		void appendFile(std::string name, std::string content);    // append to the end
		void writeBytes(std::string name, const std::vector<unsigned char>& data);

		// --- read ---
		std::string readFile(std::string name);                    // text, line-based
		std::string readAll(std::string name);                     // exact bytes (binary-safe)
		std::vector<unsigned char> readBytes(std::string name);    // raw bytes

		// --- delete / copy / move ---
		void deleteFile(std::string name);
		void deleteFolder(std::string name);                       // recursive
		void copyFile(std::string src, std::string dst);           // overwrites dst
		void moveFile(std::string src, std::string dst);           // rename / move

		// --- listing & queries ---
		std::vector<std::string> listFiles(std::string folder);
		bool exists(std::string name);
		bool isFile(std::string name);
		bool isDirectory(std::string name);
		int fileCharacterCount(std::string name);
		long fileSize(std::string name);                           // bytes, -1 if missing
		long long lastModified(std::string name);                  // unix seconds, -1 if missing
	};

	// One part of a multipart/form-data upload: a text field, or a file (set
	// isFile = true and put the file path in `content`).
	struct FormPart {
		std::string name;
		std::string content;     // text value, or file path when isFile is true
		bool isFile = false;
	};

	// Helpers that build Authorization / auth header lines.
	namespace Auth {
		std::string bearer(const std::string& token);                 // "Authorization: Bearer <token>"
		std::string basic(const std::string& user, const std::string& password);  // base64-encoded
		std::string apiKey(const std::string& headerName, const std::string& key);
	}

	// Helpers that build URLs with properly percent-encoded query strings.
	namespace Url {
		std::string encode(const std::string& value);                 // percent-encode one value
		std::string build(const std::string& base,
			const std::map<std::string, std::string>& params);
	}

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

		// --- modern API: returns a full Response (status + body + headers) ---
		// `headers` is a list of full header lines, e.g.
		//   {"Content-Type: application/json", "Authorization: Bearer <token>"}.
		// `timeoutSeconds` = 0 waits indefinitely; > 0 aborts after that long.
		// POST/PUT/PATCH take the request body inline (no temp file needed).
		Response get(std::string url, std::vector<std::string> headers = {}, long timeoutSeconds = 0);
		Response post(std::string url, std::string body, std::vector<std::string> headers = {}, long timeoutSeconds = 0);
		Response put(std::string url, std::string body, std::vector<std::string> headers = {}, long timeoutSeconds = 0);
		Response patch(std::string url, std::string body, std::vector<std::string> headers = {}, long timeoutSeconds = 0);
		Response del(std::string url, std::vector<std::string> headers = {}, long timeoutSeconds = 0);
		Response head(std::string url, std::vector<std::string> headers = {}, long timeoutSeconds = 0);

		// Streams a response body straight to disk (memory-light for big files).
		// onProgress, if set, receives (bytesNow, bytesTotal). Returns true on 2xx.
		bool download(std::string url, std::string filePath,
			std::function<void(long, long)> onProgress = {});

		// multipart/form-data upload (file uploads, etc.). Returns a full Response.
		Response upload(std::string url, std::vector<FormPart> parts,
			std::vector<std::string> headers = {}, long timeoutSeconds = 0);

		// Auto-retry for the verb methods above. Retries on 429 and 5xx, honoring
		// the server's Retry-After header when present, otherwise exponential
		// backoff (baseMs, 2*baseMs, 4*baseMs, ...). Off by default.
		void setRetries(int maxRetries);            // default 0 (no retries)
		void setRetryBackoff(int baseMs);           // default 1000 ms

	private:
		int maxRetries = 0;
		int retryBackoffMs = 1000;
	};

	// LLM backends supported by Chat.
	// (Groq, OpenRouter and DeepSeek use the same wire format as OpenAI
	// internally, but are distinct choices so callers don't have to know that or
	// set a custom host.)
	enum class Provider { Ollama, OpenAI, Anthropic, Groq, OpenRouter, DeepSeek };

	// Conversational LLM client. Talks to a local Ollama server by default, or
	// to OpenAI / Anthropic when constructed with a Provider and an API key.
	// Holds a running message history so the model remembers context across calls.
	class Chat {
	public:
		// Construct for any provider. The provider is always explicit; the API
		// key is only used by cloud providers (Ollama and other local backends
		// ignore it). Leave host empty to use the provider's default endpoint
		// (http://localhost:11434 for Ollama).
		Chat(Provider provider, std::string model, std::string apiKey = "",
			std::string host = "");

		// Null-safe overload: apiKey may come straight from std::getenv and be
		// null; a null key is treated as empty so a missing environment variable
		// does not crash the caller.
		Chat(Provider provider, std::string model, const char* apiKey,
			std::string host = "");

		// Set a persistent system instruction (model behaviour/persona).
		void setSystem(std::string instruction);

		// Sampling temperature: 0.0 = focused/deterministic, ~1.0 = creative.
		void setTemperature(double temperature);

		// Max tokens to generate. Required by Anthropic; optional for others.
		void setMaxTokens(int maxTokens);

		// Sends the prompt WITH the running history and returns the reply text.
		// On success, both the prompt and reply are appended to history.
		// Returns an empty string on failure.
		std::string ask(std::string prompt);

		// Lower-level, stateless: sends the prompt (plus existing history) and
		// returns the full HTTP Response without modifying history.
		Response raw(std::string prompt);

		// Streams the reply token-by-token as it is generated. onToken is called
		// repeatedly with each chunk of answer text. On a 2xx response the
		// exchange is appended to history (same as ask()). Returns the HTTP
		// status code (200 on success; 0 if the request could not be sent).
		//
		// onReasoning is optional: for reasoning models (e.g. gpt-oss,
		// deepseek-r1) it receives the model's "thinking" chunks as they stream,
		// separate from the answer. Leave it unset to ignore reasoning.
		long stream(std::string prompt,
			std::function<void(const std::string&)> onToken,
			std::function<void(const std::string&)> onReasoning = {});

		// The reasoning/"thinking" text from the most recent ask() or stream()
		// (empty for non-reasoning models). The answer itself is returned by
		// ask() / delivered to onToken.
		const std::string& lastReasoning() const;

		// Clears conversation history (keeps the system instruction, if any).
		void reset();

		// Read-only access to the current conversation.
		const std::vector<Message>& history() const;

		// Persist the conversation (including the system prompt) to a JSON file
		// and load it back, so a chat can survive across runs. Both return false
		// on I/O or parse failure.
		bool saveHistory(const std::string& path) const;
		bool loadHistory(const std::string& path);

	private:
		std::string endpoint() const;
		std::string buildBody(const std::string& prompt, bool streaming);

		Provider provider;
		std::string apiKey;
		std::string model;
		std::string host;
		std::vector<Message> messages;
		double temperature;
		int maxTokens;
		std::string reasoningText;
	};
}
