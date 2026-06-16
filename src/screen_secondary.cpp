#include "screen_secondary.h"

#include "constants.h"
#include "gameplay_helpers.h"
#include "net.h"
#include "save.h"
#include "system_display.h"
#include "types.h"
#include "ui.h"

#include <mutex>
#include <string>

bool DrawLeaderboardScreen(bool& lbDaily, const SaveData& sd, Vector2 vmouse)
{
	DrawText("Leaderboard", VIRTUAL_W / 2 - MeasureText("Leaderboard", 50) / 2, 45, 50, DARKBLUE);
	if (UiButton(Rectangle{ VIRTUAL_W / 2 - 175, 100, 165, 32 }, "All-Time", vmouse))
	{
		if (lbDaily)
		{
			lbDaily = false;
			FetchLeaderboard();   // re-query when switching back to the all-time board
		}
	}
	if (UiButton(Rectangle{ VIRTUAL_W / 2 + 10, 100, 165, 32 }, "Today", vmouse)) lbDaily = true;
	DrawRectangleLinesEx(Rectangle{ (lbDaily ? VIRTUAL_W / 2 + 10.0f : VIRTUAL_W / 2 - 175.0f), 100, 165, 32 }, 3, GOLD);   // gold outline marks the active tab

	if (lbDaily)
	{
		DrawText("Daily leaderboard coming soon", VIRTUAL_W / 2 - MeasureText("Daily leaderboard coming soon", 24) / 2, 280, 24, RAYWHITE);
		if (sd.lastDailyDate == TodayYMD())
		{
			std::string dailyScore = "Your score today: " + std::to_string(sd.lastDailyScore);
			DrawText(dailyScore.c_str(), VIRTUAL_W / 2 - MeasureText(dailyScore.c_str(), 22) / 2, 320, 22, SKYBLUE);
		}
	}
	else
	{
		// g_lb is filled by the async fetch thread, so read its status + entries under the lock
		std::lock_guard<std::mutex> lk(g_lb.mtx);
		if (g_lb.status == LbState::Status::LOADING)
			DrawText("Loading...", VIRTUAL_W / 2 - MeasureText("Loading...", 28) / 2, 280, 28, RAYWHITE);
		else if (g_lb.status == LbState::Status::ERROR)
			DrawText("Couldn't load leaderboard", VIRTUAL_W / 2 - MeasureText("Couldn't load leaderboard", 24) / 2, 280, 24, RED);
		else if (g_lb.entries.empty())
			DrawText("No scores yet!", VIRTUAL_W / 2 - MeasureText("No scores yet!", 26) / 2, 280, 26, RAYWHITE);
		else
		{
			const int ix = (VIRTUAL_W - 800) / 2;   // center the 800-wide list on the wider canvas
			int y = 160;
			int rank = 1;
			for (auto& e : g_lb.entries)
			{
				std::string line = std::to_string(rank) + ":  " + e.first;
				DrawText(line.c_str(), 210 + ix, y, 26, RAYWHITE);
				std::string sc = std::to_string(e.second);
				DrawText(sc.c_str(), 590 + ix - MeasureText(sc.c_str(), 26), y, 26, SKYBLUE);   // right-align the score
				y += 36;
				if (++rank > 10) break;
			}
		}
	}

	bool backClicked = UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 540.0f - FillModeBottomCrop(), 180, 40 }, "Back", vmouse);
	DrawText("or press ESC", VIRTUAL_W / 2 - MeasureText("or press ESC", 16) / 2, 584, 16, BLUE);
	return backClicked;
}

