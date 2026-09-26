#include "custom_background.h"

#include <base/fs.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/shared/config.h>

#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <limits>

#if defined(CONF_VIDEORECORDER)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}
#endif

namespace
{
	constexpr int CUSTOM_BG_DEFAULT_VIDEO_FRAME_MS = 33;
	constexpr int CUSTOM_BG_MAX_VIDEO_FRAME_MS = 250;
	constexpr int64_t CUSTOM_BG_PTS_UNSET = std::numeric_limits<int64_t>::min();

	// Diagnostics window state (mc_background_diag 1 only). These help tell the
	// menu render rate apart from the decode rate: if updates_per_10s is high but
	// decodes_per_10s is low, the fps gate is the limiter and all good; if
	// updates_per_10s itself is tiny, the whole menu loop is being slowed down.
	int gs_DiagUpdates = 0;
	int gs_DiagGated = 0;
	double gs_DiagSwsMs = 0.0;
	double gs_DiagUpMs = 0.0;
	double gs_DiagDecodeWallMs = 0.0;
	double gs_DiagRenderMs = 0.0;
	int gs_DiagRenders = 0;

	void DiagCountUpdate() { ++gs_DiagUpdates; }
	void DiagCountGated() { ++gs_DiagGated; }
	void DiagAddDecodeWallMs(double Ms) { gs_DiagDecodeWallMs += Ms; }
	void DiagAddRenderMs(double Ms)
	{
		gs_DiagRenderMs += Ms;
		++gs_DiagRenders;
	}
	void DiagUpdateTotals(double SwsMs, double UpMs)
	{
		gs_DiagSwsMs += SwsMs;
		gs_DiagUpMs += UpMs;
	}
	int DiagUpdates() { return gs_DiagUpdates; }
	int DiagGated() { return gs_DiagGated; }
	double DiagDecodeWallMs() { return gs_DiagDecodeWallMs; }
	double DiagRenderMs() { return gs_DiagRenderMs; }
	int DiagRenders() { return gs_DiagRenders; }
	void DiagResetWindow()
	{
		gs_DiagUpdates = 0;
		gs_DiagGated = 0;
		gs_DiagSwsMs = 0.0;
		gs_DiagUpMs = 0.0;
		gs_DiagDecodeWallMs = 0.0;
		gs_DiagRenderMs = 0.0;
		gs_DiagRenders = 0;
	}

	// Round up to the next power of two (used to keep video textures off the
	// driver's non-power-of-two CPU-resize path).
	int PowerOfTwoCeil(int Value)
	{
		int Result = 1;
		while(Result < Value && Result < (1 << 20))
			Result <<= 1;
		return Result;
	}

	// Resolve a background path to an absolute filesystem path.
	//
	// IStorage::GetCompletePath() asserts Type >= TYPE_SAVE, so TYPE_ABSOLUTE
	// (-2) must never reach it. Wallpaper Engine entries are absolute paths,
	// which is exactly how that assert was hit in game (storage.cpp:876
	// "Type invalid"). Absolute inputs are therefore passed through untouched and
	// only relative ones are resolved against the save directory.
	void ResolveToAbsolute(IStorage *pStorage, const char *pPath, char *pOut, size_t OutSize)
	{
		if(pPath == nullptr || pPath[0] == '\0')
		{
			pOut[0] = '\0';
			return;
		}
		if(!fs_is_relative_path(pPath))
		{
			str_copy(pOut, pPath, OutSize);
			return;
		}
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, pPath, pOut, OutSize);
	}

	const char *ExtensionOf(const char *pPath)
	{
		const char *pDot = nullptr;
		for(const char *p = pPath; *p != '\0'; ++p)
		{
			if(*p == '.')
				pDot = p;
		}
		return pDot == nullptr ? "" : pDot + 1;
	}

	void ExtensionLower(const char *pPath, char *pOut, size_t OutSize)
	{
		size_t i = 0;
		const char *pExt = ExtensionOf(pPath);
		for(; *pExt != '\0' && i + 1 < OutSize; ++pExt, ++i)
			pOut[i] = (char)tolower((unsigned char)*pExt);
		pOut[i] = '\0';
	}

	// parse mc_background_fit: "auto"/"" -> false (caller falls back to the
	// screen), otherwise a "WxH" pixel size like "1920x1440"
	bool ParseFitPx(const char *pFit, int &FitW, int &FitH)
	{
		int W = 0;
		int H = 0;
		if(pFit == nullptr || pFit[0] == '\0' || str_comp(pFit, "auto") == 0)
			return false;
		if(sscanf(pFit, "%dx%d", &W, &H) != 2 || W <= 0 || H <= 0)
			return false;
		FitW = W;
		FitH = H;
		return true;
	}

	bool IsVideoExtension(const char *pExt)
	{
		static const char *apVideoExts[] = {
			"mp4", "mkv", "webm", "mov", "avi", "m4v", "mpg", "mpeg", "flv", "wmv", "ts", "3gp"};
		for(const char *pVideoExt : apVideoExts)
		{
			if(str_comp(pExt, pVideoExt) == 0)
				return true;
		}
		return false;
	}
}

void CCustomMediaBackground::SetStatus(EStatus Status)
{
	m_Status = Status;
	m_HasError = !(Status == EStatus::DISABLED || Status == EStatus::LOADED_IMAGE || Status == EStatus::LOADED_VIDEO);
}

