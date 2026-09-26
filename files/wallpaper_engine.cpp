#include "wallpaper_engine.h"

#include <base/fs.h>
#include <base/log.h>
#include <base/str.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
	// Wallpaper Engine's Steam application id. Workshop wallpapers live under
	// <steam library>/steamapps/workshop/content/<this id>/<workshop id>/.
	constexpr const char *WE_APPID = "431960";

	void ToLowerExt(const char *pPath, char *pOut, size_t OutSize)
	{
		const char *pDot = nullptr;
		for(const char *p = pPath; *p != '\0'; ++p)
		{
			if(*p == '.')
				pDot = p;
		}
		if(pDot == nullptr)
		{
			pOut[0] = '\0';
			return;
		}
		++pDot;
		size_t i = 0;
		for(; pDot[i] != '\0' && i + 1 < OutSize; ++i)
			pOut[i] = (char)tolower((unsigned char)pDot[i]);
		pOut[i] = '\0';
	}

	// Read a JSON string value for a top-level key. Wallpaper Engine's
	// project.json is machine generated and flat for the fields we need, so a
	// tiny scanner is enough and avoids pulling in a JSON dependency.
	bool JsonString(const std::string &Json, const char *pKey, std::string &Out)
	{
		char aPattern[64];
		str_format(aPattern, sizeof(aPattern), "\"%s\"", pKey);
		size_t Pos = Json.find(aPattern);
		if(Pos == std::string::npos)
			return false;
		Pos = Json.find(':', Pos + str_length(aPattern));
		if(Pos == std::string::npos)
			return false;
		++Pos;
		while(Pos < Json.size() && (Json[Pos] == ' ' || Json[Pos] == '\t' || Json[Pos] == '\r' || Json[Pos] == '\n'))
			++Pos;
		if(Pos >= Json.size() || Json[Pos] != '"')
			return false;
		++Pos;
		Out.clear();
		while(Pos < Json.size() && Json[Pos] != '"')
		{
			// handle the two escapes that actually show up in wallpaper titles
			if(Json[Pos] == '\\' && Pos + 1 < Json.size())
			{
				++Pos;
				switch(Json[Pos])
				{
				case 'n': Out.push_back('\n'); break;
				case 't': Out.push_back('\t'); break;
				case '"': Out.push_back('"'); break;
				case '\\': Out.push_back('\\'); break;
				case '/': Out.push_back('/'); break;
				case 'u':
					// Non-ASCII titles are common (Chinese wallpapers). Decode
					// \uXXXX to UTF-8 so they display correctly. Surrogate pairs
					// are combined when a second \uXXXX follows.
					if(Pos + 4 < Json.size())
					{
						char aHex[5] = {Json[Pos + 1], Json[Pos + 2], Json[Pos + 3], Json[Pos + 4], '\0'};
						unsigned long Cp = strtoul(aHex, nullptr, 16);
						Pos += 4;
						if(Cp >= 0xD800 && Cp <= 0xDBFF && Pos + 6 < Json.size() && Json[Pos + 1] == '\\' && Json[Pos + 2] == 'u')
						{
							char aLow[5] = {Json[Pos + 3], Json[Pos + 4], Json[Pos + 5], Json[Pos + 6], '\0'};
							const unsigned long Low = strtoul(aLow, nullptr, 16);
							if(Low >= 0xDC00 && Low <= 0xDFFF)
							{
								Cp = 0x10000 + ((Cp - 0xD800) << 10) + (Low - 0xDC00);
								Pos += 6;
							}
						}
						char aUtf8[4];
						int Len = 0;
						if(Cp < 0x80)
						{
							aUtf8[Len++] = (char)Cp;
						}
						else if(Cp < 0x800)
						{
							aUtf8[Len++] = (char)(0xC0 | (Cp >> 6));
							aUtf8[Len++] = (char)(0x80 | (Cp & 0x3F));
						}
						else if(Cp < 0x10000)
						{
							aUtf8[Len++] = (char)(0xE0 | (Cp >> 12));
							aUtf8[Len++] = (char)(0x80 | ((Cp >> 6) & 0x3F));
							aUtf8[Len++] = (char)(0x80 | (Cp & 0x3F));
						}
						else
						{
							aUtf8[Len++] = (char)(0xF0 | (Cp >> 18));
							aUtf8[Len++] = (char)(0x80 | ((Cp >> 12) & 0x3F));
							aUtf8[Len++] = (char)(0x80 | ((Cp >> 6) & 0x3F));
							aUtf8[Len++] = (char)(0x80 | (Cp & 0x3F));
						}
						Out.append(aUtf8, (size_t)Len);
					}
					break;
				default: Out.push_back(Json[Pos]); break;
				}
				++Pos;
				continue;
			}
			Out.push_back(Json[Pos]);
			++Pos;
		}
		return true;
	}

	bool ReadWholeFile(const std::string &Path, std::string &Out)
	{
		FILE *pFile = fopen(Path.c_str(), "rb");
		if(pFile == nullptr)
			return false;
		fseek(pFile, 0, SEEK_END);
		const long Size = ftell(pFile);
		fseek(pFile, 0, SEEK_SET);
		if(Size <= 0)
		{
			fclose(pFile);
			return false;
		}
		Out.resize((size_t)Size);
		const size_t Read = fread(&Out[0], 1, (size_t)Size, pFile);
		fclose(pFile);
		Out.resize(Read);
		return Read > 0;
	}

}