bool DrawCreditsScreen(Vector2 vmouse)
{
	DrawText("Credits", VIRTUAL_W / 2 - MeasureText("Credits", 60) / 2, 90, 60, DARKBLUE);
	DrawText("Made by: meta_legend", VIRTUAL_W / 2 - MeasureText("Made by: meta_legend", 30) / 2, 220, 30, RAYWHITE);
	DrawText("Theme music by HeatleyBros", VIRTUAL_W / 2 - MeasureText("Theme music by HeatleyBros", 26) / 2, 290, 26, RAYWHITE);

	// the channel URL is a clickable hot-zone, opened via the system browser
	const char* url = "youtube.com/@HeatleyBros";
	int uw = MeasureText(url, 20);
	Rectangle urlRect = { VIRTUAL_W / 2 - uw / 2.0f, 332, (float)uw, 24 };
	bool urlHover = CheckCollisionPointRec(vmouse, urlRect);
	DrawText(url, (int)urlRect.x, (int)urlRect.y, 20, urlHover ? SKYBLUE : BLUE);
	if (urlHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
		OpenUrl("https://www.youtube.com/channel/UCsLlqLIE-TqDq3lh5kU2PeA");

	bool backClicked = UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 470, 180, 42 }, "Back", vmouse);
	DrawText("or press ESC", VIRTUAL_W / 2 - MeasureText("or press ESC", 16) / 2, 516, 16, BLUE);
	return backClicked;
}

bool DrawTrophiesScreen(int bestScore, Texture2D (&medalTextures)[Constants::Medals::Count], Vector2 vmouse)
{
	DrawText("Trophy Cabinet", VIRTUAL_W / 2 - MeasureText("Trophy Cabinet", 50) / 2, 60, 50, DARKBLUE);
	DrawText("Your Normal Mode high score unlocks medals", VIRTUAL_W / 2 - MeasureText("Your Normal Mode high score unlocks medals", 20) / 2, 130, 20, RAYWHITE);
	const char* medalNames[Constants::Medals::Count] = { "Bronze", "Silver", "Gold", "Platinum", "Diamond", "Ruby" };
	for (std::size_t m = 0; m < Constants::Medals::Count; m++)
	{
		float cx = (m + 0.5f) * (VIRTUAL_W / (float)Constants::Medals::Count);   // evenly space the six medals across the width
		float cy = 280.0f;
		bool unlocked = bestScore >= Constants::Medals::Thresholds[m];
		float mScale = 3.0f;
		Texture2D md = medalTextures[m];
		Vector2 pos = { cx - md.width * mScale / 2, cy - md.height * mScale / 2 };
		DrawTextureEx(md, pos, 0.0f, mScale, unlocked ? WHITE : Color{ 35, 35, 35, 255 });   // locked medals draw near-black
		DrawText(medalNames[m], (int)(cx - MeasureText(medalNames[m], 20) / 2), (int)(cy + 50), 20, unlocked ? DARKBLUE : GRAY);
		std::string req = unlocked ? std::string("Unlocked") : ("Normal " + std::to_string(Constants::Medals::Thresholds[m]));
		DrawText(req.c_str(), (int)(cx - MeasureText(req.c_str(), 16) / 2), (int)(cy + 75), 16, unlocked ? DARKGREEN : GRAY);
	}
	bool backClicked = UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 470, 180, 42 }, "Back", vmouse);
	DrawText("or press ESC", VIRTUAL_W / 2 - MeasureText("or press ESC", 16) / 2, 516, 16, BLUE);
	return backClicked;
}

bool DrawStatsScreen(const SaveData& sd, int bestScore, Vector2 vmouse)
{
	DrawText("Stats", VIRTUAL_W / 2 - MeasureText("Stats", 50) / 2, 45, 50, DARKBLUE);
	int hrs = (int)(sd.playtimeSeconds / 3600), mins = ((int)sd.playtimeSeconds % 3600) / 60;
	// labels[] and vals[] are parallel rows, drawn label-left / value-right
	const char* labels[9] = { "Normal Best Score", "Classic Best Score", "Daily Best Score", "Games Played", "Total Pipes", "Total Flaps", "Total Deaths", "Daily Wins", "Playtime" };
	std::string vals[9] = {
		std::to_string(bestScore), std::to_string(sd.bestClassicScore), std::to_string(sd.bestDailyScore),
		std::to_string(sd.totalGames), std::to_string(sd.totalPipes), std::to_string(sd.totalFlaps), std::to_string(sd.totalDeaths),
		std::to_string(sd.dailyCount), std::to_string(hrs) + "h " + std::to_string(mins) + "m"
	};
	const int ix = (VIRTUAL_W - 800) / 2;   // center the 800-wide rows on the wider canvas
	int ry = 140;
	for (int i = 0; i < 9; i++)
	{
		DrawText(labels[i], 220 + ix, ry, 24, RAYWHITE);
		DrawText(vals[i].c_str(), 580 + ix - MeasureText(vals[i].c_str(), 24), ry, 24, SKYBLUE);
		ry += 39;
	}
	return UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 524.0f - FillModeBottomCrop(), 180, 40 }, "Back", vmouse);
}