const char *CCustomMediaBackground::StatusText() const
{
	switch(m_Status)
	{
	case EStatus::DISABLED: return Localize("Disabled.");
	case EStatus::NO_FILE: return Localize("No file selected.");
	case EStatus::LOADED_IMAGE: return Localize("Loaded image.");
	case EStatus::LOADED_VIDEO: return Localize("Loaded video.");
	case EStatus::FAILED_IMAGE: return Localize("Failed to load image.");
	case EStatus::FAILED_OPEN_VIDEO: return Localize("Failed to open video.");
	case EStatus::FAILED_VIDEO_INFO: return Localize("Failed to read video info.");
	case EStatus::NO_VIDEO_STREAM: return Localize("No video stream found.");
	case EStatus::BAD_VIDEO_CODEC: return Localize("Unsupported video codec.");
	case EStatus::FAILED_DECODER_INIT: return Localize("Failed to initialize video decoder.");
	case EStatus::INVALID_DIMENSIONS: return Localize("Invalid video dimensions.");
	case EStatus::FAILED_FRAME_ALLOC: return Localize("Failed to allocate video frames.");
	case EStatus::FAILED_RGBA_ALLOC: return Localize("Failed to allocate RGBA frame.");
	case EStatus::FAILED_SCALER: return Localize("Failed to initialize video scaler.");
	case EStatus::FAILED_FIRST_FRAME: return Localize("Failed to decode first video frame.");
	case EStatus::FAILED_DECODE: return Localize("Failed while decoding video.");
	case EStatus::NO_VIDEO_SUPPORT: return Localize("This build has no video support.");
	}
	return "";
}

void CCustomMediaBackground::ClearVideoState()
{
#if defined(CONF_VIDEORECORDER)
	if(m_pPacket != nullptr)
		av_packet_free(&m_pPacket);
	if(m_pFrameRgba != nullptr)
		av_frame_free(&m_pFrameRgba);
	if(m_pFrame != nullptr)
		av_frame_free(&m_pFrame);
	if(m_pSwsCtx != nullptr)
		sws_freeContext(m_pSwsCtx);
	if(m_pCodecCtx != nullptr)
		avcodec_free_context(&m_pCodecCtx);
	if(m_pFormatCtx != nullptr)
		avformat_close_input(&m_pFormatCtx);
#endif
	m_pFormatCtx = nullptr;
	m_pCodecCtx = nullptr;
	m_pFrame = nullptr;
	m_pFrameRgba = nullptr;
	m_pPacket = nullptr;
	m_pSwsCtx = nullptr;
	m_VideoStream = -1;
	m_LastVideoPts = CUSTOM_BG_PTS_UNSET;
	m_NextFrameTime = std::chrono::nanoseconds::zero();
	m_vUploadBuffer.clear();
}

void CCustomMediaBackground::DisplayedUvRect(float &U0, float &V0, float &U1, float &V1) const
{
	U0 = m_DisplayU0;
	V0 = m_DisplayV0;
	U1 = m_DisplayU1;
	V1 = m_DisplayV1;
}

void CCustomMediaBackground::Unload()
{
	if(m_pGraphics != nullptr && m_Texture.IsValid())
		m_pGraphics->UnloadTexture(&m_Texture);
	ClearVideoState();

	m_Width = 0;
	m_Height = 0;
	m_TextureWidth = 0;
	m_TextureHeight = 0;
	m_UploadWidth = 0;
	m_UploadHeight = 0;
	m_SrcWidth = 0;
	m_SrcHeight = 0;
	m_IsVideo = false;
	m_IsLoaded = false;
	m_aLoadedPath[0] = '\0';
	m_LastConfigEnabled = -1;
	m_aLastConfigPath[0] = '\0';
	// note: m_LastVideoRes must NOT be reset here. It caches
	// g_Config.m_McBackgroundVideoRes for SyncFromConfig; clearing it inside the
	// reload it triggered made every frame look like a settings change (reload
	// loop, video frozen on a black first frame — the same class of bug that
	// m_aLastFit used to cause).
	SetStatus(EStatus::DISABLED);
}

void CCustomMediaBackground::ResolveFitBox(int &FitW, int &FitH) const
{
	// manual WxH: background-only resolution, the game resolution stays untouched
	if(ParseFitPx(g_Config.m_McBackgroundFit, FitW, FitH))
		return;
	// auto: detect the current screen parameters (HiDPI aware)
	FitW = 0;
	FitH = 0;
	if(m_pGraphics != nullptr && g_Config.m_GfxScreenWidth > 0 && g_Config.m_GfxScreenHeight > 0)
	{
		const float Scale = m_pGraphics->ScreenHiDPIScale();
		FitW = (int)((float)g_Config.m_GfxScreenWidth * Scale + 0.5f);
		FitH = (int)((float)g_Config.m_GfxScreenHeight * Scale + 0.5f);
	}
	if(FitW <= 0)
		FitW = 1920;
	if(FitH <= 0)
		FitH = 1080;
}

CCustomMediaBackground::~CCustomMediaBackground()
{
	Unload();
}

void CCustomMediaBackground::Init(IGraphics *pGraphics, IStorage *pStorage)
{
	m_pGraphics = pGraphics;
	m_pStorage = pStorage;
	SetStatus(EStatus::DISABLED);
}

