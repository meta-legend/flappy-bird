// minimal HMAC-SHA256 used to sign leaderboard submissions
#pragma once
#include <string>

namespace hmacsha256 {
	// HMAC-SHA256 of msg under key, returned as a lowercase hex string (matches what the server recomputes)
	std::string hmacHex(const std::string& key, const std::string& msg);
}
