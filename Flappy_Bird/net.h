// environment/save-path helpers and the online leaderboard (submit + fetch)
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <mutex>

// portable getenv (uses _dupenv_s on msvc to avoid the C4996 /sdl error)
std::string GetEnvVar(const char* name);

// per-user folder for save data (high score, player name, settings)
std::string SaveDir();

// the leaderboard endpoint (overridable with the FLAPPY_LEADERBOARD_URL env var)
std::string LeaderboardUrl();

// post a score to the leaderboard on a detached thread (signed, non-blocking)
void SubmitScore(const std::string& name, int score);

// shared state for the in-game leaderboard view, filled by a background thread
struct LbState
{
	std::mutex mtx;
	std::vector<std::pair<std::string, int>> entries;
	int status = 0; // 0 idle, 1 loading, 2 done, 3 error
};
extern LbState g_lb;

// fetch the top scores into g_lb on a background thread
void FetchLeaderboard();
