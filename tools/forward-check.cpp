// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: a check of the forwarder's launch arguments (port/app/forward.h) on a PC.
//   clang++-18 -std=c++20 -Wall -Wextra -Iport/app -o /tmp/forward-check tools/forward-check.cpp && /tmp/forward-check
#include "forward.h"

#include <cstdio>
#include <string>

using namespace ps5forward;

static int s_failures = 0;
#define CHECK(cond)                                                \
	do                                                             \
	{                                                              \
		if (!(cond))                                               \
		{                                                          \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			s_failures++;                                          \
		}                                                          \
	} while (0)

template<size_t N>
static Args ParseOf(const char* (&argv)[N])
{
	return Parse(static_cast<int>(N), const_cast<char**>(argv));
}

int main()
{
	{
		const char* argv[] = {"eboot.bin", "--rom", "Zelda BotW.wua", "--type", "wiiu", "--exit-after-game"};
		const Args a = ParseOf(argv);
		CHECK(a.rom == "Zelda BotW.wua");
		CHECK(a.type == Type::WiiU);
		CHECK(a.typeText == "wiiu");
		CHECK(a.exitAfterGame);
	}
	{
		const char* argv[] = {"eboot.bin", "--type=3DS", "--rom=/mnt/usb0/3ds/Pokemon.3ds"};
		const Args a = ParseOf(argv);
		CHECK(a.rom == "/mnt/usb0/3ds/Pokemon.3ds");
		CHECK(a.type == Type::N3ds);
		CHECK(!a.exitAfterGame);
	}
	{
		const char* argv[] = {"eboot.bin", "--type", "gamecube", "--rom"}; // a bad type; --rom with nothing after it
		const Args a = ParseOf(argv);
		CHECK(a.type == Type::Unknown);
		CHECK(a.typeText == "gamecube");
		CHECK(a.rom.empty());
	}
	CHECK(Parse(0, nullptr).rom.empty());

	CHECK(ParseType("WiiU") == Type::WiiU && ParseType("wii-u") == Type::WiiU && ParseType("3ds") == Type::N3ds);
	CHECK(ParseType("") == Type::Unknown && ParseType("ps2") == Type::Unknown);

	CHECK(TypeFromExtension("/data/ps5cemu/games/Mario Kart 8.WUA") == Type::WiiU);
	CHECK(TypeFromExtension("game.wux") == Type::WiiU && TypeFromExtension("app.rpx") == Type::WiiU);
	CHECK(TypeFromExtension("Pokemon.3ds") == Type::N3ds && TypeFromExtension("x.cia") == Type::N3ds);
	CHECK(TypeFromExtension("homebrew.3dsx") == Type::N3ds && TypeFromExtension("a.z3ds") == Type::N3ds);
	CHECK(TypeFromExtension("both.elf") == Type::Unknown);								 // Cemu's and Azahar's
	CHECK(TypeFromExtension("/data/ps5cemu/games/Splatoon [AGME01]") == Type::Unknown); // a folder-format game
	CHECK(TypeFromExtension("/data/my.games/Splatoon") == Type::Unknown);				 // a dot in a folder only
	CHECK(TypeFromExtension(".hidden") == Type::Unknown);

	CHECK(Resolve("Zelda.wua", "/data/ps5cemu/games") == "/data/ps5cemu/games/Zelda.wua");
	CHECK(Resolve("RPG/Zelda.wua\r\n", "/data/ps5cemu/games/") == "/data/ps5cemu/games/RPG/Zelda.wua");
	CHECK(Resolve("Splatoon/", "/g") == "/g/Splatoon");
	CHECK(Resolve("/mnt/usb0/Zelda.wua", "/g") == "/mnt/usb0/Zelda.wua");
	CHECK(Resolve("articbase://192.168.1.20", "/g") == "articbase://192.168.1.20");
	CHECK(Resolve("../Zelda.wua", "/g").empty());
	CHECK(Resolve("RPG/../../x.3ds", "/g").empty());
	CHECK(Resolve("RPG/..x.3ds", "/g") == "/g/RPG/..x.3ds");
	CHECK(Resolve("", "/g").empty());

	CHECK(Folder("/mnt/usb0/3ds/Pokemon.3ds") == "/mnt/usb0/3ds");
	CHECK(Folder("/Zelda.wua") == "/");
	CHECK(Folder("articbase://192.168.1.20").empty());
	CHECK(IsArtic("articinio://") && !IsArtic("/articbase://"));

	std::printf(s_failures ? "%d failed\n" : "all passed\n", s_failures);
	return s_failures ? 1 : 0;
}
