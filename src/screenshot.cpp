#include "game.h"

#include "constants.h"
#include "font.h"
#include "run_artifacts.h"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// windows.h declares functions that collide with raylib's (CloseWindow, ShowCursor, LoadImage, DrawText, Rectangle).
// rename them to *Win32 just across the include, then undef so the rest of this file uses raylib's versions
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CloseWindow CloseWindowWin32
#define ShowCursor ShowCursorWin32
#define LoadImage LoadImageWin32
#define DrawText DrawTextWin32
#define Rectangle RectangleWin32
#include <windows.h>
#undef Rectangle
#undef DrawText
#undef LoadImage
#undef ShowCursor
#undef CloseWindow
// font.h's DrawText / MeasureText macros got nuked by the line above; re-establish them so the toast still
// routes through gPixelFont (the raylib originals are still reachable via DrawTextEx / MeasureTextEx)
#define DrawText DrawTextPx
#define MeasureText MeasureTextPx
#endif

namespace
{
	std::mutex gScreenshotToastMutex;
	std::vector<std::string> gScreenshotToasts;   // filled by worker threads, drained on the main thread

	// thread-safe push; the main thread pulls these in FlushScreenshotToasts
	void QueueScreenshotToast(std::string message)
	{
		std::lock_guard<std::mutex> lock(gScreenshotToastMutex);
		gScreenshotToasts.push_back(std::move(message));
	}

	bool CopyScreenshotToClipboard(Image& image);

	// off-thread: flip, write the PNG, copy to the clipboard, then queue a result toast — keeps the disk/encode work
	// off the frame so capturing never hitches the game
	void SaveScreenshotImageAsync(Image image, std::string shotDirectory, std::string playerName, int score, int bestScore)
	{
		std::thread([image, shotDirectory = std::move(shotDirectory), playerName = std::move(playerName), score, bestScore]() mutable
		{
			ImageFlipVertical(&image);
			const std::string name = WriteFrameScreenshotImage(image, shotDirectory, playerName, score, bestScore);
			const bool copied = CopyScreenshotToClipboard(image);
			UnloadImage(image);
			QueueScreenshotToast(std::string(copied ? "Saved + copied " : "Saved ") + name);
		}).detach();
	}

	// copy the image onto the Windows clipboard as a CF_DIB; no-op (false) on other platforms
	bool CopyScreenshotToClipboard(Image& image)
	{
#ifdef _WIN32
		if (!IsImageValid(image)) return false;
		ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

		const std::size_t pixelBytes = static_cast<std::size_t>(image.width) * image.height * 4;
		HGLOBAL clipboardMemory = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + pixelBytes);
		if (clipboardMemory == nullptr)
		{
			return false;
		}

		auto* header = static_cast<BITMAPINFOHEADER*>(GlobalLock(clipboardMemory));
		if (header == nullptr)
		{
			GlobalFree(clipboardMemory);
			return false;
		}

		*header = {};
		header->biSize = sizeof(BITMAPINFOHEADER);
		header->biWidth = image.width;
		header->biHeight = -image.height; // negative = top-down rows, matching the exported PNG
		header->biPlanes = 1;
		header->biBitCount = 32;
		header->biCompression = BI_RGB;
		header->biSizeImage = static_cast<DWORD>(pixelBytes);

		// pixel data follows the header; swizzle RGBA -> BGRA, which is the byte order a DIB expects
		const auto* source = static_cast<const unsigned char*>(image.data);
		auto* destination = reinterpret_cast<unsigned char*>(header + 1);
		for (std::size_t pixel = 0; pixel < pixelBytes; pixel += 4)
		{
			destination[pixel + 0] = source[pixel + 2];
			destination[pixel + 1] = source[pixel + 1];
			destination[pixel + 2] = source[pixel + 0];
			destination[pixel + 3] = source[pixel + 3];
		}
		GlobalUnlock(clipboardMemory);

		if (!OpenClipboard(nullptr))
		{
			GlobalFree(clipboardMemory);
			return false;
		}
		EmptyClipboard();
		// on success the clipboard takes ownership of clipboardMemory, so we only free it ourselves on failure
		const bool copied = SetClipboardData(CF_DIB, clipboardMemory) != nullptr;
		CloseClipboard();
		if (!copied) GlobalFree(clipboardMemory);
		return copied;
#else
		(void)image;
		return false;
#endif
	}
}