bool CCustomMediaBackground::DecodeFirstFrameFromFile(const char *pAbsolutePath)
{
#if !defined(CONF_VIDEORECORDER)
	(void)pAbsolutePath;
	return false;
#else
	if(pAbsolutePath == nullptr || pAbsolutePath[0] == '\0')
		return false;

	AVFormatContext *pFormatCtx = nullptr;
	AVCodecContext *pCodecCtx = nullptr;
	SwsContext *pSwsCtx = nullptr;
	AVPacket *pPacket = nullptr;
	AVFrame *pFrame = nullptr;
	AVFrame *pFrameRgba = nullptr;
	int VideoStream = -1;
	bool Success = false;
	int SrcW = 0;
	int SrcH = 0;
	size_t FrameBytes = 0;

	auto CopyFrame = [&]() -> bool {
		if(pFrame == nullptr || pFrameRgba == nullptr || pSwsCtx == nullptr || SrcW <= 0 || SrcH <= 0 || FrameBytes == 0)
			return false;
		if(av_frame_make_writable(pFrameRgba) < 0)
			return false;
		const int ScaledLines = sws_scale(pSwsCtx, pFrame->data, pFrame->linesize, 0, SrcH, pFrameRgba->data, pFrameRgba->linesize);
		if(ScaledLines <= 0 || pFrameRgba->linesize[0] <= 0 || (size_t)pFrameRgba->linesize[0] < (size_t)SrcW * 4ull)
			return false;

		CImageInfo Image;
		Image.m_Width = SrcW;
		Image.m_Height = SrcH;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.Allocate();
		for(int y = 0; y < SrcH; ++y)
		{
			mem_copy(
				Image.m_pData + (size_t)y * (size_t)SrcW * 4ull,
				pFrameRgba->data[0] + (size_t)y * (size_t)pFrameRgba->linesize[0],
				(size_t)SrcW * 4ull);
		}

		if(m_Texture.IsValid())
			m_pGraphics->UnloadTexture(&m_Texture);
		m_Texture = m_pGraphics->LoadTextureRawMove(Image, 0, pAbsolutePath);
		if(!m_Texture.IsValid())
			return false;
		m_Width = SrcW;
		m_Height = SrcH;
		return true;
	};

	do
	{
		if(avformat_open_input(&pFormatCtx, pAbsolutePath, nullptr, nullptr) != 0)
			break;
		if(avformat_find_stream_info(pFormatCtx, nullptr) < 0)
			break;

		VideoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if(VideoStream < 0)
			break;

		const AVStream *pStream = pFormatCtx->streams[VideoStream];
		const AVCodec *pCodec = avcodec_find_decoder(pStream->codecpar->codec_id);
		if(pCodec == nullptr)
			break;

		pCodecCtx = avcodec_alloc_context3(pCodec);
		if(pCodecCtx == nullptr || avcodec_parameters_to_context(pCodecCtx, pStream->codecpar) < 0 || avcodec_open2(pCodecCtx, pCodec, nullptr) < 0)
			break;

		SrcW = pCodecCtx->width;
		SrcH = pCodecCtx->height;
		if(SrcW <= 0 || SrcH <= 0)
			break;
		if((size_t)SrcW > std::numeric_limits<size_t>::max() / ((size_t)SrcH * 4ull))
			break;
		FrameBytes = (size_t)SrcW * (size_t)SrcH * 4ull;
		if(FrameBytes == 0)
			break;

		pSwsCtx = sws_getContext(SrcW, SrcH, pCodecCtx->pix_fmt, SrcW, SrcH, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
		if(pSwsCtx == nullptr)
			break;

		pPacket = av_packet_alloc();
		pFrame = av_frame_alloc();
		pFrameRgba = av_frame_alloc();
		if(pPacket == nullptr || pFrame == nullptr || pFrameRgba == nullptr)
			break;

		pFrameRgba->format = AV_PIX_FMT_RGBA;
		pFrameRgba->width = SrcW;
		pFrameRgba->height = SrcH;
		if(av_frame_get_buffer(pFrameRgba, 1) < 0)
			break;

		while(av_read_frame(pFormatCtx, pPacket) >= 0)
		{
			if(pPacket->stream_index == VideoStream)
			{
				if(avcodec_send_packet(pCodecCtx, pPacket) < 0)
				{
					av_packet_unref(pPacket);
					break;
				}
				while(avcodec_receive_frame(pCodecCtx, pFrame) == 0)
				{
					if(CopyFrame())
					{
						Success = true;
						break;
					}
				}
			}
			av_packet_unref(pPacket);
			if(Success)
				break;
		}

		if(!Success && avcodec_send_packet(pCodecCtx, nullptr) >= 0)
		{
			while(avcodec_receive_frame(pCodecCtx, pFrame) == 0)
			{
				if(CopyFrame())
				{
					Success = true;
					break;
				}
			}
		}
	} while(false);

	if(pFrameRgba != nullptr)
		av_frame_free(&pFrameRgba);
	if(pFrame != nullptr)
		av_frame_free(&pFrame);
	if(pPacket != nullptr)
		av_packet_free(&pPacket);
	if(pSwsCtx != nullptr)
		sws_freeContext(pSwsCtx);
	if(pCodecCtx != nullptr)
		avcodec_free_context(&pCodecCtx);
	if(pFormatCtx != nullptr)
		avformat_close_input(&pFormatCtx);

	return Success;
#endif
}

bool CCustomMediaBackground::LoadImage(const char *pPath, int StorageType)
{
	m_IsVideo = false;

	char aExt[16];
	ExtensionLower(pPath, aExt, sizeof(aExt));

	// plain PNG files go through the engine's own PNG loader
	if(str_comp(aExt, "png") == 0)
	{
		CImageInfo Image;
		if(m_pGraphics->LoadPng(Image, pPath, StorageType))
		{
			const int Width = (int)Image.m_Width;
			const int Height = (int)Image.m_Height;
			IGraphics::CTextureHandle Texture = m_pGraphics->LoadTextureRawMove(Image, 0, pPath);
			if(Texture.IsValid())
			{
				if(m_Texture.IsValid())
					m_pGraphics->UnloadTexture(&m_Texture);
				m_Texture = Texture;
				m_Width = Width;
				m_Height = Height;
				m_IsLoaded = true;
				SetStatus(EStatus::LOADED_IMAGE);
				return true;
			}
		}
	}

#if defined(CONF_VIDEORECORDER)
	// Resolve through the helper: absolute paths (Wallpaper Engine wallpapers)
	// bypass GetCompletePath, which would assert on TYPE_ABSOLUTE.
	char aAbsolutePath[IO_MAX_PATH_LENGTH];
	ResolveToAbsolute(m_pStorage, pPath, aAbsolutePath, sizeof(aAbsolutePath));
	if(DecodeFirstFrameFromFile(aAbsolutePath))
	{
		m_IsLoaded = true;
		SetStatus(EStatus::LOADED_IMAGE);
		return true;
	}
#endif

	SetStatus(EStatus::FAILED_IMAGE);
	return false;
}

bool CCustomMediaBackground::UploadCurrentVideoFrame(int DurationMs)
{
#if !defined(CONF_VIDEORECORDER)
	(void)DurationMs;
	return false;
#else
	if(m_pGraphics == nullptr || m_pFrame == nullptr || m_pFrameRgba == nullptr || m_pSwsCtx == nullptr ||
		m_Width <= 0 || m_Height <= 0 || m_UploadWidth <= 0 || m_UploadHeight <= 0)
		return false;

	if(av_frame_make_writable(m_pFrameRgba) < 0)
		return false;

	const auto SwsT0 = std::chrono::steady_clock::now();
	// The scaler writes the video into the top-left m_Width x m_Height rectangle
	// of the padded frame; the border is padding that Render() never samples.
	sws_scale(m_pSwsCtx, m_pFrame->data, m_pFrame->linesize, 0, m_SrcHeight, m_pFrameRgba->data, m_pFrameRgba->linesize);
	const double SwsMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - SwsT0).count();

	// Upload the whole padded texture (a power-of-two size on both axes), which
	// keeps the driver off its non-power-of-two CPU-resize path. The frame is
	// allocated at exactly this size, so no per-frame row copy is needed.
	CImageInfo Image;
	Image.m_Width = m_UploadWidth;
	Image.m_Height = m_UploadHeight;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = m_pFrameRgba->data[0];

	// Upload the frame.
	//
	// Three things matter here, all measured with a 2K video background:
	//
	// 1) POWER-OF-TWO SIZE. The OpenGL backend CPU-resizes non-power-of-two
	//    textures on every update (TextureUpdate -> ResizeImage when
	//    !m_HasNPOTTextures). At 2560x1440 that meant resampling to 4096x2048
	//    (~33 MB) per frame, which is what actually made the menu collapse to
	//    ~4 fps. The texture is therefore allocated at m_UploadWidth x
	//    m_UploadHeight (powers of two) and Render() maps UVs to the real video
	//    rectangle, so the padding is never sampled.
	// 2) MIPMAPS. Flags=0 leaves GL_GENERATE_MIPMAP on, regenerating the whole
	//    chain on every write; TEXLOAD_NO_MIPMAPS disables that. A background is
	//    drawn 1:1 or minified by the quad, so mipmaps are not needed.
	// 3) REUSE. The texture is allocated once and then refreshed in place through
	//    IGraphics::UpdateTextureRgba() (glTexSubImage2D) instead of being
	//    destroyed and re-created every frame.
	const auto UpT0 = std::chrono::steady_clock::now();
	bool UploadOk;
	if(m_Texture.IsValid() && m_TextureWidth == m_UploadWidth && m_TextureHeight == m_UploadHeight)
	{
		UploadOk = m_pGraphics->UpdateTextureRgba(m_Texture, 0, 0, (size_t)m_UploadWidth, (size_t)m_UploadHeight, Image.m_pData);
	}
	else
	{
		// first frame (or the decode box changed): allocate the texture once
		IGraphics::CTextureHandle Texture = m_pGraphics->LoadTextureRaw(Image, IGraphics::TEXLOAD_NO_MIPMAPS, m_aLoadedPath);
		UploadOk = Texture.IsValid();
		if(UploadOk)
		{
			if(m_Texture.IsValid())
				m_pGraphics->UnloadTexture(&m_Texture);
			m_Texture = Texture;
			m_TextureWidth = m_UploadWidth;
			m_TextureHeight = m_UploadHeight;
		}
	}
	const double UpMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - UpT0).count();
	Image.m_pData = nullptr; // owned by m_pFrameRgba
	if(!UploadOk)
		return false;

	m_LastVideoPts = m_pFrame->best_effort_timestamp;
	// optional frame rate cap (mc_background_video_fps): only ever slows the video down,
	// never speeds it up, and reduces decode + texture upload cost
	int EffectiveMs = DurationMs;
	if(const int Fps = g_Config.m_McBackgroundVideoFps; Fps > 0)
		EffectiveMs = std::max(EffectiveMs, (1000 + Fps - 1) / Fps);
	m_NextFrameTime = time_get_nanoseconds() + std::chrono::milliseconds(std::clamp(EffectiveMs, 1, CUSTOM_BG_MAX_VIDEO_FRAME_MS));
	// Opt-in diagnostics (mc_background_diag 1): append pipeline counters every
	// ~10s to <save>/mcbg_diag.txt. The decode rate proves whether the fps gate
	// (m_NextFrameTime) actually throttles, and reload lines expose load loops.
	// Default 0 => never writes anything (no standing instrumentation).
	if(g_Config.m_McBackgroundDiag)
	{
		static int64_t s_DiagWindowStart = 0;
		static int s_DiagDecodes = 0;
		static int s_DiagSumEffective = 0;
		static double s_DiagSumSws = 0.0;
		static double s_DiagSumUp = 0.0;
		++s_DiagDecodes;
		s_DiagSumEffective += EffectiveMs;
		s_DiagSumSws += SwsMs;
		s_DiagSumUp += UpMs;
		DiagUpdateTotals(SwsMs, UpMs);
		const int64_t NowT = time_get();
		if(s_DiagWindowStart == 0)
			s_DiagWindowStart = NowT;
		if(NowT - s_DiagWindowStart >= 10 * time_freq())
		{
			char aDiagPath[IO_MAX_PATH_LENGTH];
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "mcbg_diag.txt", aDiagPath, sizeof(aDiagPath));
			if(FILE *pFile = fopen(aDiagPath, "a"))
			{
				const auto NextGapMs = std::chrono::duration_cast<std::chrono::milliseconds>(m_NextFrameTime - std::chrono::nanoseconds(time_get())).count();
				fprintf(pFile, "updates_per_10s=%d gated_per_10s=%d decodes_per_10s=%d avg_sws_ms=%.2f avg_upload_ms=%.1f decode_wall_ms=%.0f renders=%d render_ms=%.0f avg_effective_ms=%.1f cap_fps=%d threads=%d next_gap_ms=%lld\n",
					DiagUpdates(), DiagGated(), s_DiagDecodes,
					s_DiagDecodes > 0 ? s_DiagSumSws / s_DiagDecodes : 0.0,
					s_DiagDecodes > 0 ? s_DiagSumUp / s_DiagDecodes : 0.0,
					DiagDecodeWallMs(), DiagRenders(), DiagRenderMs(),
					s_DiagDecodes > 0 ? (double)s_DiagSumEffective / s_DiagDecodes : 0.0,
					g_Config.m_McBackgroundVideoFps, g_Config.m_McBackgroundThreads, (long long)NextGapMs);
				fclose(pFile);
			}
			s_DiagWindowStart = NowT;
			s_DiagDecodes = 0;
			s_DiagSumEffective = 0;
			s_DiagSumSws = 0.0;
			s_DiagSumUp = 0.0;
			DiagResetWindow();
		}
	}
	return true;