std::string CWallpaperEngine::JoinPath(const std::string &A, const std::string &B)
{
	if(A.empty())
		return B;
	if(B.empty())
		return A;
	const char Last = A[A.size() - 1];
	if(Last == '/' || Last == '\\')
		return A + B;
	return A + "/" + B;
}

bool CWallpaperEngine::FileExists(const std::string &Path)
{
	return fs_is_file(Path.c_str()) != 0;
}

bool CWallpaperEngine::DirExists(const std::string &Path)
{
	return fs_is_dir(Path.c_str()) != 0;
}

const char *CWallpaperEngine::TypeName(EType Type)
{
	switch(Type)
	{
	case EType::VIDEO: return "video";
	case EType::IMAGE: return "image";
	case EType::WEB: return "web";
	case EType::SCENE: return "scene";
	case EType::APPLICATION: return "application";
	case EType::UNKNOWN: break;
	}
	return "unknown";
}

CWallpaperEngine::EType CWallpaperEngine::TypeFromString(const std::string &Type, const std::string &File)
{
	std::string Lower;
	Lower.reserve(Type.size());
	for(const char c : Type)
		Lower.push_back((char)tolower((unsigned char)c));

	if(Lower == "video")
		return EType::VIDEO;
	if(Lower == "image")
		return EType::IMAGE;
	if(Lower == "web")
		return EType::WEB;
	if(Lower == "scene")
		return EType::SCENE;
	if(Lower == "application")
		return EType::APPLICATION;

	// No usable "type": infer it from the entry file, exactly like Wallpaper
	// Engine's own tooling does.
	char aExt[16];
	ToLowerExt(File.c_str(), aExt, sizeof(aExt));
	if(str_comp(aExt, "mp4") == 0 || str_comp(aExt, "webm") == 0 || str_comp(aExt, "mkv") == 0 ||
		str_comp(aExt, "avi") == 0 || str_comp(aExt, "mov") == 0 || str_comp(aExt, "m4v") == 0)
		return EType::VIDEO;
	if(str_comp(aExt, "jpg") == 0 || str_comp(aExt, "jpeg") == 0 || str_comp(aExt, "png") == 0 ||
		str_comp(aExt, "gif") == 0 || str_comp(aExt, "webp") == 0 || str_comp(aExt, "bmp") == 0)
		return EType::IMAGE;
	if(str_comp(aExt, "html") == 0 || str_comp(aExt, "htm") == 0 || str_comp(aExt, "js") == 0)
		return EType::WEB;
	if(str_comp(aExt, "pkg") == 0)
		return EType::SCENE;
	return EType::UNKNOWN;
}

