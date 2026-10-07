// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: launch arguments from a home screen forwarder, after ProsperoEden's
// (blackbearreloaded/ProsperoEden#13). A forwarder is a small app with its own home screen tile that
// starts PPSA99360 with
//
//   --rom <file>        the game: an absolute path, or one inside that side's game files folder
//   --type <wiiu|3ds>   which emulator: Cemu (Wii U) or Azahar (3DS); without it, the file's extension
//                       decides where it can (docs/FORWARDER.md)
//   --exit-after-game   when that game goes back to the library, close PS5CEMU-HAR instead
//
// and main_ps5.cpp starts that game without the launcher. Header only and standard C++ only, so
// tools/forward-check.cpp checks it on a PC.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace ps5forward
{
	enum class Type
	{
		Unknown, // neither --type nor an extension that says
		WiiU,	 // Cemu
		N3ds,	 // Azahar
	};

	struct Args
	{
		std::string rom;			// --rom <file> or --rom=<file>; empty when not given
		std::string typeText;		// --type's value as given, for the log
		Type type = Type::Unknown;	// from --type; Unknown when not given or not wiiu/3ds
		bool exitAfterGame = false; // --exit-after-game
	};

	inline std::string Lower(std::string_view text)
	{
		std::string lower(text);
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return lower;
	}

	// "wiiu", "3ds" (and the obvious spellings of each); Unknown for anything else.
	inline Type ParseType(std::string_view text)
	{
		const std::string lower = Lower(text);
		if (lower == "wiiu" || lower == "wii-u" || lower == "wii_u" || lower == "cemu")
			return Type::WiiU;
		if (lower == "3ds" || lower == "n3ds" || lower == "azahar")
			return Type::N3ds;
		return Type::Unknown;
	}

	// Unknown arguments are ignored.
	inline Args Parse(int argc, char** argv)
	{
		Args result;
		if (argc <= 0 || argv == nullptr)
			return result;
		const auto value = [&](int& i, std::string_view arg, std::string_view flag, std::string& out) {
			const std::string prefix = std::string(flag) + "=";
			if (arg == flag)
			{
				if (i + 1 < argc && argv[i + 1] != nullptr)
					out = argv[++i];
				return true;
			}
			if (arg.starts_with(prefix))
			{
				out = std::string(arg.substr(prefix.size()));
				return true;
			}
			return false;
		};
		for (int i = 0; i < argc; ++i)
		{
			if (argv[i] == nullptr)
				break;
			const std::string_view arg{argv[i]};
			if (arg == "--exit-after-game")
				result.exitAfterGame = true;
			else if (value(i, arg, "--rom", result.rom))
				continue;
			else if (value(i, arg, "--type", result.typeText))
				result.type = ParseType(result.typeText);
		}
		return result;
	}

	// The emulator a file's extension belongs to, where only one of them takes it: Cemu's formats
	// (app/emulator.cpp ListGames), and Azahar's the 3DS library scans for (azahar/library.cpp
	// Known). .elf is both's, and a Wii U game in its folder format has no extension: --type decides.
	inline Type TypeFromExtension(std::string_view path)
	{
		const size_t slash = path.find_last_of('/');
		const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
		const size_t dot = name.find_last_of('.');
		if (dot == std::string_view::npos || dot == 0)
			return Type::Unknown;
		const std::string extension = Lower(name.substr(dot));
		for (const char* wiiu : {".wua", ".wud", ".wux", ".rpx", ".wuhb"})
			if (extension == wiiu)
				return Type::WiiU;
		for (const char* n3ds : {".3ds", ".cci", ".cxi", ".cia", ".3dsx", ".app", ".axf", ".z3ds", ".zcci", ".zcxi", ".z3dsx"})
			if (extension == n3ds)
				return Type::N3ds;
		return Type::Unknown;
	}

	// Azahar's Artic Base addresses (azahar/azahar.h IsArtic): not files, passed on as they are.
	inline bool IsArtic(std::string_view rom)
	{
		return rom.starts_with("articbase://") || rom.starts_with("articinio://") || rom.starts_with("articinin://");
	}

	// The path to start: an absolute one (or an Artic Base address) as it is; a relative one inside
	// gamesFolder, never above it: "" for a path with a ".." part.
	inline std::string Resolve(std::string_view rom, std::string_view gamesFolder)
	{
		while (!rom.empty() && (rom.back() == ' ' || rom.back() == '\r' || rom.back() == '\n'))
			rom.remove_suffix(1);
		while (rom.size() > 1 && rom.back() == '/')
			rom.remove_suffix(1); // a Wii U folder given as "Game/"
		if (rom.empty())
			return {};
		if (rom.front() == '/' || IsArtic(rom))
			return std::string(rom);
		for (std::string_view rest = rom; !rest.empty();)
		{
			const size_t slash = rest.find('/');
			if (rest.substr(0, slash) == "..")
				return {};
			if (slash == std::string_view::npos)
				break;
			rest.remove_prefix(slash + 1);
		}
		std::string path(gamesFolder);
		if (!path.empty() && path.back() != '/')
			path += '/';
		path += rom;
		return path;
	}

	// The folder a resolved path is in, for ps5privilege::ReachFolders ("" for an Artic address).
	inline std::string Folder(std::string_view path)
	{
		if (path.empty() || path.front() != '/')
			return {};
		const size_t slash = path.find_last_of('/');
		return slash == 0 ? std::string("/") : std::string(path.substr(0, slash));
	}

	inline const char* TypeName(Type type)
	{
		return type == Type::WiiU ? "wiiu" : type == Type::N3ds ? "3ds" : "unknown";
	}
}