#endif
}

bool CCustomMediaBackground::DecodeNextVideoFrame(bool LoopOnEof)
{
#if !defined(CONF_VIDEORECORDER)
	(void)LoopOnEof;
	return false;
#else
	if(m_pFormatCtx == nullptr || m_pCodecCtx == nullptr || m_pPacket == nullptr || m_pFrame == nullptr)
		return false;

	while(true)
	{
		while(avcodec_receive_frame(m_pCodecCtx, m_pFrame) == 0)
		{
			int DurationMs = CUSTOM_BG_DEFAULT_VIDEO_FRAME_MS;
			if(m_LastVideoPts != CUSTOM_BG_PTS_UNSET && m_pFrame->best_effort_timestamp != AV_NOPTS_VALUE)
			{
				const int64_t DurationTs = m_pFrame->best_effort_timestamp - m_LastVideoPts;
				if(DurationTs > 0)
				{
					const int64_t Rescaled = av_rescale_q(DurationTs, m_pFormatCtx->streams[m_VideoStream]->time_base, AVRational{1, 1000});
					if(Rescaled > 0)
						DurationMs = (int)Rescaled;
				}
			}
			return UploadCurrentVideoFrame(DurationMs);
		}

		const int ReadResult = av_read_frame(m_pFormatCtx, m_pPacket);
		if(ReadResult < 0)
		{
			if(!LoopOnEof)
				return false;

			av_seek_frame(m_pFormatCtx, m_VideoStream, 0, AVSEEK_FLAG_BACKWARD);
			avcodec_flush_buffers(m_pCodecCtx);
			m_LastVideoPts = CUSTOM_BG_PTS_UNSET;
			continue;
		}

		if(m_pPacket->stream_index != m_VideoStream)
		{
			av_packet_unref(m_pPacket);
			continue;
		}

		const int SendResult = avcodec_send_packet(m_pCodecCtx, m_pPacket);
		av_packet_unref(m_pPacket);
		if(SendResult < 0)
			return false;
	}
#endif
}

