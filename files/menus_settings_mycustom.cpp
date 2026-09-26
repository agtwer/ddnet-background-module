/* Background settings tab (formerly "My custom"): the custom background
 * (png/jpg/mp4 or a Wallpaper Engine wallpaper). Purely local visual feature:
 * it never touches gameplay, network or prediction. */
#include "menus.h"

#include <base/fs.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// Background files live in a folder called "Background" directly inside the
	// save directory (i.e. next to the config files, e.g. %APPDATA%\DDNet\Background).
	const char *CUSTOM_BG_FOLDER = "Background";

	std::vector<std::string> s_vBackgroundFiles;
	bool s_BackgroundFilesScanned = false;

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

	bool IsSupportedBackgroundExtension(const char *pExt)
	{
		static const char *apExts[] = {
			// images (png is decoded by the engine, everything else through FFmpeg —
			// this is what makes jpg/jpeg usable as a background)
			"png", "jpg", "jpeg", "jpe", "jfif", "bmp", "webp", "gif", "tif", "tiff", "avif",
			// videos
			"mp4", "mkv", "webm", "mov", "avi", "m4v", "mpg", "mpeg", "flv", "wmv", "ts", "3gp"};
		for(const char *pExtCandidate : apExts)
		{
			if(str_comp(pExt, pExtCandidate) == 0)
				return true;
		}
		return false;
	}

	int CustomBackgroundScanCallback(const char *pFilename, int IsDir, int StorageType, void *pUser)
	{
		(void)StorageType;
		(void)pUser;
		if(IsDir)
			return 0;
		if(!IsSupportedBackgroundExtension(ExtensionOf(pFilename)))
			return 0;

		char aBuf[IO_MAX_PATH_LENGTH];
		str_format(aBuf, sizeof(aBuf), "%s/%s", CUSTOM_BG_FOLDER, pFilename);
		s_vBackgroundFiles.emplace_back(aBuf);
		return 0;
	}

	void ScanCustomBackgroundFiles(IStorage *pStorage)
	{
		s_BackgroundFilesScanned = true;
		s_vBackgroundFiles.clear();

		pStorage->CreateFolder(CUSTOM_BG_FOLDER, IStorage::TYPE_SAVE);
		pStorage->ListDirectory(IStorage::TYPE_SAVE, CUSTOM_BG_FOLDER, CustomBackgroundScanCallback, nullptr);
	}

	// Accordion state for the resolution picker (click to expand, click to close).
	bool s_FitExpanded = false;
	// Accordion state for the display-mode list (cover / contain / ...).
	bool s_ModeExpanded = false;
	// Accordion state for the video decode-resolution list.
	bool s_ResExpanded = false;
	// Accordion state for the Wallpaper Engine wallpaper list.
	bool s_WeListExpanded = false;

	const char *const apFitPresets[] = {
		"1920x1080", "2560x1440", "3840x2160", "5760x3240",
		"1920x1440", "2560x1920", "3840x2880", "5760x4320"};
	constexpr int FitPresetCount = sizeof(apFitPresets) / sizeof(apFitPresets[0]);
}

/**
 * Renders the custom menu background if it is enabled and loaded,
 * returns whether anything was rendered.
 */
bool CMenus::RenderCustomMenuBackground()
{
	m_CustomBackground.SyncFromConfig(g_Config.m_McMenuBackground, g_Config.m_McBackgroundPath);
	if(!g_Config.m_McMenuBackground)
		return false;
	m_CustomBackground.Update();
	return m_CustomBackground.Render();
}