bool DrawAchievementsScreen(const SaveData& sd, Vector2 vmouse)
{
	DrawText("Achievements", VIRTUAL_W / 2 - MeasureText("Achievements", 46) / 2, 24, 46, DARKBLUE);
	// achMask is a bitmask, one bit per Achievement; count the set bits for the "x / N unlocked" header
	int got = 0;
	for (int i = 0; i < Constants::Achievements::Count; i++) if ((sd.achMask >> i) & 1ull) got++;
	std::string hdr = std::to_string(got) + " / " + std::to_string(Constants::Achievements::Count) + " unlocked";
	DrawText(hdr.c_str(), VIRTUAL_W / 2 - MeasureText(hdr.c_str(), 20) / 2, 78, 20, RAYWHITE);
	const float ix = (VIRTUAL_W - 800) / 2.0f;   // center the two-column grid on the wider canvas
	for (int i = 0; i < Constants::Achievements::Count; i++)
	{
		bool on = (sd.achMask >> i) & 1ull;
		float ax = ((i % 2 == 0) ? 60.0f : 410.0f) + ix;   // even index = left column, odd = right
		float ay = 112.0f + (i / 2) * 64.0f;
		DrawRectangleRec(Rectangle{ ax, ay, 330, 56 }, on ? Color{ 30, 60, 40, 255 } : Color{ 35, 35, 35, 255 });
		DrawText(Constants::Achievements::Names[i], (int)ax + 10, (int)ay + 8, 20, on ? GOLD : GRAY);
		DrawText(on ? Constants::Achievements::Descriptions[i] : "???", (int)ax + 10, (int)ay + 32, 16, on ? RAYWHITE : DARKGRAY);   // hide the how-to until earned
	}
	return UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 532.0f - FillModeBottomCrop(), 180, 38 }, "Back", vmouse);
}

bool DrawInfoScreen(const std::string& playerName, Vector2 vmouse)
{
	DrawText("Info", VIRTUAL_W / 2 - MeasureText("Info", 46) / 2, 24, 46, DARKBLUE);
	float colX = VIRTUAL_W / 2 - 200;

	DrawText("About", (int)colX, 100, 24, DARKBLUE);
	std::string pl = "Player: " + (playerName.empty() ? std::string("(unset)") : playerName);
	DrawText(pl.c_str(), (int)colX + 10, 140, 20, RAYWHITE);
#ifdef FLAPPY_VERSION
	DrawText(("Version: " FLAPPY_VERSION), (int)colX + 10, 170, 20, RAYWHITE);   // FLAPPY_VERSION is injected from the project version by cmake
#else
	DrawText("Version: dev", (int)colX + 10, 170, 20, RAYWHITE);
#endif

	DrawText("Credits", (int)colX, 230, 24, DARKBLUE);
	DrawText("Game by: meta_legend", (int)colX + 10, 270, 20, RAYWHITE);
	DrawText("Music: HeatleyBros", (int)colX + 10, 300, 20, RAYWHITE);
	const char* url = "youtube.com/@HeatleyBros";
	int uw = MeasureText(url, 16);
	Rectangle urlRect = { colX + 10, 326, (float)uw, 20 };
	bool urlHover = CheckCollisionPointRec(vmouse, urlRect);
	DrawText(url, (int)urlRect.x, (int)urlRect.y, 16, urlHover ? SKYBLUE : BLUE);
	if (urlHover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
		OpenUrl("https://www.youtube.com/channel/UCsLlqLIE-TqDq3lh5kU2PeA");
	DrawText("Art pack: Megacrash (CC0)", (int)colX + 10, 360, 20, RAYWHITE);
	DrawText("Font: PublicPixel", (int)colX + 10, 390, 20, RAYWHITE);

	bool backClicked = UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 530.0f - FillModeBottomCrop(), 180, 40 }, "Back", vmouse);
	DrawText("or press ESC", VIRTUAL_W / 2 - MeasureText("or press ESC", 16) / 2, 578, 16, BLUE);
	return backClicked;
}