void FlappyGame::FlushScreenshotToasts()
{
	// move the queued messages out under the lock, then show them on the main thread (ShowToast isn't thread-safe)
	std::vector<std::string> messages;
	{
		std::lock_guard<std::mutex> lock(gScreenshotToastMutex);
		messages.swap(gScreenshotToasts);
	}
	for (const std::string& message : messages) ShowToast(message);
}

void FlappyGame::TakeScreenshot()
{
	storage.fileManager.createFolders(storage.screenshotDirectory);
	const int capturedScore = singlePlayer.alive ? singlePlayer.score : std::max(0, singlePlayer.savedScore);
	std::string shotDirectory = storage.screenshotDirectory;
	std::string playerName = storage.save.playerName;
	const int bestScore = storage.save.bestScore;

	Image image = {};
	if (interfaceState.current == GameState::PHOTO_MODE)
	{
		// photo mode: re-render the scene through the photo pan/zoom at window resolution, so the capture matches
		// exactly what's framed on screen
		RenderTexture2D photoCapture = LoadRenderTexture(std::max(1, GetScreenWidth()), std::max(1, GetScreenHeight()));
		BeginTextureMode(photoCapture);
		ClearBackground(BLACK);
		Rectangle destination = frame.destination;
		ApplyPhotoModeTransform(destination);
		Rectangle source = { 0.0f, 0.0f, static_cast<float>(screen.texture.width), -static_cast<float>(screen.texture.height) };
		DrawTexturePro(screen.texture, source, destination, Vector2{ 0.0f, 0.0f }, 0.0f, WHITE);
		EndTextureMode();

		image = LoadImageFromTexture(photoCapture.texture);
		UnloadRenderTexture(photoCapture);
	}
	else
	{
		image = LoadImageFromTexture(screen.texture);   // normal: just read back the game's render target
	}

	SaveScreenshotImageAsync(image, std::move(shotDirectory), std::move(playerName), capturedScore, bestScore);
	ShowToast("Screenshot queued");
}

