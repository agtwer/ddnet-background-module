#ifndef GAME_CLIENT_COMPONENTS_WALLPAPER_ENGINE_H
#define GAME_CLIENT_COMPONENTS_WALLPAPER_ENGINE_H

#include <base/types.h>

#include <string>
#include <vector>

/**
 * Wallpaper Engine wallpaper discovery (Background -> Wallpaper Engine).
 *
 * Scope, per the user's request: only the core wallpaper functionality plus
 * sound — video wallpapers and image wallpapers. Scene and application
 * wallpapers are deliberately NOT supported (that would require porting
 * Wallpaper Engine's own scene renderer, which is out of scope).
 *
 * How a wallpaper is described on disk (verified against Wallpaper Engine's
 * format and the dsh-wallpaper-engine reference implementation):
 *   <item>/project.json with { "file": ..., "type": ..., "title": ... }
 *   type is one of "video" | "image" (formerly "scene") | "web" | "application".
 *   Wallpapers live in either the install directory (projects/defaultprojects,
 *   projects/myprojects) or the Steam Workshop (steamapps/workshop/content/431960).
 *
 * This class only *finds* wallpapers and resolves the media file; the actual
 * decode/playback reuses the existing FFmpeg background pipeline, and the audio
 * uses the engine's own sound system. It never touches gameplay state.
 */
class CWallpaperEngine
{
public:
	enum class EType
	{
		UNKNOWN,
		VIDEO, // .mp4 / .webm / ... — playable by the existing video pipeline
		IMAGE, // .jpg / .png / ...   — playable by the existing image pipeline
		WEB,   // HTML — not supported here
		SCENE, // .pkg — not supported here
		APPLICATION,
	};

	struct SWallpaper
	{
		std::string m_Id;    // workshop id, or the folder name for local projects
		std::string m_Title; // human readable name from project.json
		std::string m_Path;  // absolute path to the wallpaper folder
		std::string m_Media; // absolute path to the media file to play
		EType m_Type = EType::UNKNOWN;
	};

	// Rescan the Steam library + Wallpaper Engine install for wallpapers.
	// Cheap enough to call on demand (it is cached after the first scan).
	void Scan(bool Force = false);

	const std::vector<SWallpaper> &Wallpapers() const { return m_vWallpapers; }
	const std::string &InstallDir() const { return m_InstallDir; }
	const std::string &LastError() const { return m_LastError; }

	// Find one wallpaper by workshop id or folder path (what mc_we_path stores).
	const SWallpaper *Find(const char *pIdOrPath) const;

	// Used by the directory scanner (public so the file-local callback can call it).
	void AddFound(const SWallpaper &Wallpaper);

	// Resolve a wallpaper's media file for the background pipeline. Returns an
	// empty string for scene/web/application wallpapers (unsupported).
	static std::string ResolveMedia(const std::string &Folder, EType Type, const std::string &DeclaredFile);
	static const char *TypeName(EType Type);

private:
	std::vector<SWallpaper> m_vWallpapers;
	std::string m_InstallDir;
	std::string m_LastError;
	bool m_Scanned = false;

	void ScanRoot(const std::string &Root);
	static bool ReadProjectJson(const std::string &Folder, std::string &Title, std::string &File, EType &Type);
	static EType TypeFromString(const std::string &Type, const std::string &File);
	static bool FileExists(const std::string &Path);
	static bool DirExists(const std::string &Path);
	static std::string JoinPath(const std::string &A, const std::string &B);
};

#endif
