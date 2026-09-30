# Changelog - Apocrypha Menu Framework (Oblivion Remastered)

Newest first. Versions are issued by the version gate; a number here is one a build earned by working in game.

## 1.0.4 - 2026-09-29 - untested (the System row proven in game 12:5x; this build with the pause row hidden not yet run)

### Added
- The idle vanity camera never takes over while the window is open ([Menu] bKeepCameraAwake, on): the camera manager's own idle timer is stopped while the window is up and restarted when it closes, the way the game does after its pause menu, and a vanity camera already running is left. It used to rotate the view and hide the HUD under a HUD mod's page.
- A row in the game's own System page: "Apocrypha Menu Framework", under Save, Load and Quit, reached with the D-pad like the game's rows and opening the framework when pressed. The row is created in the live page from the same widget class as the game's rows (its look and sound are the game's), added to the page's panel with the last row's layout, and spliced into the rows' controller navigation; nothing bound on the page is touched and no game file is replaced, so it works with any menu artwork. The "Mod settings in the game's System menu" switch on the Settings page turns it off (Menus.bSystemMenuRow; a change takes effect at the next launch). Opened from the row, the window sits on the right of the screen so the page's rows stay in view; drag it and the position is remembered for that way in, as before.
- A per-frame game-thread tick (the message pump's PeekMessageW import, chained) and the reflection helpers the row needs (ProcessEvent watch by class through a vtable-slot swap, property offsets by name), brought over from Tween Menu for Oblivion Remastered.

### Not in this version
- Pausing the game while the menu is open. It was wired through the engine's GameplayStatics::SetGamePaused (Pause.cpp) and the call went through, but the world kept running; the owner had it left out of 1.0.4 ("finalize AMF in its current form without the time stop feature for now because it doesn't currently work"). [Menu] bPauseGame is still read and kept, and does nothing.

## 1.0.3 - 2026-09-29 - working

### Fixed
- Closing the menu now cancels a mod's bind-button capture that is still waiting. Before, a keyboard-side capture left armed by closing the menu from the controller took the next key or click in the game (within the mod's timeout, 8 s for Ultimate Combat Redux) as the binding and hid that press from the game. The mod's next poll reports cancelled, so its Rebind button simply comes back.

## 1.0.2 - 2026-09-29 - working

### Added
- Key capture for other mods' bind buttons: AMF_BeginKeyCapture, AMF_PollKeyCapture and AMF_CancelKeyCapture (in sdk/include/AMF.h as AMF::BeginKeyCapture, PollKeyCapture, CancelKeyCapture and HasKeyCapture). A mod's Rebind button arms the capture and the next press becomes the binding - any key, mouse button, mouse wheel, controller button, trigger or stick direction. The press is swallowed: the menu does not navigate on it and the game never sees it, so B and A can be bound on a controller without backing out of the page. Esc cancels a keyboard capture; a timeout ends either side. Ultimate Combat Redux's Rebind buttons use it.

### Fixed
- The DLL no longer carries the build machine's folder paths (rule 45): /d1trimfile strips the project folder from __FILE__ and std::source_location, including CommonLibOB64's OBSE/Interfaces.h reached through the precompiled header, and /PDBALTPATH records only the PDB's file name. 1.0.1 carried both paths.

## 1.0.1 - 2026-09-26

First release. (0.1.0 was the first working build of the night; the owner set the release number to 1.0.1.)

The Skyrim Apocrypha Menu Framework brought to The Elder Scrolls IV: Oblivion Remastered as an OBSE64 plugin.

Added
- The framework window over the game's DirectX 12 renderer: the DXGI factory is hooked before the game's WinMain, the
  swap chain and its presenting command queue are taken as the game creates them, and Dear ImGui 1.90.8 (docking)
  draws on every Present.
- The Skyrim core, unchanged where the game did not force a change: the Mod Control Panel (side list, content pane,
  tabs), the six themes and theme files with their art, the knotwork frame, the TrueType font atlas, the Controls page
  with rebinding, the Help pages, the on-screen keyboard, personalization (aliases, order, favourites), per-save state,
  the eleven translations, the hang watchdog and fast exit.
- Input from the game window's messages (keyboard, mouse) and a per-frame XInput read (controller), feeding the same
  decision and record queue the Skyrim engine hook fed; the game sees none of it while the menu is open, and a key
  held across the opening still gets its release.
- The pad gate: the game's own XInput reads are routed through the framework and answered with an empty pad while
  the menu is open (and until every button is up after it closes), so A on a menu entry cannot also fire in the game.
- The public API: `sdk/include/AMF.h`, a header-only, dependency-free way for other mods to add pages - safe without
  the framework, C++ Dear ImGui through the shared context (`AMF_GetImGuiContext`, `AMF_GetImGuiAllocatorFunctions`,
  `AMF_CheckImGuiABI`) or the cimgui `ig*` exports - and `sdk/example`, a complete mod built against it.
- The C API and the complete cimgui 1.90.8dock export surface, so a mod written for the Skyrim framework registers
  and draws the same way.
- The driving tools `amf.menu`, `amf.process` and `amf.keybind`, registered with TestBench when it is present.
- Settings, themes, fonts and translations live beside the plugin under `OBSE\Plugins\ApocryphaMenuFramework`; the log
  is `Documents\My Games\Oblivion Remastered\OBSE\Logs\ApocryphaMenuFramework.log`, at info by default.

Not yet (see PLAN.md, M3)
- Opening from the game's pause menu; pausing the game while the menu is open; a controller button that opens the
  menu; the game's HUD opacity; the startup curtain. Their settings rows are not drawn until each is wired.
- In-process screen capture (the driving tool's `capture` op) on D3D12.

Fixed during the night's testing
- The controller was dead in the game for the whole session: the framework loaded xinput1_4.dll at plugin load, before
  the game and Steam had set up their controller path. XInput is now resolved when the menu first opens, through the
  game's own import.
- Theme art (frame, background) did not load: the path still had Skyrim's `Data\` root. Two gate rules now refuse a
  Skyrim root in an Oblivion repository's sources and shipped text.
- The first D-pad press after opening only selected the list pane; navigation now starts on the open entry, and the
  press that switches to controller mode counts.
- A second mouse pointer over the menu (Unreal re-setting its arrow) and the menu's cursor jumping from the centre
  on the first movement.
