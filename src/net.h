// leaderboard networking (signed, async) plus a few path/env helpers
#pragma once
#include <string>
#include <vector>
#include <mutex>

// POST a score on a detached thread (fire-and-forget); the body is HMAC-SHA256 signed
// (name + score + nonce + timestamp) so the server can reject forged submissions
void SubmitScore(const std::string& name, int score);

// leaderboard snapshot shared between the fetch thread and the UI — hold mtx for any access
struct LbState
{
	std::mutex mtx;
	enum class Status {IDLE, LOADING, OK, ERROR};
	Status status = Status::IDLE;
	std::vector<std::pair<std::string, int>> entries;
};
extern LbState g_lb;

// start an async fetch on a detached thread; it fills g_lb and flips g_lb.status, which the UI polls under mtx
void FetchLeaderboard();

// per-user data directory (LOCALAPPDATA / XDG / Application Support by platform); holds the save file
std::string SaveDir();

// directory photo-mode screenshots are written to
std::string ScreenshotDir();

// std::getenv wrapper returning a std::string ("" when the variable is unset)
std::string GetEnvVar(const char* name);

// base URL of the leaderboard service
std::string LeaderboardUrl();
