#include "net.h"
#include "networkml.h"
#include "hmac_sha256.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <random>
#include <cstdlib>
#include <ctime>

using namespace std;

// shared secret used to sign leaderboard submissions, injected at build time by
// cmake (-DLEADERBOARD_SECRET=...), falls back to a dev value for local testing
#ifndef LEADERBOARD_SECRET
#define LEADERBOARD_SECRET "dev-value"
#endif

string GetEnvVar(const char* name)
{
#ifdef _MSC_VER
	char* buf = nullptr;
	size_t sz = 0;
	if (_dupenv_s(&buf, &sz, name) == 0 && buf)
	{
		string value(buf);
		free(buf);
		return value;
	}
	return "";
#else
	const char* env = std::getenv(name);
	return env ? string(env) : string("");
#endif
}

string SaveDir()
{
#ifdef _WIN32
	string base = GetEnvVar("LOCALAPPDATA");
	if (base.empty()) base = ".";
	return base + "\\FlappyBird";
#elif defined(__APPLE__)
	return GetEnvVar("HOME") + "/Library/Application Support/FlappyBird";
#else
	string base = GetEnvVar("XDG_DATA_HOME");
	if (base.empty()) base = GetEnvVar("HOME") + "/.local/share";
	return base + "/FlappyBird";
#endif
}

string LeaderboardUrl()
{
	string env = GetEnvVar("FLAPPY_LEADERBOARD_URL");
	if (!env.empty()) return env;
	return "https://personalwebsiteclonetest.netlify.app/api/leaderboard";
}

// minimal json string escaping for the player name
static string JsonEscape(const string& s)
{
	string out;
	for (char c : s)
	{
		switch (c)
		{
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n";  break;
		case '\r': out += "\\r";  break;
		case '\t': out += "\\t";  break;
		default:
			if ((unsigned char)c < 0x20) continue; // drop other control chars
			out += c;
		}
	}
	return out;
}

void SubmitScore(const string& name, int score)
{
	string player = name.empty() ? string("Anonymous") : name;
	std::thread([player, score]() {
		try
		{
			string ts = std::to_string((long long)time(nullptr));
			string scoreStr = std::to_string(score);

			// random hex nonce so each request is unique (replay protection)
			string nonce;
			std::random_device rd;
			static const char* hexd = "0123456789abcdef";
			for (int i = 0; i < 24; ++i) nonce += hexd[rd() & 0xf];

			// sign name + score + nonce + timestamp, server recomputes and checks
			string canonical = player + "\n" + scoreStr + "\n" + nonce + "\n" + ts;
			string sig = hmacsha256::hmacHex(LEADERBOARD_SECRET, canonical);

			ML::Requests req;
			string body = "{\"name\":\"" + JsonEscape(player) +
				"\",\"score\":" + scoreStr + "}";
			req.post(LeaderboardUrl(), body, {
				"Content-Type: application/json",
				"X-Timestamp: " + ts,
				"X-Nonce: " + nonce,
				"X-Signature: " + sig
			}, 5);
		}
		catch (...) {}
	}).detach();
}

LbState g_lb;

void FetchLeaderboard()
{
	{
		std::lock_guard<std::mutex> lk(g_lb.mtx);
		g_lb.status = 1;
		g_lb.entries.clear();
	}
	std::thread([]()
	{
		std::vector<std::pair<std::string, int>> parsed;
		int status = 3;
		try
		{
			ML::Requests req;
			ML::Response r = req.get(LeaderboardUrl() + "?limit=10", {}, 5);
			if (r.ok())
			{
				auto j = nlohmann::json::parse(r.body);
				for (auto& e : j)
					parsed.push_back({ e.value("name", std::string("?")), (int)e.value("score", 0) });
				status = 2;
			}
		}
		catch (...) { status = 3; }
		std::lock_guard<std::mutex> lk(g_lb.mtx);
		g_lb.entries = parsed;
		g_lb.status = status;
	}).detach();
}
