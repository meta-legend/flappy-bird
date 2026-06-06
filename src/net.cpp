#include "net.h"
#include "networkml.h"
#include "hmac_sha256.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <random>
#include <cstdlib>
#include <ctime>


// shared secret used to sign leaderboard submissions, injected at build time by cmake (-DLEADERBOARD_SECRET=...);
// falls back to a dev value for local testing
#ifndef LEADERBOARD_SECRET
#define LEADERBOARD_SECRET "dev-value"
#endif

std::string GetEnvVar(const char* name)
{
#ifdef _MSC_VER
	// MSVC deprecates getenv; _dupenv_s hands back an allocated copy we must free
	char* buf = nullptr;
	size_t sz = 0;
	if (_dupenv_s(&buf, &sz, name) == 0 && buf)
	{
		std::string value(buf);
		free(buf);
		return value;
	}
	return "";
#else
	const char* env = std::getenv(name);
	return env ? std::string(env) : std::string("");
#endif
}

std::string SaveDir()
{
	// per-OS app-data location, falling back to "." if the env var is missing
#ifdef _WIN32
	std::string base = GetEnvVar("LOCALAPPDATA");
	if (base.empty()) base = ".";
	return base + "\\FlappyBird";
#elif defined(__APPLE__)
	return GetEnvVar("HOME") + "/Library/Application Support/FlappyBird";
#else
	std::string base = GetEnvVar("XDG_DATA_HOME");
	if (base.empty()) base = GetEnvVar("HOME") + "/.local/share";
	return base + "/FlappyBird";
#endif
}

std::string ScreenshotDir()
{
	// screenshots are user-facing artifacts meant to be found/shared, so they go in the OS picture library —
	// NOT next to save.bin in AppData (hidden, and on an installed build the exe dir often isn't even writable)
#ifdef _WIN32
	std::string home = GetEnvVar("USERPROFILE");
	if (home.empty()) return ".\\screenshots";
	return home + "\\Pictures\\Flappy Bird";
#elif defined(__APPLE__)
	std::string home = GetEnvVar("HOME");
	if (home.empty()) return "./screenshots";
	return home + "/Pictures/Flappy Bird";
#else
	std::string base = GetEnvVar("XDG_PICTURES_DIR");
	if (base.empty())
	{
		std::string home = GetEnvVar("HOME");
		if (home.empty()) return "./screenshots";
		base = home + "/Pictures";
	}
	return base + "/Flappy Bird";
#endif
}

std::string LeaderboardUrl()
{
	// FLAPPY_LEADERBOARD_URL overrides the endpoint (e.g. point dev builds at a local server); otherwise the live one
	std::string env = GetEnvVar("FLAPPY_LEADERBOARD_URL");
	if (!env.empty()) return env;
	return "https://mxtalegend.netlify.app/api/leaderboard";
}

// minimal JSON string escaper for the hand-built request body (nlohmann is only pulled in to parse the response)
static std::string JsonEscape(const std::string& s)
{
	std::string out;
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
				if ((unsigned char)c < 0x20) continue; // drop other control chars rather than emit invalid JSON
				out += c;
		}
	}
	return out;
}

// fire-and-forget on a detached thread; any failure is swallowed so a flaky network never disrupts the game
void SubmitScore(const std::string& name, int score)
{
	std::string player = name.empty() ? std::string("Anonymous") : name;
	std::thread([player, score]() {
		try
		{
			std::string ts = std::to_string((long long)time(nullptr));
			std::string scoreStr = std::to_string(score);

			// random hex nonce so each request is unique (replay protection)
			std::string nonce;
			std::random_device rd;
			static const char* hexd = "0123456789abcdef";
			for (int i = 0; i < 24; ++i) nonce += hexd[rd() & 0xf];

			// sign name + score + nonce + timestamp; the server recomputes the same HMAC and rejects mismatches
			std::string canonical = player + "\n" + scoreStr + "\n" + nonce + "\n" + ts;
			std::string sig = hmacsha256::hmacHex(LEADERBOARD_SECRET, canonical);

			ML::Requests req;
			std::string body = "{\"name\":\"" + JsonEscape(player) +
				"\",\"score\":" + scoreStr + "}";
			req.post(LeaderboardUrl(), body, {
				"Content-Type: application/json",
				"X-Timestamp: " + ts,
				"X-Nonce: " + nonce,
				"X-Signature: " + sig
			}, 5);   // 5s timeout
		}
		catch (...) {}
	}).detach();
}

LbState g_lb;

void FetchLeaderboard()
{
	// flip to LOADING under the lock so the UI can show a spinner immediately, then do the slow fetch off-lock
	{
		std::lock_guard<std::mutex> lk(g_lb.mtx);
		g_lb.status = LbState::Status::LOADING;
		g_lb.entries.clear();
	}
	std::thread([]()
	{
		std::vector<std::pair<std::string, int>> parsed;
		LbState::Status localStatus = LbState::Status::ERROR;
		try {
			ML::Requests req;
			ML::Response r = req.get(LeaderboardUrl() + "?limit=10", {}, 5);
			if (r.ok())
			{
				auto j = nlohmann::json::parse(r.body);
				for (auto& e : j)
					parsed.push_back({ e.value("name", std::string("?")), (int)e.value("score", 0) });
				localStatus = LbState::Status::OK;
			}
		}
		catch (...) { localStatus = LbState::Status::ERROR; }
		// publish results under the lock in one shot, so the UI never observes a half-written list
		std::lock_guard<std::mutex> lk(g_lb.mtx);
		g_lb.entries = parsed;
		g_lb.status = localStatus;
	}).detach();
}
