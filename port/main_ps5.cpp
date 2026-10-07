// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the entry point (Cemu's src/main.cpp on the desktop).
//
//  1. out of the sandbox: /data, and JIT memory for the recompilers (ps5/privilege.h);
//  2. the boot log, the DualSense and Cemu's core (settings, MLC, graphic packs, the game scan);
//  3. the launcher until a game is chosen: the new one (frontend/shell.h), on the side last used, or
//     the classic one (frontend/launcher.h, its start screen choosing a side) when L1 was held as the
//     app started, ps5cemu.json's ui.classic asks for it, or the new one cannot draw;
//  4. the game, on Cemu's or Azahar's Vulkan renderer, until the in-game menu (touchpad + Options)
//     asks for the library, which starts PS5CEMU-HAR over (app/emulator.h, RestartToLibrary) on
//     that emulator's side.
// A home screen forwarder's --rom (app/forward.h, docs/FORWARDER.md) skips 3: its game starts at once,
// and --exit-after-game closes the app where 4 would start it over.

#include "app/boxart.h"
#include "app/emulator.h"
#include "app/forward.h"
#include "app/pack_updates.h"
#include "app/paths.h"
#include "app/updates.h"
#include "azahar/azahar.h"
#include "azahar/library.h"
#include "frontend/launcher.h"
#include "frontend/settings.h"
#include "frontend/shell.h"
#include "ps5/crash.h"
#include "ps5/display.h"
#include "ps5/kernel.h"
#include "ps5/log.h"
#include "ps5/notify.h"
#include "ps5/pad.h"
#include "ps5/privilege.h"
#include "ps5/threads.h"
#include "ps5/window.h"
#include <utility>

// The PS4 SDK's version record: its size is set by the caller (libkernel)
struct SceKernelSwVersion
{
	size_t size;
	char text[0x1c];
	uint32_t version;
};
extern "C" int32_t sceKernelGetSystemSwVersion(SceKernelSwVersion* version);

namespace
{
	// The console's firmware, as its settings show it ("11.60"), for the boot log and Diagnostics:
	// most reports hinge on it
	std::string Firmware()
	{
		SceKernelSwVersion version{};
		version.size = sizeof(version);
		if (sceKernelGetSystemSwVersion(&version) != 0)
			return "unknown";
		version.text[sizeof(version.text) - 1] = 0;
		return fmt::format("{} ({:#010x})", version.text, version.version);
	}

	std::vector<std::string> Diagnostics(const ps5privilege::Result& privileges)
	{
		return {
			fmt::format("PS5CEMU-HAR {}: Cemu at {}, Azahar at {}", PS5CEMU_VERSION, PS5CEMU_CEMU_COMMIT, PS5CEMU_AZAHAR_COMMIT),
			fmt::format("Firmware {}", Firmware()),
			privileges.summary,
			ps5log::Path()[0] ? fmt::format("Logs: {}, {}/log.txt", ps5log::Path(), ps5paths::kRoot) : "Boot log not written (/data is unreachable)",
		};
	}

	ps5emu::Options Options(const ps5settings::Launcher& settings)
	{
		return {settings.gamesFolder, settings.overlay, settings.volume, settings.upscaleFilter, settings.asyncShaders, settings.gamePadSpeaker};
	}

	// The side the launcher opens on when the app starts over (and only then), and why the game did
	// not start, when there is a reason.
	void RememberSide(const char* side, const std::string& launchError = {})
	{
		// the game's menu may have saved settings since the launcher's were read
		ps5settings::Launcher settings = ps5settings::Load();
		settings.side = side;
		settings.launchError = launchError;
		ps5settings::Save(settings);
	}

	// The 119.88 Hz mode for the next game, where the setting asks for it. The driver configures
	// VideoOut once a process, when a renderer first asks for the display's modes, and offers 119.88
	// Hz only to a title whose param.json declares high-frame-rate output. It reads /app0's unless
	// PS5_VIDEOOUT_PARAM_JSON names another, and a process the HEN has jailbroken has no /app0
	// (app/paths.h), so it is told where the app's is; with the setting off it is told of one that
	// is not there, which declares nothing, so the display stays at 59.94 Hz in a sandboxed process too.
	void SetHighFrameRate(bool highFrameRate)
	{
		ps5display::SetHighFrameRate(highFrameRate);
		const std::string paramJson = ps5paths::AppDir() + (highFrameRate ? "/sce_sys/param.json" : "/sce_sys/no-high-frame-rate.json");
		setenv("PS5_VIDEOOUT_PARAM_JSON", paramJson.c_str(), 1);
		// the new launcher opened VideoOut through the driver at 59.94 Hz: the game's rate asked for again
		ps5display::ConfigureOutput(highFrameRate);
	}
}