bool CCustomMediaBackground::LoadVideo(const char *pPath, int StorageType)
{
	m_IsVideo = true;

#if !defined(CONF_VIDEORECORDER)
	(void)pPath;
	(void)StorageType;
	SetStatus(EStatus::NO_VIDEO_SUPPORT);
	return false;
#else
	char aAbsolutePath[IO_MAX_PATH_LENGTH];
	ResolveToAbsolute(m_pStorage, pPath, aAbsolutePath, sizeof(aAbsolutePath));

	const int OpenRes = avformat_open_input(&m_pFormatCtx, aAbsolutePath, nullptr, nullptr);
	if(OpenRes != 0)
	{
		SetStatus(EStatus::FAILED_OPEN_VIDEO);
		return false;
	}
	if(avformat_find_stream_info(m_pFormatCtx, nullptr) < 0)
	{
		SetStatus(EStatus::FAILED_VIDEO_INFO);
		return false;
	}

	m_VideoStream = av_find_best_stream(m_pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if(m_VideoStream < 0)
	{
		SetStatus(EStatus::NO_VIDEO_STREAM);
		return false;
	}

	const AVStream *pStream = m_pFormatCtx->streams[m_VideoStream];
	const AVCodec *pCodec = avcodec_find_decoder(pStream->codecpar->codec_id);
	if(pCodec == nullptr)
	{
		SetStatus(EStatus::BAD_VIDEO_CODEC);
		ClearVideoState();
		return false;
	}

	m_pCodecCtx = avcodec_alloc_context3(pCodec);
	if(m_pCodecCtx == nullptr || avcodec_parameters_to_context(m_pCodecCtx, pStream->codecpar) < 0)
	{
		SetStatus(EStatus::FAILED_DECODER_INIT);
		ClearVideoState();
		return false;
	}
	// Perf (checklist #17): FFmpeg opens decoders with ONE thread by default, so a
	// 2K source stalled the render thread for the whole decode every frame.
	// ffbench: 2560x1440 h264 = 14.7 ms/frame single-threaded -> 0.7 ms/frame with
	// threads. In-game pacing measurement (mc_background_diag) showed 4 threads
	// beats auto/16 and 1 thread on wall time per decode cycle at 2K, so the
	// default is 4 (mc_background_threads, 0 = auto). Decode-only change; the game
	// resolution is never involved.
	// Note: low-res decoding (AVCodecContext.lowres) is NOT an option here —
	// FFmpeg 8.1's H.264 decoder reports "maximum value for lowres ... is 0".
	m_pCodecCtx->thread_count = g_Config.m_McBackgroundThreads;
	if(avcodec_open2(m_pCodecCtx, pCodec, nullptr) < 0)
	{
		SetStatus(EStatus::FAILED_DECODER_INIT);
		ClearVideoState();
		return false;
	}

	m_SrcWidth = m_pCodecCtx->width;
	m_SrcHeight = m_pCodecCtx->height;
	if(m_SrcWidth <= 0 || m_SrcHeight <= 0)
	{
		SetStatus(EStatus::INVALID_DIMENSIONS);
		ClearVideoState();
		return false;
	}

	// Video decode size: mc_background_video_res, and ONLY that setting.
	//
	// This used to be derived from mc_background_fit (the background fit box),
	// which meant picking "screen size" silently downscaled the source: a
	// 3000x2000 clip was decoded at 2046x1364 on a 2048x1152 screen, and a 4K
	// clip was crushed to the screen size too. The fit setting is a
	// composition/crop aspect and must not touch the decoded resolution, so the
	// two are now separate:
	//   mc_background_video_res = 0 (default) -> decode at the source size
	//                           1..4           -> cap the height (2160/1440/1080/720)
	// Capping is purely an opt-in performance trade-off, never a screen effect.
	static constexpr int s_apVideoResHeights[] = {0, 2160, 1440, 1080, 720};
	const int VideoResIndex = std::clamp(g_Config.m_McBackgroundVideoRes, 0, 4);
	const int CapHeight = s_apVideoResHeights[VideoResIndex];
	m_Width = m_SrcWidth;
	m_Height = m_SrcHeight;
	if(CapHeight > 0 && m_SrcHeight > CapHeight)
	{
		m_Height = CapHeight;
		m_Width = std::max(1, (int)((int64_t)m_SrcWidth * CapHeight / m_SrcHeight));
	}

	// Round the upload size up to a power of two on each axis.
	//
	// This is the difference between a smooth and an unusable video background.
	// The OpenGL backend keeps non-power-of-two textures on the CPU-resize path
	// (see CCommandProcessorFragment_OpenGL::TextureUpdate: when
	// !m_HasNPOTTextures it calls ResizeImage on every update), so a 2560x1440
	// frame was resampled to 4096x2048 - about 33 MB of work - on every single
	// frame. Measured menu frame rate with the same code and clip sizes:
	//   1024x512  (power of two) -> ~388 fps
	//   1024x1024 (power of two) -> ~377 fps
	//   2048x1024 (power of two) -> ~345 fps
	//    854x480  (not)          -> ~203 fps
	//   1280x720  (not)          ->  ~16 fps
	//   2560x1440 (not)          ->   ~4 fps
	// Sizing the texture to powers of two avoids that path entirely. The video is
	// decoded into the padded area and the extra border is never sampled, because
	// Render() maps UVs using the real video rectangle.
	m_UploadWidth = PowerOfTwoCeil(m_Width);
	m_UploadHeight = PowerOfTwoCeil(m_Height);

	m_pFrame = av_frame_alloc();
	m_pFrameRgba = av_frame_alloc();
	m_pPacket = av_packet_alloc();
	if(m_pFrame == nullptr || m_pFrameRgba == nullptr || m_pPacket == nullptr)
	{
		SetStatus(EStatus::FAILED_FRAME_ALLOC);
		ClearVideoState();
		return false;
	}

	m_pFrameRgba->format = AV_PIX_FMT_RGBA;
	m_pFrameRgba->width = m_UploadWidth;
	m_pFrameRgba->height = m_UploadHeight;
	if(av_frame_get_buffer(m_pFrameRgba, 1) < 0)
	{
		SetStatus(EStatus::FAILED_RGBA_ALLOC);
		ClearVideoState();
		return false;
	}
	// Clear the padding so the never-sampled border cannot show stale pixels.
	memset(m_pFrameRgba->data[0], 0, (size_t)m_pFrameRgba->linesize[0] * (size_t)m_UploadHeight);

	// Scale the source straight into the padded RGBA frame: the video occupies
	// the top-left m_Width x m_Height rectangle, the rest is padding.
	m_pSwsCtx = sws_getContext(m_SrcWidth, m_SrcHeight, m_pCodecCtx->pix_fmt, m_Width, m_Height, AV_PIX_FMT_RGBA,
		SWS_BILINEAR, nullptr, nullptr, nullptr);
	if(m_pSwsCtx == nullptr)
	{
		SetStatus(EStatus::FAILED_SCALER);
		ClearVideoState();
		return false;
	}

	if(!DecodeNextVideoFrame(true))
	{
		SetStatus(EStatus::FAILED_FIRST_FRAME);
		ClearVideoState();
		return false;
	}

	m_IsLoaded = true;
	SetStatus(EStatus::LOADED_VIDEO);
	return true;
#endif
}

void CCustomMediaBackground::ReloadFromConfig(int Enabled, const char *pPath)
{
	char aPath[IO_MAX_PATH_LENGTH];
	str_copy(aPath, pPath, sizeof(aPath));

	Unload();

	m_LastConfigEnabled = Enabled;
	str_copy(m_aLastConfigPath, aPath, sizeof(m_aLastConfigPath));

	if(!Enabled)
	{
		SetStatus(EStatus::DISABLED);
		return;
	}
	if(aPath[0] == '\0')
	{
		SetStatus(EStatus::NO_FILE);
		return;
	}

	const int StorageType = fs_is_relative_path(aPath) ? IStorage::TYPE_SAVE : IStorage::TYPE_ABSOLUTE;
	char aExt[16];
	ExtensionLower(aPath, aExt, sizeof(aExt));

	bool Success = false;
	if(IsVideoExtension(aExt))
		Success = LoadVideo(aPath, StorageType);
	else
		Success = LoadImage(aPath, StorageType);

	if(Success)
		str_copy(m_aLoadedPath, aPath, sizeof(m_aLoadedPath));

	if(g_Config.m_McBackgroundDiag)
	{
		char aDiagPath[IO_MAX_PATH_LENGTH];
		m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "mcbg_diag.txt", aDiagPath, sizeof(aDiagPath));
		if(FILE *pFile = fopen(aDiagPath, "a"))
		{
			fprintf(pFile, "reload enabled=%d success=%d path=%s status=%d src=%dx%d decode=%dx%d upload=%dx%d video_res=%d fit=%s\n",
				Enabled, Success ? 1 : 0, aPath, (int)Status(),
				m_SrcWidth, m_SrcHeight, m_Width, m_Height, m_UploadWidth, m_UploadHeight,
				g_Config.m_McBackgroundVideoRes, g_Config.m_McBackgroundFit);
			fclose(pFile);
		}
	}
}