void CMenus::RenderSettingsMyCustom(CUIRect MainView)
{
	// compact sizes, identical to the TClient settings page
	constexpr float FontSize = 14.0f;
	constexpr float EditBoxFontSize = 12.0f;
	constexpr float LineSize = 20.0f;
	constexpr float HeadlineFontSize = 20.0f;
	constexpr float HeadlineHeight = 20.0f;
	constexpr float Margin = 10.0f;
	constexpr float MarginSmall = 5.0f;

	// The black panel colour used by this page's section boxes and rows, driven by
	// mc_ui_panel_alpha (0..25, where 25 = the engine's authored 0.25 alpha) so
	// the "shadow" behind the options is user adjustable.
	const ColorRGBA PanelColor(0.0f, 0.0f, 0.0f, std::clamp(g_Config.m_McUiPanelAlpha, 0, 25) / 100.0f);

	// Expand/collapse indicator of the accordion rows, drawn at the right end of
	// the header button.
	//
	// It is a Font Awesome glyph, so it MUST be drawn with the icon font preset:
	// gluing it onto a normal label (str_format("%s  %s", text, FontIcon::...))
	// renders a missing-glyph box instead of the arrow, because a button label is
	// drawn with the default font.
	const auto DrawExpandArrow = [this](CUIRect Rect, bool Expanded) {
		CUIRect Arrow;
		Rect.VSplitRight(24.0f, nullptr, &Arrow);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		Ui()->DoLabel(&Arrow, Expanded ? FontIcon::CHEVRON_UP : FontIcon::CHEVRON_DOWN, 14.0f, TEXTALIGN_MC);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};

	// refresh the file list before layout so the file section height is known
	if(!s_BackgroundFilesScanned)
		ScanCustomBackgroundFiles(Storage());

	// Wallpaper Engine list is only scanned when that source is actually selected,
	// so the Steam/workshop folders are never touched for users who don't use it.
	const bool UseWallpaperEngine = g_Config.m_McBackgroundSource == 1;
	if(UseWallpaperEngine)
		m_WallpaperEngine.Scan();

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	MainView.VSplitRight(5.0f, &MainView, nullptr); // padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // padding for scrollbar

	CUIRect Column = MainView;
	char aBuf[256];

	// ***** Section: source + settings ***** //
	{
		// rows: source switch + 2 checkboxes + (file row OR we list toggle)
		// + fps + fit accordion (+ expanded rows) + status + buttons
		const float ModeRows = s_ModeExpanded ? 4.0f : 0.0f;
		const float ResRows = s_ResExpanded ? 5.0f : 0.0f;
		const float FitRows = s_FitExpanded ? (float)(FitPresetCount + 1) : 1.0f;
		// the "effective crop direction" hint only shows for the two crop modes
		const bool ShowCropHint = std::clamp(g_Config.m_McBackgroundMode, 0, 3) <= 1 &&
					  m_CustomBackground.SrcWidth() > 0 && m_CustomBackground.SrcHeight() > 0;
		const float HintRows = ShowCropHint ? 1.0f : 0.0f;
		// source + 2 checkboxes + file + fps + fit header + video res + mode
		// + black overlay + title toggle + status + buttons
		constexpr float BaseRows = 1.0f + 2.0f + 1.0f + 1.0f + 1.0f + 1.0f + 1.0f + 1.0f + 1.0f + 2.0f;
		const float BoxHeight = Margin + HeadlineHeight + MarginSmall + LineSize * (BaseRows + FitRows + ModeRows + ResRows + HintRows) + Margin;
		CUIRect Box;
		Column.HSplitTop(BoxHeight, &Box, &Column);
		if(!s_ScrollRegion.AddRect(Box))
		{
			// box scrolled out of view: only its scroll extent was registered
		}
		else
		{
		Box.Draw(PanelColor, IGraphics::CORNER_ALL, 10.0f);

		CUIRect Content = Box;
		Content.HSplitTop(Margin, nullptr, &Content);
		CUIRect Headline;
		Content.HSplitTop(HeadlineHeight, &Headline, &Content);
		Ui()->DoLabel(&Headline, TCLocalize("Background settings", "Background"), HeadlineFontSize, TEXTALIGN_ML);
		Content.HSplitTop(MarginSmall, nullptr, &Content);

		// Where the background comes from: your own file, or Wallpaper Engine.
		// Both feed the same pipeline, so this is a source choice, not two tabs.
		CUIRect SourceRow;
		Content.HSplitTop(LineSize, &SourceRow, &Content);
		CUIRect SourceLabel;
		SourceRow.VSplitLeft(90.0f, &SourceLabel, &SourceRow);
		Ui()->DoLabel(&SourceLabel, TCLocalize("Source"), FontSize, TEXTALIGN_ML);
		static CButtonContainer s_aSourceIds[2];
		static const char *const apSourceNames[] = {"My own file", "Wallpaper Engine"};
		const float SourceWidth = SourceRow.w / 2.0f;
		for(int i = 0; i < 2; ++i)
		{
			CUIRect Slot;
			SourceRow.VSplitLeft(SourceWidth, &Slot, &SourceRow);
			if(DoButton_Menu(&s_aSourceIds[i], TCLocalize(apSourceNames[i]), g_Config.m_McBackgroundSource == i ? 1 : 0, &Slot))
			{
				g_Config.m_McBackgroundSource = i;
				// switching to Wallpaper Engine opens its list right away
				if(i == 1)
					s_WeListExpanded = true;
			}
		}

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_McMenuBackground, TCLocalize("Show custom background on the main menu"), &g_Config.m_McMenuBackground, &Content, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_McGameBackground, TCLocalize("Show custom background on the entity layer (in game)"), &g_Config.m_McGameBackground, &Content, LineSize);

		if(!UseWallpaperEngine)
		{
			// background file path
			CUIRect PathRow;
			Content.HSplitTop(LineSize, &PathRow, &Content);
			CUIRect PathLabel, PathBox;
			PathRow.VSplitLeft(60.0f, &PathLabel, &PathBox);
			Ui()->DoLabel(&PathLabel, TCLocalize("File:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_PathInput;
			s_PathInput.SetBuffer(g_Config.m_McBackgroundPath, sizeof(g_Config.m_McBackgroundPath));
			s_PathInput.SetEmptyText(TCLocalize("C:/path/to/background.png, .jpg or .mp4"));
			Ui()->DoEditBox(&s_PathInput, &PathBox, EditBoxFontSize);
		}
		else
		{
			// collapsed list of Wallpaper Engine wallpapers
			CUIRect WeToggle;
			Content.HSplitTop(LineSize, &WeToggle, &Content);
			const auto &vWallpapers = m_WallpaperEngine.Wallpapers();
			const char *pSelectedName = TCLocalize("none selected");
			if(g_Config.m_McWePath[0] != '\0')
			{
				const auto *pFound = m_WallpaperEngine.Find(g_Config.m_McWePath);
				if(pFound != nullptr)
					pSelectedName = pFound->m_Title.c_str();
				else
					pSelectedName = g_Config.m_McWePath;
			}
			str_format(aBuf, sizeof(aBuf), "%s: %s", TCLocalize("Wallpaper"), pSelectedName);
			static CButtonContainer s_WeToggleId;
			if(DoButton_Menu(&s_WeToggleId, aBuf, s_WeListExpanded ? 1 : 0, &WeToggle))
				s_WeListExpanded = !s_WeListExpanded;
			DrawExpandArrow(WeToggle, s_WeListExpanded);
			if(vWallpapers.empty())
			{
				CUIRect EmptyRow;
				Content.HSplitTop(LineSize, &EmptyRow, &Content);
				const std::string &Error = m_WallpaperEngine.LastError();
				Ui()->DoLabel(&EmptyRow, Error.empty() ? TCLocalize("No video or image wallpapers found.") : Error.c_str(), FontSize, TEXTALIGN_ML);
			}
		}

		// video frame rate cap: slider AND numeric input box side by side, so
		// the value can be dragged quickly or typed exactly.
		CUIRect FpsRow;
		Content.HSplitTop(LineSize, &FpsRow, &Content);
		CUIRect FpsBoxRect;
		FpsRow.VSplitRight(56.0f, &FpsRow, &FpsBoxRect);
		FpsRow.VSplitRight(MarginSmall, &FpsRow, nullptr);
		Ui()->DoScrollbarOption(&g_Config.m_McBackgroundVideoFps, &g_Config.m_McBackgroundVideoFps, &FpsRow, TCLocalize("Video frame rate (0 = source)"), 0, 120, &CUi::ms_LinearScrollbarScale, 0u, " fps");
		static CLineInputNumber s_FpsInput;
		static int s_FpsShown = -1;
		if(!s_FpsInput.IsActive() && s_FpsShown != g_Config.m_McBackgroundVideoFps)
		{
			s_FpsInput.SetInteger(g_Config.m_McBackgroundVideoFps);
			s_FpsShown = g_Config.m_McBackgroundVideoFps;
		}
		Ui()->DoEditBox(&s_FpsInput, &FpsBoxRect, EditBoxFontSize);
		if(!s_FpsInput.IsActive())
		{
			int Value = s_FpsInput.GetInteger();
			Value = std::clamp(Value, 0, 120);
			if(Value != g_Config.m_McBackgroundVideoFps)
				g_Config.m_McBackgroundVideoFps = Value;
			s_FpsShown = g_Config.m_McBackgroundVideoFps;
		}

		// background fit: an accordion (click the header to expand, click a
		// preset to pick it, click the header again to collapse) instead of a
		// cycling button — this is what the game's own lists do.
		CUIRect FitHeaderRow;
		Content.HSplitTop(LineSize, &FitHeaderRow, &Content);
		const char *pCurrentFit = g_Config.m_McBackgroundFit[0] == '\0' ? "auto" : g_Config.m_McBackgroundFit;
		char aFitButton[128];
		str_format(aFitButton, sizeof(aFitButton), "%s", pCurrentFit);
		CUIRect FitLabel, FitButton;
		FitHeaderRow.VSplitLeft(150.0f, &FitLabel, &FitButton);
		Ui()->DoLabel(&FitLabel, TCLocalize("Background fit"), FontSize, TEXTALIGN_ML);
		static CButtonContainer s_FitButtonId;
		if(DoButton_Menu(&s_FitButtonId, aFitButton, s_FitExpanded ? 1 : 0, &FitButton))
			s_FitExpanded = !s_FitExpanded;
		DrawExpandArrow(FitButton, s_FitExpanded);
		GameClient()->m_Tooltips.DoToolTip(&s_FitButtonId, &FitButton, TCLocalize("Expand to choose the composition aspect (crop reference); it never changes the video's decode resolution"));

		if(s_FitExpanded)
		{
			char aScreenFit[32];
			str_format(aScreenFit, sizeof(aScreenFit), "%dx%d", g_Config.m_GfxScreenWidth, g_Config.m_GfxScreenHeight);
			static CButtonContainer s_aFitIds[FitPresetCount + 1];
			CUIRect FitSlot;
			Content.HSplitTop(LineSize, &FitSlot, &Content);
			if(s_ScrollRegion.AddRect(FitSlot))
			{
				if(DoButton_Menu(&s_aFitIds[0], TCLocalize("auto (detect screen)"), str_comp(pCurrentFit, "auto") == 0 ? 1 : 0, &FitSlot))
					str_copy(g_Config.m_McBackgroundFit, "auto", sizeof(g_Config.m_McBackgroundFit));
			}
			for(int i = 0; i < FitPresetCount; ++i)
			{
				Content.HSplitTop(LineSize, &FitSlot, &Content);
				if(!s_ScrollRegion.AddRect(FitSlot))
					continue;
				const bool IsScreenSize = str_comp(pCurrentFit, aScreenFit) == 0 && str_comp(apFitPresets[i], aScreenFit) == 0;
				if(DoButton_Menu(&s_aFitIds[i + 1], apFitPresets[i], (str_comp(pCurrentFit, apFitPresets[i]) == 0 || IsScreenSize) ? 1 : 0, &FitSlot))
					str_copy(g_Config.m_McBackgroundFit, apFitPresets[i], sizeof(g_Config.m_McBackgroundFit));
			}
		}

		// Video decode resolution. Deliberately SEPARATE from "Background fit":
		// the fit setting is only a composition/crop aspect, while this one
		// decides the size the video is actually decoded at. It defaults to the
		// source size, so a 4K clip is never crushed to the screen size.
		CUIRect ResRow;
		Content.HSplitTop(LineSize, &ResRow, &Content);
		CUIRect ResLabel, ResBox;
		ResRow.VSplitLeft(150.0f, &ResLabel, &ResBox);
		Ui()->DoLabel(&ResLabel, TCLocalize("Video resolution"), FontSize, TEXTALIGN_ML);
		static const char *const apResNames[] = {"Original (source)", "2160p", "1440p", "1080p", "720p"};
		const int ResIndex = std::clamp(g_Config.m_McBackgroundVideoRes, 0, 4);
		str_format(aBuf, sizeof(aBuf), "%s", TCLocalize(apResNames[ResIndex]));
		static CButtonContainer s_ResButtonId;
		if(DoButton_Menu(&s_ResButtonId, aBuf, s_ResExpanded ? 1 : 0, &ResBox))
			s_ResExpanded = !s_ResExpanded;
		DrawExpandArrow(ResBox, s_ResExpanded);
		GameClient()->m_Tooltips.DoToolTip(&s_ResButtonId, &ResBox, TCLocalize("Expand to limit the video decode resolution (performance trade-off)"));

		if(s_ResExpanded)
		{
			static CButtonContainer s_aResIds[5];
			for(int i = 0; i < 5; ++i)
			{
				CUIRect ResSlot;
				Content.HSplitTop(LineSize, &ResSlot, &Content);
				if(!s_ScrollRegion.AddRect(ResSlot))
					continue;
				if(DoButton_Menu(&s_aResIds[i], TCLocalize(apResNames[i]), g_Config.m_McBackgroundVideoRes == i ? 1 : 0, &ResSlot))
					g_Config.m_McBackgroundVideoRes = i;
			}
		}

		// Display mode: the background always fills the screen (no bars), so
		// this chooses which direction is cropped / whether to stretch or tile.
		CUIRect ModeRow;
		Content.HSplitTop(LineSize, &ModeRow, &Content);
		CUIRect ModeLabel, ModeBox;
		ModeRow.VSplitLeft(150.0f, &ModeLabel, &ModeBox);
		Ui()->DoLabel(&ModeLabel, TCLocalize("Display mode"), FontSize, TEXTALIGN_ML);
		static const char *const apModeNames[] = {"Crop top/bottom", "Crop left/right", "Stretch", "Tile"};
		const int ModeIndex = std::clamp(g_Config.m_McBackgroundMode, 0, 3);
		str_format(aBuf, sizeof(aBuf), "%s", TCLocalize(apModeNames[ModeIndex]));
		static CButtonContainer s_ModeButtonId;
		if(DoButton_Menu(&s_ModeButtonId, aBuf, s_ModeExpanded ? 1 : 0, &ModeBox))
			s_ModeExpanded = !s_ModeExpanded;
		DrawExpandArrow(ModeBox, s_ModeExpanded);
		GameClient()->m_Tooltips.DoToolTip(&s_ModeButtonId, &ModeBox, TCLocalize("Expand to choose how the background fills the screen"));

		if(s_ModeExpanded)
		{
			static CButtonContainer s_aModeIds[4];
			for(int i = 0; i < 4; ++i)
			{
				CUIRect ModeSlot;
				Content.HSplitTop(LineSize, &ModeSlot, &Content);
				if(!s_ScrollRegion.AddRect(ModeSlot))
					continue;
				if(DoButton_Menu(&s_aModeIds[i], TCLocalize(apModeNames[i]), g_Config.m_McBackgroundMode == i ? 1 : 0, &ModeSlot))
					g_Config.m_McBackgroundMode = i;
			}
		}

		// Tell the user which crop direction is actually possible for the loaded
		// video on this screen. Only one direction can fill the screen without
		// bars, so the crop modes are a preference and this line reports the
		// effective one — otherwise picking the impossible option would look
		// like the setting did nothing.
		if(ModeIndex <= 1 && m_CustomBackground.SrcWidth() > 0 && m_CustomBackground.SrcHeight() > 0)
		{
			CUIRect HintRow;
			Content.HSplitTop(LineSize, &HintRow, &Content);
			const float VideoAspect = (float)m_CustomBackground.SrcWidth() / (float)m_CustomBackground.SrcHeight();
			float TargetAspect = (float)g_Config.m_GfxScreenWidth / (float)std::max(1, g_Config.m_GfxScreenHeight);
			int FitW = 0;
			int FitH = 0;
			if(sscanf(g_Config.m_McBackgroundFit, "%dx%d", &FitW, &FitH) == 2 && FitW > 0 && FitH > 0)
				TargetAspect = (float)FitW / (float)FitH;
			const bool CropsSides = VideoAspect > TargetAspect;
			str_format(aBuf, sizeof(aBuf), "%s: %s", TCLocalize("Effective"), CropsSides ? TCLocalize("crop left/right") : TCLocalize("crop top/bottom"));
			TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
			Ui()->DoLabel(&HintRow, aBuf, FontSize, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}

		// Menu presentation: the black panel behind the rows/boxes (the "shadow"
		// the options sit on) and the big main-menu title image.
		// The shadow is offered as five equal steps between "no shadow at all"
		// and the engine's authored value (25): transparent / hazy / faint /
		// subtle / normal. Picking one stores the matching opacity.
		CUIRect ShadowRow;
		Content.HSplitTop(LineSize, &ShadowRow, &Content);
		CUIRect ShadowLabel, ShadowSlots;
		ShadowRow.VSplitLeft(150.0f, &ShadowLabel, &ShadowSlots);
		Ui()->DoLabel(&ShadowLabel, TCLocalize("Option shadow"), FontSize, TEXTALIGN_ML);
		static const int s_aShadowLevels[5] = {0, 6, 13, 19, 25};
		static const char *const s_apShadowNames[5] = {"Transparent", "Hazy", "Faint", "Subtle", "Normal"};
		static CButtonContainer s_aShadowIds[5];
		const float ShadowSlotWidth = ShadowSlots.w / 5.0f;
		for(int i = 0; i < 5; ++i)
		{
			CUIRect Slot;
			ShadowSlots.VSplitLeft(ShadowSlotWidth, &Slot, &ShadowSlots);
			if(DoButton_Menu(&s_aShadowIds[i], TCLocalize(s_apShadowNames[i]), g_Config.m_McUiPanelAlpha == s_aShadowLevels[i] ? 1 : 0, &Slot))
				g_Config.m_McUiPanelAlpha = s_aShadowLevels[i];
		}

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_McMenuTitle, TCLocalize("Show the big DDNet TClient title on the main menu"), &g_Config.m_McMenuTitle, &Content, LineSize);

		// status line
		CUIRect StatusRow;
		Content.HSplitTop(LineSize, &StatusRow, &Content);
		str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Status"), m_CustomBackground.StatusText());
		if(m_CustomBackground.HasError())
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&StatusRow, aBuf, FontSize, TEXTALIGN_ML);
		if(m_CustomBackground.HasError())
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		// folder + reload buttons
		CUIRect ButtonRow;
		Content.HSplitTop(LineSize, &ButtonRow, &Content);
		CUIRect ReloadButton;
		ButtonRow.VSplitRight(LineSize, &ButtonRow, &ReloadButton);
		ButtonRow.VSplitRight(MarginSmall, &ButtonRow, nullptr);

		static CButtonContainer s_OpenFolderId;
		if(DoButton_Menu(&s_OpenFolderId, TCLocalize("Backgrounds folder"), 0, &ButtonRow))
		{
			Storage()->CreateFolder(CUSTOM_BG_FOLDER, IStorage::TYPE_SAVE);
			char aFullPath[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, CUSTOM_BG_FOLDER, aFullPath, sizeof(aFullPath));
			Client()->ViewFile(aFullPath);
			s_BackgroundFilesScanned = false;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_OpenFolderId, &ButtonRow, TCLocalize("Open the folder to add png/jpg/mp4 background files"));

		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		static CButtonContainer s_ReloadButtonId;
		const int ReloadClicked = DoButton_Menu(&s_ReloadButtonId, FontIcon::ARROW_ROTATE_RIGHT, 0, &ReloadButton);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		if(ReloadClicked)
		{
			s_BackgroundFilesScanned = false;
			m_WallpaperEngine.Scan(true);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_ReloadButtonId, &ReloadButton, TCLocalize("Reload the file list"));
		}
	}

	// ***** Section: Wallpaper Engine list (only when that source is active) ***** //
	if(UseWallpaperEngine && s_WeListExpanded)
	{
		const auto &vWallpapers = m_WallpaperEngine.Wallpapers();
		const float Rows = vWallpapers.empty() ? 1.0f : (float)vWallpapers.size();
		CUIRect ListBox;
		Column.HSplitTop(MarginSmall + HeadlineHeight + LineSize * Rows + Margin, &ListBox, &Column);
		if(s_ScrollRegion.AddRect(ListBox))
		{
			ListBox.Draw(PanelColor, IGraphics::CORNER_ALL, 10.0f);
			CUIRect Content = ListBox;
			Content.HSplitTop(Margin, nullptr, &Content);
			CUIRect Headline;
			Content.HSplitTop(HeadlineHeight, &Headline, &Content);
			Ui()->DoLabel(&Headline, TCLocalize("Wallpaper Engine wallpapers (video and image only)"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			if(vWallpapers.empty())
			{
				CUIRect EmptyRow;
				Content.HSplitTop(LineSize, &EmptyRow, &Content);
				Ui()->DoLabel(&EmptyRow, TCLocalize("Nothing found. Scene and application wallpapers are not supported."), FontSize, TEXTALIGN_ML);
			}
			else
			{
				static std::vector<CButtonContainer> s_vWeButtons;
				if(s_vWeButtons.size() < vWallpapers.size())
					s_vWeButtons.resize(vWallpapers.size());
				for(size_t i = 0; i < vWallpapers.size(); ++i)
				{
					CUIRect Slot;
					Content.HSplitTop(LineSize, &Slot, &Content);
					if(!s_ScrollRegion.AddRect(Slot))
						continue;
					const auto &Wallpaper = vWallpapers[i];
					const bool Selected = str_comp(g_Config.m_McWePath, Wallpaper.m_Path.c_str()) == 0;
					str_format(aBuf, sizeof(aBuf), "%s  [%s]", Wallpaper.m_Title.c_str(), CWallpaperEngine::TypeName(Wallpaper.m_Type));
					if(DoButton_Menu(&s_vWeButtons[i], aBuf, Selected ? 1 : 0, &Slot))
					{
						str_copy(g_Config.m_McWePath, Wallpaper.m_Path.c_str(), sizeof(g_Config.m_McWePath));
						// hand the wallpaper's media file to the same background
						// pipeline, so playback reuses the existing decode/upload
						str_copy(g_Config.m_McBackgroundPath, Wallpaper.m_Media.c_str(), sizeof(g_Config.m_McBackgroundPath));
						g_Config.m_McMenuBackground = 1;
					}
				}
			}
		}
	}

	// ***** Section: Wallpaper Engine sound ***** //
	if(UseWallpaperEngine)
	{
		constexpr float WeSoundRows = 4.0f;
		CUIRect Box;
		Column.HSplitTop(Margin + HeadlineHeight + MarginSmall + LineSize * WeSoundRows + Margin, &Box, &Column);
		if(s_ScrollRegion.AddRect(Box))
		{
			Box.Draw(PanelColor, IGraphics::CORNER_ALL, 10.0f);
			CUIRect Content = Box;
			Content.HSplitTop(Margin, nullptr, &Content);
			CUIRect Headline;
			Content.HSplitTop(HeadlineHeight, &Headline, &Content);
			Ui()->DoLabel(&Headline, TCLocalize("Wallpaper sound", "Wallpaper Engine"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_McWeSound, TCLocalize("Play wallpaper sound"), &g_Config.m_McWeSound, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_McWePauseOnBlur, TCLocalize("Pause the wallpaper when the game loses focus"), &g_Config.m_McWePauseOnBlur, &Content, LineSize);
			CUIRect VolumeRow;
			Content.HSplitTop(LineSize, &VolumeRow, &Content);
			Ui()->DoScrollbarOption(&g_Config.m_McWeVolume, &g_Config.m_McWeVolume, &VolumeRow, TCLocalize("Wallpaper volume"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
			CUIRect NoteRow;
			Content.HSplitTop(LineSize, &NoteRow, &Content);
			Ui()->DoLabel(&NoteRow, TCLocalize("Wallpaper sound is applied when the background video has an audio track."), FontSize, TEXTALIGN_ML);
		}
	}

	// ***** Section: my own file list (only for the file source) ***** //
	if(!UseWallpaperEngine)
	{
		const size_t FileCount = s_vBackgroundFiles.size();
		const float Rows = (float)(FileCount > 0 ? FileCount : 1);
		const float BoxHeight = Margin + HeadlineHeight + MarginSmall + LineSize * Rows + Margin;
		CUIRect Box;
		Column.HSplitTop(BoxHeight, &Box, &Column);
		if(!s_ScrollRegion.AddRect(Box))
		{
			// box scrolled out of view: only its scroll extent was registered
		}
		else
		{
		Box.Draw(PanelColor, IGraphics::CORNER_ALL, 10.0f);

		CUIRect Content = Box;
		Content.HSplitTop(Margin, nullptr, &Content);
		CUIRect Headline;
		Content.HSplitTop(HeadlineHeight, &Headline, &Content);
		Ui()->DoLabel(&Headline, TCLocalize("Background files", "Background"), HeadlineFontSize, TEXTALIGN_ML);
		Content.HSplitTop(MarginSmall, nullptr, &Content);

		if(FileCount == 0)
		{
			CUIRect EmptyRow;
			Content.HSplitTop(LineSize, &EmptyRow, &Content);
			Ui()->DoLabel(&EmptyRow, TCLocalize("No files found. Put png/jpg/mp4 files into the folder above."), FontSize, TEXTALIGN_ML);
		}
		else
		{
		static std::vector<CButtonContainer> s_vFileButtons;
		if(s_vFileButtons.size() < FileCount)
			s_vFileButtons.resize(FileCount);

		for(size_t i = 0; i < FileCount; ++i)
		{
			CUIRect Slot;
			Content.HSplitTop(LineSize, &Slot, &Content);
			if(!s_ScrollRegion.AddRect(Slot))
				continue;

			const std::string &File = s_vBackgroundFiles[i];
			const bool Selected = str_comp(g_Config.m_McBackgroundPath, File.c_str()) == 0;
			if(DoButton_Menu(&s_vFileButtons[i], File.c_str(), Selected ? 1 : 0, &Slot))
			{
				str_copy(g_Config.m_McBackgroundPath, File.c_str(), sizeof(g_Config.m_McBackgroundPath));
			}
		}
		}
		}
	}
	s_ScrollRegion.End();
}