bool CWallpaperEngine::ReadProjectJson(const std::string &Folder, std::string &Title, std::string &File, EType &Type)
{
	const std::string ProjectJson = JoinPath(Folder, "project.json");
	std::string Json;
	if(!ReadWholeFile(ProjectJson, Json))
		return false;

	std::string TypeStr;
	const bool HasFile = JsonString(Json, "file", File);
	JsonString(Json, "type", TypeStr);
	if(!JsonString(Json, "title", Title) || Title.empty())
	{
		// fall back to the folder name when the wallpaper has no title
		const size_t Slash = Folder.find_last_of("/\\");
		Title = Slash == std::string::npos ? Folder : Folder.substr(Slash + 1);
	}
	Type = TypeFromString(TypeStr, File);
	return HasFile || !TypeStr.empty();
}

std::string CWallpaperEngine::ResolveMedia(const std::string &Folder, EType Type, const std::string &DeclaredFile)
{
	// Only video and image wallpapers are supported: scene wallpapers need
	// Wallpaper Engine's own scene renderer and web needs a browser engine.
	if(Type != EType::VIDEO && Type != EType::IMAGE)
		return std::string();

	if(!DeclaredFile.empty())
	{
		const std::string Candidate = JoinPath(Folder, DeclaredFile);
		if(FileExists(Candidate))
			return Candidate;
	}

	// Fall back to the first media file in the folder. Wallpaper Engine items
	// frequently declare a scene.json while shipping only a video, and orphaned
	// folders have no project.json at all.
	static const char *apVideoExts[] = {"mp4", "webm", "mkv", "mov", "avi", "m4v"};
	static const char *apImageExts[] = {"jpg", "jpeg", "png", "webp", "bmp", "gif"};
	const bool WantVideo = Type == EType::VIDEO;

	struct SFindCtx
	{
		const char *const *m_apExts;
		int m_ExtCount;
		std::string m_Found;
	};
	SFindCtx Ctx;
	Ctx.m_apExts = WantVideo ? apVideoExts : apImageExts;
	Ctx.m_ExtCount = WantVideo ? (int)(sizeof(apVideoExts) / sizeof(apVideoExts[0])) : (int)(sizeof(apImageExts) / sizeof(apImageExts[0]));

	fs_listdir(
		Folder.c_str(),
		[](const char *pName, int IsDir, int DirType, void *pUser) -> int {
			(void)DirType;
			auto *pCtx = static_cast<SFindCtx *>(pUser);
			if(IsDir || !pCtx->m_Found.empty())
				return 0;
			char aExt[16];
			ToLowerExt(pName, aExt, sizeof(aExt));
			for(int i = 0; i < pCtx->m_ExtCount; ++i)
			{
				if(str_comp(aExt, pCtx->m_apExts[i]) == 0)
				{
					pCtx->m_Found = pName;
					break;
				}
			}
			return 0;
		},
		0, &Ctx);

	if(!Ctx.m_Found.empty())
		return JoinPath(Folder, Ctx.m_Found);
	return std::string();
}

void CWallpaperEngine::ScanRoot(const std::string &Root)
{
	// Two-phase scan.
	//
	// Phase 1 only collects the immediate sub-directory names. Phase 2 inspects
	// each one AFTER the directory iteration has finished. This matters because
	// inspecting an item needs its own fs_listdir (to find the media file), and
	// running a nested fs_listdir from inside a listdir callback re-enters the
	// storage backend while it is still iterating — a good way to crash or
	// produce garbage entries.
	struct SCollectCtx
	{
		std::vector<std::string> m_vFolders;
	};
	SCollectCtx Collect;
	fs_listdir(
		Root.c_str(),
		[](const char *pName, int IsDir, int DirType, void *pUser) -> int {
			(void)DirType;
			if(!IsDir)
				return 0;
			auto *pCtx = static_cast<SCollectCtx *>(pUser);
			pCtx->m_vFolders.emplace_back(pName);
			return 0;
		},
		0, &Collect);

	// Phase 2: now that iteration is done, inspect each folder.
	for(const std::string &Name : Collect.m_vFolders)
	{
		const std::string Folder = JoinPath(Root, Name);
		std::string Title;
		std::string File;
		EType Type = EType::UNKNOWN;
		if(!ReadProjectJson(Folder, Title, File, Type))
		{
			// No project.json: try to infer from the folder contents, so
			// orphaned/partially installed items still show up.
			const std::string Video = ResolveMedia(Folder, EType::VIDEO, "");
			if(!Video.empty())
			{
				Type = EType::VIDEO;
			}
			else
			{
				const std::string Image = ResolveMedia(Folder, EType::IMAGE, "");
				if(Image.empty())
					continue;
				Type = EType::IMAGE;
			}
			Title = Name;
		}

		const std::string Media = ResolveMedia(Folder, Type, File);
		if(Media.empty())
			continue; // scene/web/application or nothing playable

		SWallpaper Wallpaper;
		Wallpaper.m_Id = Name;
		Wallpaper.m_Title = Title;
		Wallpaper.m_Path = Folder;
		Wallpaper.m_Media = Media;
		Wallpaper.m_Type = Type;
		AddFound(Wallpaper);
	}
}