void CCustomMediaBackground::SyncFromConfig(int Enabled, const char *pPath)
{
	// Only the decode-resolution setting affects the video decode box. A change
	// of mc_background_fit must NOT reload the video: that setting is a
	// composition/crop aspect only, so reloading would needlessly restart
	// playback whenever the user picks a different fit.
	const bool ResChanged = m_LastVideoRes != g_Config.m_McBackgroundVideoRes;
	if(ResChanged)
		m_LastVideoRes = g_Config.m_McBackgroundVideoRes;
	if(m_LastConfigEnabled != Enabled || str_comp(m_aLastConfigPath, pPath) != 0)
		ReloadFromConfig(Enabled, pPath);
	else if(ResChanged && m_IsVideo && m_IsLoaded)
		ReloadFromConfig(Enabled, pPath); // re-resolve the video decode box
}

void CCustomMediaBackground::Update()
{
	// diag: count every Update() call (menu render rate) and every call that
	// actually passes the fps gate, independent of the per-decode counters
	if(g_Config.m_McBackgroundDiag)
		DiagCountUpdate();

	if(!m_IsLoaded || !m_IsVideo)
		return;

	const auto Now = time_get_nanoseconds();
	if(Now < m_NextFrameTime)
		return;

	if(g_Config.m_McBackgroundDiag)
		DiagCountGated();

	int Guard = 0;
	while(Now >= m_NextFrameTime && Guard < 2)
	{
		const auto DecT0 = std::chrono::steady_clock::now();
		const bool Decoded = DecodeNextVideoFrame(true);
		if(g_Config.m_McBackgroundDiag)
			DiagAddDecodeWallMs(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - DecT0).count());
		if(!Decoded)
		{
			SetStatus(EStatus::FAILED_DECODE);
			m_IsLoaded = false;
			ClearVideoState();
			if(m_pGraphics != nullptr && m_Texture.IsValid())
				m_pGraphics->UnloadTexture(&m_Texture);
			break;
		}
		++Guard;
	}
	if(Guard == 2 && Now >= m_NextFrameTime)
	{
		// still behind after the 2-frame burst: pause one frame interval honouring
		// the fps cap, so mc_background_video_fps really bounds decode cost (the
		// fixed 33 ms jump used to exceed the cap and re-spike the CPU)
		int JumpMs = CUSTOM_BG_DEFAULT_VIDEO_FRAME_MS;
		if(const int Fps = g_Config.m_McBackgroundVideoFps; Fps > 0)
			JumpMs = std::max(JumpMs, (1000 + Fps - 1) / Fps);
		m_NextFrameTime = Now + std::chrono::milliseconds(JumpMs);
	}
}