void FlappyGame::ProcessPhotoModeInput()
{
	PhotoModeState& photo = interfaceState.photoMode;
	const int activationKey = storage.save.keyPhotoMode;

	// don't let the capture key fire while the player is typing their name
	if (interfaceState.mainMenu.editingName)
	{
		photo.screenshotHoldSeconds = 0.0f;
		return;
	}

	if (photo.suppressActivationUntilRelease)
	{
		// just entered photo mode on this key: ignore it until released, so the same press doesn't instantly screenshot
		if (!IsKeyDown(activationKey)) photo.suppressActivationUntilRelease = false;
		photo.screenshotHoldSeconds = 0.0f;
		return;
	}

	// a quick tap captures; holding past ActivationHoldSeconds (handled below) instead opens photo mode
	if (IsKeyDown(activationKey)) photo.screenshotHoldSeconds += deltaTime;
	if (IsKeyReleased(activationKey))
	{
		if (photo.screenshotHoldSeconds < Constants::PhotoMode::ActivationHoldSeconds) TakeScreenshot();
		photo.screenshotHoldSeconds = 0.0f;
	}

	if ((interfaceState.current == GameState::PLAYING || interfaceState.current == GameState::READY) &&
		photo.screenshotHoldSeconds >= Constants::PhotoMode::ActivationHoldSeconds)
	{
		photo.returnState = interfaceState.current;   // remember where to return when photo mode exits
		interfaceState.current = GameState::PHOTO_MODE;
		photo.offset = { 0.0f, 0.0f };
		photo.zoom = 1.0f;
	}

	if (interfaceState.current != GameState::PHOTO_MODE)
	{
		SetMouseCursor(MOUSE_CURSOR_DEFAULT);
		return;
	}
	SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);

	// keyboard + drag panning
	const float panStep = deltaTime * Constants::PhotoMode::KeyboardPanSpeed;
	if (IsKeyDown(KEY_LEFT))  photo.offset.x -= panStep;
	if (IsKeyDown(KEY_RIGHT)) photo.offset.x += panStep;
	if (IsKeyDown(KEY_UP))    photo.offset.y -= panStep;
	if (IsKeyDown(KEY_DOWN))  photo.offset.y += panStep;

	if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
	{
		const Vector2 mouseDelta = GetMouseDelta();
		photo.offset.x -= mouseDelta.x;
		photo.offset.y -= mouseDelta.y;
	}

	const float previousZoom = photo.zoom;
	if (IsKeyDown(KEY_EQUAL) || IsKeyDown(KEY_KP_ADD))
		photo.zoom += deltaTime * Constants::PhotoMode::KeyboardZoomSpeed;
	if (IsKeyDown(KEY_MINUS) || IsKeyDown(KEY_KP_SUBTRACT))
		photo.zoom -= deltaTime * Constants::PhotoMode::KeyboardZoomSpeed;
	photo.zoom += GetMouseWheelMove() * Constants::PhotoMode::MouseWheelZoomStep;
	photo.zoom = std::clamp(photo.zoom,
		Constants::PhotoMode::MinimumZoom, Constants::PhotoMode::MaximumZoom);
	if (photo.zoom != previousZoom)
	{
		// zoom toward the cursor: adjust the pan offset so the world point under the cursor stays put across the zoom
		const Vector2 cursor = GetMousePosition();
		const Vector2 center = {
			frame.destination.x + frame.destination.width * 0.5f,
			frame.destination.y + frame.destination.height * 0.5f
		};
		const float zoomRatio = photo.zoom / previousZoom;
		photo.offset.x = zoomRatio * (cursor.x - center.x + photo.offset.x) - (cursor.x - center.x);
		photo.offset.y = zoomRatio * (cursor.y - center.y + photo.offset.y) - (cursor.y - center.y);
	}

	// clamp the pan so the view can't slide more than half a (zoomed) viewport past center
	const float panLimitX = frame.destination.width * photo.zoom * Constants::PhotoMode::PanLimitViewportFraction;
	const float panLimitY = frame.destination.height * photo.zoom * Constants::PhotoMode::PanLimitViewportFraction;
	photo.offset.x = std::clamp(photo.offset.x, -panLimitX, panLimitX);
	photo.offset.y = std::clamp(photo.offset.y, -panLimitY, panLimitY);

	if (IsKeyPressed(KEY_ESCAPE))
	{
		interfaceState.current = photo.returnState;
		SetMouseCursor(MOUSE_CURSOR_DEFAULT);
	}
}

void FlappyGame::ApplyPhotoModeTransform(Rectangle& destination) const
{
	// scale the present rect about its own center by zoom, then shift it by the pan offset
	const PhotoModeState& photo = interfaceState.photoMode;
	const float centerX = destination.x + destination.width * 0.5f;
	const float centerY = destination.y + destination.height * 0.5f;
	destination.width *= photo.zoom;
	destination.height *= photo.zoom;
	destination.x = centerX - destination.width * 0.5f - photo.offset.x;
	destination.y = centerY - destination.height * 0.5f - photo.offset.y;
}

void FlappyGame::DrawPhotoModeToast() const
{
	if (interfaceState.toast.remainingSeconds <= 0.0f) return;

	// photo mode draws at native window resolution (not the virtual canvas), so size the toast from the window scale
	const float uiScale = std::max(0.75f,
		std::min(GetScreenWidth() / (float)VIRTUAL_W, GetScreenHeight() / (float)VIRTUAL_H));
	const int fontSize = std::max(16, (int)(22.0f * uiScale));
	const int width = MeasureText(interfaceState.toast.message.c_str(), fontSize);
	const int paddingX = std::max(10, (int)(14.0f * uiScale));
	const int height = std::max(30, (int)(34.0f * uiScale));
	const int x = GetScreenWidth() / 2 - width / 2;
	const int y = GetScreenHeight() - height - std::max(12, (int)(18.0f * uiScale));
	DrawRectangle(x - paddingX, y, width + paddingX * 2, height, Fade(BLACK, 0.78f));
	DrawText(interfaceState.toast.message.c_str(), x, y + (height - fontSize) / 2, fontSize, RAYWHITE);
}