void CWallpaperEngine::Scan(bool Force)
{
	if(m_Scanned && !Force)
		return;
	m_Scanned = true;
	m_vWallpapers.clear();
	m_LastError.clear();

	// Candidate Wallpaper Engine install directories and Steam library roots.
	// The install directory is confirmed by wallpaper32.exe, and workshop items
	// live under <library>/steamapps/workshop/content/431960.
	static const char *apProbeDirs[] = {
		"C:/Program Files (x86)/Steam",
		"C:/Program Files/Steam",
		"D:/Steam",
		"D:/SteamLibrary",
		"E:/Steam",
		"E:/SteamLibrary",
		"F:/SteamLibrary",
	};

	bool FoundAny = false;
	for(const char *pSteamRoot : apProbeDirs)
	{
		const std::string SteamRoot = pSteamRoot;
		if(!DirExists(SteamRoot))
			continue;

		// Wallpaper Engine install: <steam>/steamapps/common/wallpaper_engine
		const std::string WeInstall = JoinPath(JoinPath(JoinPath(SteamRoot, "steamapps"), "common"), "wallpaper_engine");
		if(DirExists(WeInstall) && FileExists(JoinPath(WeInstall, "wallpaper32.exe")))
		{
			if(m_InstallDir.empty())
				m_InstallDir = WeInstall;
			ScanRoot(JoinPath(JoinPath(WeInstall, "projects"), "defaultprojects"));
			ScanRoot(JoinPath(JoinPath(WeInstall, "projects"), "myprojects"));
			FoundAny = true;
		}

		// Steam Workshop items for the Wallpaper Engine app id.
		const std::string Workshop = JoinPath(JoinPath(JoinPath(JoinPath(SteamRoot, "steamapps"), "workshop"), "content"), WE_APPID);
		if(DirExists(Workshop))
		{
			ScanRoot(Workshop);
			FoundAny = true;
		}
	}

	if(!FoundAny)
		m_LastError = "Wallpaper Engine / Steam workshop not found";
	else if(m_vWallpapers.empty())
		m_LastError = "No video or image wallpapers found";
}

void CWallpaperEngine::AddFound(const SWallpaper &Wallpaper)
{
	for(const SWallpaper &Existing : m_vWallpapers)
	{
		if(Existing.m_Media == Wallpaper.m_Media)
			return;
	}
	m_vWallpapers.push_back(Wallpaper);
}

const CWallpaperEngine::SWallpaper *CWallpaperEngine::Find(const char *pIdOrPath) const
{
	if(pIdOrPath == nullptr || pIdOrPath[0] == '\0')
		return nullptr;
	for(const SWallpaper &Wallpaper : m_vWallpapers)
	{
		if(str_comp(Wallpaper.m_Id.c_str(), pIdOrPath) == 0)
			return &Wallpaper;
		if(str_comp(Wallpaper.m_Path.c_str(), pIdOrPath) == 0)
			return &Wallpaper;
		if(str_comp(Wallpaper.m_Media.c_str(), pIdOrPath) == 0)
			return &Wallpaper;
	}
	return nullptr;
}