bool CCustomMediaBackground::Render()
{
	const auto RenderT0 = std::chrono::steady_clock::now();
	if(!m_IsLoaded || m_pGraphics == nullptr || !m_Texture.IsValid())
		return false;

	const CScreenRect Screen = m_pGraphics->GetScreen();
	const float ScreenW = Screen.Width();
	const float ScreenH = Screen.Height();
	if(ScreenW <= 0.0f || ScreenH <= 0.0f || m_Width <= 0 || m_Height <= 0)
		return false;

	// Draw mode (mc_background_mode). The texture may be larger than the video
	// (power-of-two padding), so UVs start out covering only the real video
	// rectangle and the padding is never sampled.
	const float UsedU = m_UploadWidth > 0 ? (float)m_Width / (float)m_UploadWidth : 1.0f;
	const float UsedV = m_UploadHeight > 0 ? (float)m_Height / (float)m_UploadHeight : 1.0f;

	// Fit box (mc_background_fit): "auto" = screen aspect, or a fixed WxH. This
	// defines the aspect the background is composed in; the game resolution is
	// never changed by it.
	float FitAspect = ScreenW / ScreenH;
	int FitW = 0;
	int FitH = 0;
	if(ParseFitPx(g_Config.m_McBackgroundFit, FitW, FitH))
		FitAspect = (float)FitW / (float)FitH;

	const int Mode = g_Config.m_McBackgroundMode;

	// ---- how the image meets the screen -------------------------------------
	//
	// No mode below may ever leave uncovered (letterbox) area: the screen is
	// always fully covered by the background. That constraint decides the crop
	// direction, because for a given video and frame only ONE direction can fill
	// the screen without bars:
	//   video wider than the frame  -> the sides overflow   -> crop left/right
	//   video narrower than the frame -> top/bottom overflow -> crop top/bottom
	// So the two crop modes are the user's *intent* ("do not lose the sides" vs
	// "do not lose the top/bottom"); when that intent is geometrically impossible
	// the other direction is used, because honouring it would produce exactly the
	// bars this feature forbids. The settings UI shows which one is in effect.
	//
	// The crop is always centred, so the loss is equal on both sides.
	//
	// 0 = crop top/bottom (keep the whole width, drop the vertical overflow)
	// 1 = crop left/right (keep the whole height, drop the horizontal overflow)
	// 2 = stretch (fill the screen, aspect distortion allowed)
	// 3 = tile (repeat the image, filling the screen)
	if(Mode == 3) // tile: repeat the image at its natural size
	{
		// the material samples "what is behind" a panel; for tiling that is the
		// whole video image repeated, so expose the single-tile rect
		m_DisplayU0 = 0.0f;
		m_DisplayV0 = 0.0f;
		m_DisplayU1 = UsedU;
		m_DisplayV1 = UsedV;
		const float TileW = (float)m_Width;
		const float TileH = (float)m_Height;
		m_pGraphics->BlendNormal();
		m_pGraphics->TextureSet(m_Texture);
		m_pGraphics->WrapRepeat(); // required so the texture can repeat
		m_pGraphics->QuadsBegin();
		m_pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		// UV 0..1 maps the padded texture; scale it so the repeat period equals
		// exactly one video image (i.e. UsedU/UsedV per tile).
		const int CountX = (int)(ScreenW / TileW) + 2;
		const int CountY = (int)(ScreenH / TileH) + 2;
		for(int y = 0; y < CountY; ++y)
		{
			for(int x = 0; x < CountX; ++x)
			{
				const float X = Screen.m_TopLeft.x + (float)x * TileW - std::fmod(Screen.m_TopLeft.x, TileW);
				const float Y = Screen.m_TopLeft.y + (float)y * TileH - std::fmod(Screen.m_TopLeft.y, TileH);
				if(X >= Screen.m_TopLeft.x + ScreenW || Y >= Screen.m_TopLeft.y + ScreenH)
					continue;
				m_pGraphics->QuadsSetSubset(0.0f, 0.0f, UsedU, UsedV);
				const IGraphics::CQuadItem Quad(X, Y, TileW, TileH);
				m_pGraphics->QuadsDrawTL(&Quad, 1);
			}
		}
		m_pGraphics->QuadsEnd();
		m_pGraphics->WrapNormal();
		m_pGraphics->TextureClear();
		if(g_Config.m_McBackgroundDiag)
			DiagAddRenderMs(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - RenderT0).count());
		return true;
	}

	float DstX = Screen.m_TopLeft.x;
	float DstY = Screen.m_TopLeft.y;
	float DstW = ScreenW;
	float DstH = ScreenH;
	float TopLeftU = 0.0f;
	float TopLeftV = 0.0f;
	float BottomRightU = UsedU;
	float BottomRightV = UsedV;

	if(Mode != 2) // aspect preserving (0 and 1): crop, then cover the screen
	{
		const float VideoAspect = (float)m_Width / (float)m_Height;
		const float ScreenAspect = ScreenW / ScreenH;

		// Keeping the whole width only fills the frame when the video is not wider
		// than the frame; keeping the whole height only fills when it is not
		// taller. Exactly one holds, and that one decides the crop direction.
		if(VideoAspect <= FitAspect)
		{
			// keep the whole width, crop the vertical overflow equally
			const float VisibleV = VideoAspect / FitAspect;
			const float Crop = (1.0f - VisibleV) * 0.5f;
			TopLeftV = UsedV * Crop;
			BottomRightV = UsedV - UsedV * Crop;
		}
		else
		{
			// keep the whole height, crop the horizontal overflow equally
			const float VisibleU = FitAspect / VideoAspect;
			const float Crop = (1.0f - VisibleU) * 0.5f;
			TopLeftU = UsedU * Crop;
			BottomRightU = UsedU - UsedU * Crop;
		}

		// The cropped region now has the composition aspect; cover the screen with
		// it (this only crops when a manual mc_background_fit aspect differs from
		// the screen aspect).
		if(FitAspect > ScreenAspect)
		{
			const float Crop = (1.0f - ScreenAspect / FitAspect) * 0.5f;
			const float Width = BottomRightU - TopLeftU;
			TopLeftU += Width * Crop;
			BottomRightU -= Width * Crop;
		}
		else if(FitAspect < ScreenAspect)
		{
			const float Crop = (1.0f - FitAspect / ScreenAspect) * 0.5f;
			const float Height = BottomRightV - TopLeftV;
			TopLeftV += Height * Crop;
			BottomRightV -= Height * Crop;
		}
	}
	// Mode 2 (stretch) keeps the whole texture mapped onto the whole screen.

	// remember the mapping of the visible sub-rect of the texture (exposed via
	// DisplayedUvRect() for anything that needs to know what is behind a panel)
	m_DisplayU0 = TopLeftU;
	m_DisplayV0 = TopLeftV;
	m_DisplayU1 = BottomRightU;
	m_DisplayV1 = BottomRightV;

	m_pGraphics->BlendNormal();
	m_pGraphics->TextureSet(m_Texture);
	m_pGraphics->WrapClamp();
	m_pGraphics->QuadsBegin();
	m_pGraphics->QuadsSetSubset(TopLeftU, TopLeftV, BottomRightU, BottomRightV);
	m_pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	const IGraphics::CQuadItem Quad(DstX, DstY, DstW, DstH);
	m_pGraphics->QuadsDrawTL(&Quad, 1);
	m_pGraphics->QuadsEnd();
	m_pGraphics->WrapNormal();
	m_pGraphics->TextureClear();
	if(g_Config.m_McBackgroundDiag)
		DiagAddRenderMs(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - RenderT0).count());
	return true;
}
