#ifndef GAME_CLIENT_COMPONENTS_CUSTOM_BACKGROUND_H
#define GAME_CLIENT_COMPONENTS_CUSTOM_BACKGROUND_H

#include <base/types.h>

#include <engine/graphics.h>
#include <engine/storage.h>

#include <chrono>
#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

/**
 * Custom media background ("My custom" settings page).
 *
 * Loads a user provided PNG image or video file (mp4, ...) and renders it
 * as a fullscreen, aspect-correct ("cover") background. It is purely a
 * local visual feature and does not touch any gameplay relevant state.
 */
class CCustomMediaBackground
{
public:
	enum class EStatus
	{
		DISABLED,
		NO_FILE,
		LOADED_IMAGE,
		LOADED_VIDEO,
		FAILED_IMAGE,
		FAILED_OPEN_VIDEO,
		FAILED_VIDEO_INFO,
		NO_VIDEO_STREAM,
		BAD_VIDEO_CODEC,
		FAILED_DECODER_INIT,
		INVALID_DIMENSIONS,
		FAILED_FRAME_ALLOC,
		FAILED_RGBA_ALLOC,
		FAILED_SCALER,
		FAILED_FIRST_FRAME,
		FAILED_DECODE,
		NO_VIDEO_SUPPORT,
	};

private:
	IGraphics *m_pGraphics = nullptr;
	IStorage *m_pStorage = nullptr;

	IGraphics::CTextureHandle m_Texture;
	// Dimensions of the currently allocated texture, so the video path can tell
	// whether an in-place update is possible or a new allocation is needed.
	int m_TextureWidth = 0;
	int m_TextureHeight = 0;
	// Texture size rounded up to powers of two. The OpenGL backend CPU-resizes
	// non-power-of-two textures on every update, which is what made large videos
	// unusable; the padding is never sampled (see Render()'s UV mapping).
	int m_UploadWidth = 0;
	int m_UploadHeight = 0;
	// m_Width/m_Height: real video dimensions after the fit-box downscale
	int m_Width = 0;
	int m_Height = 0;
	// m_SrcWidth/m_SrcHeight: codec (decode source) dimensions
	int m_SrcWidth = 0;
	int m_SrcHeight = 0;

	bool m_IsVideo = false;
	bool m_IsLoaded = false;
	bool m_HasError = false;
	EStatus m_Status = EStatus::DISABLED;
	char m_aLoadedPath[IO_MAX_PATH_LENGTH] = "";

	int m_LastConfigEnabled = -1;
	char m_aLastConfigPath[IO_MAX_PATH_LENGTH] = "";
	// last seen mc_background_video_res value (the video decode box follows it,
	// NOT mc_background_fit: the fit setting is only a composition/crop aspect
	// and must never downscale the source video)
	int m_LastVideoRes = -1;
	// UV rectangle of the texture that the last Render() mapped onto the whole
	// screen. The interface material samples this to show "what is behind" a panel.
	float m_DisplayU0 = 0.0f;
	float m_DisplayV0 = 0.0f;
	float m_DisplayU1 = 1.0f;
	float m_DisplayV1 = 1.0f;

	// video playback state
	AVFormatContext *m_pFormatCtx = nullptr;
	AVCodecContext *m_pCodecCtx = nullptr;
	AVFrame *m_pFrame = nullptr;
	AVFrame *m_pFrameRgba = nullptr;
	AVPacket *m_pPacket = nullptr;
	SwsContext *m_pSwsCtx = nullptr;
	int m_VideoStream = -1;
	int64_t m_LastVideoPts = 0;
	std::chrono::nanoseconds m_NextFrameTime{0};
	std::vector<uint8_t> m_vUploadBuffer;

	void SetStatus(EStatus Status);
	void ClearVideoState();
	bool LoadImage(const char *pPath, int StorageType);
	bool LoadVideo(const char *pPath, int StorageType);
	bool DecodeFirstFrameFromFile(const char *pAbsolutePath);
	bool DecodeNextVideoFrame(bool LoopOnEof);
	bool UploadCurrentVideoFrame(int DurationMs);
	void ReloadFromConfig(int Enabled, const char *pPath);

public:
	~CCustomMediaBackground();

	void Init(IGraphics *pGraphics, IStorage *pStorage);
	void SyncFromConfig(int Enabled, const char *pPath);
	// resolve mc_background_fit into a pixel box (auto = screen size, HiDPI aware)
	void ResolveFitBox(int &FitW, int &FitH) const;
	void Update();
	bool Render();
	void Unload();

	bool HasError() const { return m_HasError; }
	bool IsLoaded() const { return m_IsLoaded; }
	EStatus Status() const { return m_Status; }
	int SrcWidth() const { return m_SrcWidth; }
	int SrcHeight() const { return m_SrcHeight; }
	int DecodeWidth() const { return m_Width; }
	int DecodeHeight() const { return m_Height; }
	bool IsVideo() const { return m_IsVideo; }
	// texture currently holding the background, and the UV rect of it that maps
	// onto the whole screen (the sub-rect actually visible after fit/crop)
	IGraphics::CTextureHandle TextureHandle() const { return m_Texture; }
	void DisplayedUvRect(float &U0, float &V0, float &U1, float &V1) const;
	// translated at call time so it follows the current game language
	const char *StatusText() const;
};

#endif
