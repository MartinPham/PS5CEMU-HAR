// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: Cemu's core, started and driven the way its desktop frontend does (src/main.cpp's
// CemuCommonInit, gui/wxgui's CemuApp::OnInit and MainWindow::FileLoad), without wxWidgets.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps5emu
{
	struct Game
	{
		uint64_t titleId = 0;		 // the base title
		std::string name;			 // from meta.xml
		std::filesystem::path path;	 // what Cemu launches
		uint16_t version = 0;		 // the base's, or the update's when installed
		bool hasUpdate = false;
		uint32_t dlcCount = 0;
		std::string region;
	};

	// Paths, settings (with PS5 defaults on first start), MLC, graphic packs, controllers and
	// Cemu's own initialisation. False, with a reason, when something essential is missing.
	bool InitializeCore(std::string& error);

	// The games Cemu found in the game files folder and the MLC, sorted by name.
	std::vector<Game> ListGames();

	// The Vulkan renderer on VideoOut, then the title. False, with a reason, when it cannot start.
	bool LaunchGame(const Game& game, std::string& error);

	// Runs while the game does, handling the port's shortcuts. Returns when the player asks for the
	// library; the caller then restarts the app (RestartToLibrary).
	void RunGame();

	// Starts PS5Cemu over (sceSystemServiceLoadExec on its own eboot), which shows the library with
	// the last game selected. Cemu cannot yet shut a game down and start another reliably in one
	// process (MainWindow::EndEmulation says so), so leaving a game means a fresh process; PS5SX2
	// goes back to its shelf the same way. Returns only if the restart did not take.
	void RestartToLibrary();
}