// What Cemu's src/main.cpp defines for the rest of Cemu.
std::atomic_bool g_isGPUInitFinished = false;
// Cemu's command line asks for a console window with some options; there is none on the PS5.
void requireConsole() {}

int main(int argc, char* argv[])
{
	ps5log::Line("[main] PS5CEMU-HAR {} starting (Cemu at {}, Azahar at {})", PS5CEMU_VERSION, PS5CEMU_CEMU_COMMIT, PS5CEMU_AZAHAR_COMMIT);
	// before any thread starts: the HEN jailbreaks the process as it is
	const ps5privilege::Result privileges = ps5privilege::Acquire();
	if (privileges.filesystem)
		ps5log::Open(ps5paths::kLogs);
	ps5log::Line("[main] firmware {}", Firmware());
	ps5log::Line("[main] {}", privileges.summary);
	ps5crash::Install(); // Cemu's own replaces it, in a session Cemu runs in
	{
		// how the console starts the CPU's floating point: desktop systems keep denormals (0x1f80);
		// flush-to-zero (bit 15) or denormals-are-zero (bit 6) would make Cemu's and Azahar's float
		// results differ from a desktop's
		uint32_t mxcsr = 0;
		asm volatile("stmxcsr %0" : "=m"(mxcsr));
		ps5log::Line("[main] MXCSR {:#06x}: flush-to-zero {}, denormals-are-zero {}", mxcsr, mxcsr & 0x8000 ? "on" : "off",
			mxcsr & 0x40 ? "on" : "off");
	}
	ps5threads::Initialize(); // before any thread starts: they inherit the main thread's CPUs

	ps5settings::Launcher settings = ps5settings::Load();
	// the update notice, on a fresh start only (not each time a game hands back to the library)
	const bool freshStart = settings.side.empty();
	// A forwarder's game (--rom): a relative path is looked for in its side's game files folder (both,
	// the Wii U's first, when neither --type nor its extension says which). Every restart of the app
	// (RestartToLibrary) runs without arguments, so the game starts once.
	const ps5forward::Args forward = ps5forward::Parse(argc, argv);
	ps5forward::Type forwardType = forward.type;
	if (forwardType == ps5forward::Type::Unknown && forward.typeText.empty())
		forwardType = ps5forward::IsArtic(forward.rom) ? ps5forward::Type::N3ds : ps5forward::TypeFromExtension(forward.rom);
	// where to look, each with the side it means: an absolute path once (its side as known so far), a
	// relative one in each side's folder that can be its
	std::vector<std::pair<std::string, ps5forward::Type>> forwardCandidates;
	if (!forward.rom.empty())
	{
		ps5log::Line("[forward] --rom {} --type {}{}", forward.rom, forward.typeText.empty() ? "(not given)" : forward.typeText,
			forward.exitAfterGame ? " --exit-after-game" : "");
		if (forward.rom.front() == '/' || ps5forward::IsArtic(forward.rom))
			forwardCandidates.emplace_back(ps5forward::Resolve(forward.rom, ""), forwardType);
		else
		{
			if (forwardType != ps5forward::Type::N3ds)
				forwardCandidates.emplace_back(ps5forward::Resolve(forward.rom, settings.gamesFolder), ps5forward::Type::WiiU);
			if (forwardType != ps5forward::Type::WiiU)
				forwardCandidates.emplace_back(ps5forward::Resolve(forward.rom, settings.n3ds.gamesFolder), ps5forward::Type::N3ds);
		}
		std::erase_if(forwardCandidates, [](const auto& candidate) { return candidate.first.empty(); });
	}
	// still before any thread: the game folders and drives the HEN may have left out (#16), and the
	// forwarded game's
	if (privileges.filesystem)
	{
		std::vector<std::string> folders = {settings.gamesFolder, settings.n3ds.gamesFolder};
		for (const auto& [path, type] : forwardCandidates)
			if (const std::string folder = ps5forward::Folder(path); !folder.empty())
				folders.push_back(folder);
		ps5privilege::ReachFolders(folders);
	}
	// the forwarded game's file, once the folders can be read: its path and its emulator, or why not
	std::string forwardPath, forwardError;
	if (!forward.rom.empty())
	{
		if (!forward.typeText.empty() && forward.type == ps5forward::Type::Unknown)
			forwardError = fmt::format("--type {} is neither wiiu nor 3ds", forward.typeText);
		else if (forwardCandidates.empty())
			forwardError = fmt::format("{} is not a path in the game files folder (no \"..\")", forward.rom);
		else
		{
			std::error_code ec;
			for (const auto& [path, side] : forwardCandidates)
			{
				const bool artic = ps5forward::IsArtic(path);
				if (!artic && !std::filesystem::exists(path, ec))
					continue;
				const bool folder = !artic && std::filesystem::is_directory(path, ec);
				// a folder is a Wii U game's (code/content/meta); an absolute file whose extension both
				// emulators take (.elf), or none, needs --type
				ps5forward::Type type = side;
				if (type == ps5forward::Type::Unknown && folder)
					type = ps5forward::Type::WiiU;
				if (type == ps5forward::Type::Unknown)
				{
					forwardError = fmt::format("{}: say --type wiiu or --type 3ds for this file", path);
					break;
				}
				if (folder && type == ps5forward::Type::N3ds)
				{
					forwardError = fmt::format("{} is a folder, not a 3DS game", path);
					break;
				}
				forwardPath = path;
				forwardType = type;
				break;
			}
			if (forwardPath.empty() && forwardError.empty())
				forwardError = fmt::format("{} was not found", forwardCandidates[0].first);
		}
		if (!privileges.filesystem)
			forwardError.clear(); // the notice below says why nothing can be read
		ps5log::Line("[forward] {}", forwardPath.empty() ? forwardError.empty() ? "no /data: the launcher instead" : forwardError :
																fmt::format("{} on the {} side", forwardPath, ps5forward::TypeName(forwardType)));
	}
	ps5threads::SetPinning(settings.pinCpuThreads);
	ps5log::ForwardDriverMessages();
	// before either emulator's Vulkan driver starts, which reads it once
	if (!settings.radvDebug.empty())
	{
		setenv("RADV_DEBUG", settings.radvDebug.c_str(), 1);
		ps5log::Line("[vulkan] RADV_DEBUG={} (radvDebug in ps5cemu.json)", settings.radvDebug);
	}
	for (const auto& [name, value] : settings.radvEnvironment)
	{
		// the driver's own variables only: the rest of the environment is the app's
		const bool driver = name.rfind("RADV_", 0) == 0 || name.rfind("MESA_", 0) == 0 || name.rfind("ACO_", 0) == 0;
		if (!driver || name == "RADV_DEBUG")
		{
			ps5log::Line("[vulkan] {} in radvEnvironment left out: only RADV_, MESA_ and ACO_ names, and radvDebug for RADV_DEBUG", name);
			continue;
		}
		setenv(name.c_str(), value.c_str(), 1);
		ps5log::Line("[vulkan] {}={} (radvEnvironment in ps5cemu.json)", name, value);
	}
	ps5emu::SetSubmitDraws(settings.cemuSubmitDraws);
	ps5pad::Init();
	ps5pad::SetVibrationEnabled(settings.rumble);
	ps5window::Initialize();

	ps5launcher::Status status;
	status.diagnostics = Diagnostics(ps5privilege::Current());
	std::string error;
	if (!privileges.filesystem)
	{
		status.notice = status.notice3ds =
			"PS5CEMU-HAR cannot reach /data. Load a HEN with PPSA99360 in its app jailbreak list, or elfldr, then restart PS5CEMU-HAR.";
		ps5log::Line("[main] {}", status.notice);
		ps5notify::Send(status.notice);
	}
	else if (!forwardError.empty())
	{
		// the forwarder's game could not be found: the launcher opens on its side (the Wii U's when
		// unknown) and says why
		const bool n3ds = forwardType == ps5forward::Type::N3ds;
		(n3ds ? status.notice3ds : status.notice) = "Forwarded game not found: " + forwardError;
		settings.side = n3ds ? "3ds" : "wiiu";
		ps5notify::Send("Forwarded game not found: " + forwardError);
	}
	else if (!settings.launchError.empty())
	{
		// the last game failed after its renderer started, and the process was started over to show it
		(settings.side == "3ds" ? status.notice3ds : status.notice) = "The game could not start: " + settings.launchError;
		settings.launchError.clear();
		ps5settings::Save(settings);
	}

	// One emulator per session: neither starts until the start screen's choice (or the side the last
	// game was on), and leaving that side starts the app over. Cemu's core (its guest memory, system
	// threads, crash handler, graphic packs and game scan) runs only for the Wii U; Azahar (its game
	// scan, and its core once a game starts) only for the 3DS.
	// Each time the launcher moves to a side (a game that did not start brings the launcher back).
	// Either side gives way to the other in the same process: Azahar's core and Cemu's emulated
	// Wii U both start only for a game, so until then only their game lists and settings are loaded.
	std::optional<ps5launcher::System> started;
	const size_t sessionLine = status.diagnostics.size();
	auto prepare = [&](ps5launcher::System system) {
		if (started == system)
			return;
		started = system;
		status.diagnostics.resize(sessionLine);
		if (system == ps5launcher::System::N3ds)
		{
			ps5log::Line("[main] the 3DS side: Cemu's emulated Wii U is not started");
			status.diagnostics.push_back("This session: Azahar (3DS); Cemu's emulated Wii U is not started");
			ps5azahar::StartScan(settings.n3ds.gamesFolder);
			ps5emu::LogMemory(); // the 3DS side's start, against Cemu's
			return;
		}
		ps5log::Line("[main] the Wii U side: Azahar's core is not started");
		status.diagnostics.push_back("This session: Cemu (Wii U); Azahar's core is not started");
		if (!privileges.filesystem)
			return;
		std::string coreError;
		if (!ps5emu::InitializeCore(coreError))
		{
			status.notice = "Cemu did not start: " + coreError;
			ps5log::Line("[main] {}", status.notice);
			ps5notify::Send(status.notice);
			return;
		}
		status.coreReady = true;
		ps5emu::ApplyOptions(Options(settings));
		if (!privileges.jit)
			ps5notify::Send("No JIT memory: Wii U games run on the interpreter, much slower. Is PPSA99360 in your HEN's app jailbreak list?");
		ps5emu::LogMemory(); // Cemu's start, against the 3DS side's
	};

	// no update check behind a forwarded game, which starts at once
	if (freshStart && privileges.filesystem && forwardPath.empty())
		ps5update::Start();
	bool forwardPending = !forwardPath.empty();
	bool forwarded = false; // the game now starting is the forwarder's
	// the new launcher, unless L1 is held now (or ui.classic says so from an earlier start)
	bool shell = ps5shell::Wanted(settings);
	ps5log::Line("[main] the {} launcher", shell ? "new" : "classic");
	for (;;)
	{
		SetHighFrameRate(false); // the launcher at 59.94 Hz
		ps5display::SetFramePacing(1); // and the 3DS's games every refresh
		std::optional<ps5launcher::Choice> choice;
		forwarded = false;
		if (std::exchange(forwardPending, false))
		{
			// the forwarder's game, as the launcher would hand it over: its side prepared, the background
			// work stopped and the side's scan finished first (frontend/launcher.cpp StopBackgroundWork)
			const ps5launcher::System system = forwardType == ps5forward::Type::N3ds ? ps5launcher::System::N3ds : ps5launcher::System::WiiU;
			prepare(system);
			if (system == ps5launcher::System::N3ds || status.coreReady)
			{
				ps5boxart::Stop();
				ps5packs::Stop();
				for (int waited = 0; system == ps5launcher::System::N3ds ? ps5azahar::Scanning() : ps5emu::Scanning(); waited++)
				{
					if (waited == 0)
						ps5log::Line("[forward] waiting for the library's scan to finish");
					sceKernelUsleep(16000);
				}
				ps5emu::Game game = system == ps5launcher::System::N3ds ? ps5azahar::GameAt(forwardPath) : ps5emu::GameAt(forwardPath);
				if (game.titleId != 0)
				{
					// the library's last and recent games, as a launch from it records them
					if (system == ps5launcher::System::N3ds)
						ps5settings::AddRecent(settings.n3ds.lastGame, settings.n3ds.recent, game.titleId);
					else
						ps5settings::AddRecent(settings, game.titleId);
					ps5settings::Save(settings);
				}
				ps5log::Line("[forward] starting {} ({})", game.name, game.format);
				choice = ps5launcher::Choice{system, std::move(game)};
				forwarded = true;
			}
			// else Cemu did not start: the launcher shows why (prepare set the notice)
		}
		if (!choice && shell)
		{
			switch (ps5shell::Run(settings, status, prepare, choice))
			{
			case ps5shell::Outcome::Chosen: break;
			case ps5shell::Outcome::Classic:
				// nothing of it reached VideoOut: the classic launcher in its place, this time
				ps5log::Line("[main] the new launcher could not start: the classic one instead");
				ps5notify::Send("The new launcher could not start, so the classic one is open. The boot log says why.");
				shell = false;
				choice = ps5launcher::Run(settings, status, prepare);
				break;
			case ps5shell::Outcome::Restart:
			case ps5shell::Outcome::Failed:
				// VideoOut was taken: the classic launcher from a fresh process, and from now on (holding
				// R1 as the app starts brings the new one back)
				settings = ps5settings::Load(); // the launcher saved what it changed
				settings.ui.classic = true;
				if (ps5settings::Save(settings))
				{
					ps5log::Line("[main] the new launcher stopped drawing: starting over in the classic one");
					ps5notify::Send("The new launcher stopped drawing: PS5CEMU-HAR starts again with the classic one. Hold R1 as it starts "
									"to try the new one again.");
					ps5emu::RestartToLibrary();
					return 1;
				}
				// without /data a fresh process could not know to open the classic one: no restarting in a loop
				ps5log::Line("[main] the new launcher stopped drawing, and /data cannot keep the classic one for the next start");
				ps5notify::Send("The new launcher stopped drawing. Close PS5CEMU-HAR, then hold L1 as it starts for the classic one.");
				for (;;)
					sceKernelUsleep(1000000);
			}
		}
		else if (!choice)
			choice = ps5launcher::Run(settings, status, prepare);
		ps5update::Stop(); // nothing of the launcher's runs beside a game
		ps5packs::Stop();
		if (!choice)
		{
			// nothing to show it on: wait for the player to close the app from the PS5's menu
			for (;;)
				sceKernelUsleep(1000000);
		}
		const ps5emu::Game& game = choice->game;

		if (choice->system == ps5launcher::System::N3ds)
		{
			if (ps5azahar::LaunchGame(game, settings.n3ds, error))
			{
				ps5azahar::RunGame();
				RememberSide("3ds");
				if (forwarded && forward.exitAfterGame)
					ps5emu::ExitApp(); // --exit-after-game: to the home screen, not the library
				ps5emu::RestartToLibrary();
				return 0;
			}
			ps5log::Line("[main] {} did not start: {}", game.name, error);
			if (ps5azahar::CoreTouched())
			{
				// Azahar's core is half started: show why from a fresh process
				RememberSide("3ds", error);
				ps5emu::RestartToLibrary();
				return 1;
			}
			status.notice3ds = "The game could not start: " + error;
			settings.side = "3ds"; // the launcher shows why on Azahar's side
			continue;
		}

		ps5emu::ApplyOptions(Options(settings));
		SetHighFrameRate(settings.highFrameRate);
		ps5display::SetFramePacing(settings.framePacing);
		ps5window::Initialize();
		if (ps5emu::LaunchGame(game, error))
		{
			ps5emu::RunGame();
			RememberSide("wiiu");
			if (forwarded && forward.exitAfterGame)
				ps5emu::ExitApp(); // --exit-after-game: to the home screen, not the library
			ps5emu::RestartToLibrary();
			return 0;
		}
		ps5log::Line("[main] {} did not start: {}", game.name, error);
		if (!ps5emu::RendererStarted())
		{
			status.notice = "The game could not start: " + error;
			settings.side = "wiiu";
			continue;
		}
		// Cemu's renderer holds VideoOut: show the reason from a fresh process
		RememberSide("wiiu", error);
		ps5emu::RestartToLibrary();
		return 1;
	}
}
