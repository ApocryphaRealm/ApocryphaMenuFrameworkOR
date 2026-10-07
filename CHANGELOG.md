# Changelog - Apocrypha Menu Framework (Oblivion Remastered)

Newest first. Versions are issued by the version gate; a number here is one a build earned by working in game.

## 1.0.8 - 2026-10-07 - untested (built; Oblivion Remastered is not installed, so not run in game)

### Fixed
- **D-pad right on the Mods row skipped its own controls.** On the row with the alphabetical tick box, the A-Z / Z-A
  switch and the Sort button, right went straight across to the options pane instead of to the next control (the
  owner, 2026-10-07: "pressing D-pad right skips past the toggle and sort button and goes to the right pane"). A
  right press in the list pane is now decided one frame late, the way the options pane's sideways press already
  is: if it moved the highlight to a control beside it, it stays in the list pane; a press that moved nothing
  (on a menu entry, or on the row's last control) goes across to the options as before. Renderer.cpp:
  g_pendingSideRight.

## 1.0.7 - 2026-10-06 - untested (built; Oblivion Remastered is not installed, so not run in game)

A player's crash report (2026-10-06, AMF 1.0.6 with HUD Position Manager 1.0.2, UE4SS loaded): the game crashed about a
minute after the pause menu was closed, inside the SKSE MENUS row code, with no menu open.

### Fixed
- **A crash after closing the pause menu.** When the game's garbage collector condemned the System page, one check
  forgot the page while the scan for pages still found it, so the row was added again to a page whose widgets were
  already being freed. Pages the collector has condemned (marked unreachable or garbage, being destroyed, or renamed
  to None) are now skipped by the scan and by every kept reference alike, every step into the page's widgets is checked
  live before it is read, and the row's add is guarded so a fault skips that page instead of closing the game.
## 1.0.6 - 2026-10-02 - untested

### Changed
- **No title bar; the frame runs round all four sides** (the owner, 2026-10-02: *"do the same thing that AMF for Skyrim
  did by removing the top bar and extending, or rather connecting, the frame on all four sides"*). The collapse arrow is
  gone and the window no longer moves: without a title bar ImGui would let any empty part of the window drag it. Each
  way in keeps its own place. The key-opened window opens where it was last left; the System-row window opens on the
  right of the screen so the System page's rows stay in view. Both still resize from their edges.
- **Oblivion, this framework's own look, is the new default theme** (the owner: *"give it a frame art similar to how
  Skyrim has a frame art, except this frame will be more like an embroidered map's edge. Something a bit decorative in
  a gold or brown color"*).
  - The frame: an embroidered map's edge - a gold couched cord, a compass star in a stitched ring at each corner, and a
    brown running stitch.
  - The colours: parchment with brown ink.
  - The art is original, drawn from shapes by `tools/make_mapedge_frame.py`, so no game file ships.
  - The edges tile rather than stretch, so the stitches keep their size.
  - The frame scales with the UI: 26 px at 1080p, 43 px at 1800 px tall.
  - Skyrim's knotwork stays as the "Skyrim" theme, and every other theme is unchanged.
  - First built as "Cyrodiil Map", then renamed (the owner: *"just rename the current theme to Oblivion, and we can get
    rid of the old Oblivion paper theme"*). A saved `cyrodiil` or `oblivion-paper` theme id now opens as `oblivion`.

### Added
- **A Screenshot control that catches this menu: F11 on the keyboard, View on the controller** (the owner, 2026-10-02:
  *"I'd rather just have a hotkey that takes the same screenshot that yours does"*, then *"make it a rebindable option
  ... it can be something that only works with AMF open because regular Steam screenshot works in the game itself"*).
  - Steam's F12 copies the frame before this framework draws, so it shows the game without the menu.
  - This control copies what the screen shows, menu and all, and saves it as a PNG on a worker thread. A rising double
    beep says it was saved.
  - How and when the framework draws is unchanged.
  - It is one of the framework's own controls, rebindable on Controls with press-to-bind, and it works while the menu
    is open.
  - F11 rather than F12, since Steam would also save a picture without the menu.
  - View is shared only with the on-screen keyboard's Done; while that keyboard is up, the press goes to Done.
  - The pictures go to the game's `Data\AMF Screenshots`, which Mod Organizer 2 puts in its overwrite folder (the owner:
    *"have it kicked out through MO2 into the overwrite"*). `[Screenshot] sFolder` names another folder.
  - Two new strings in all eleven languages.

### Added - the Skyrim framework's 2.0 (the owner: *"update the Oblivion AMF to the most up-to-date version in line with
AMF for Skyrim. So it has the separators and all that"*)
- **Separators in the mod list**, like MO2's.
  - Y (or a right-click) on a mod gives *New separator above* and *Send to* (a separator, or No separator).
  - A separator folds and unfolds with A or a click, and shows a count when folded.
  - Separators can be renamed, deleted and favourited.
  - A-Z sorts the loose mods only.
- **Move to the top** goes to the top of the mod's own separator.
- **Reorder** in a mod's options opens a small up / down box. Each press moves the mod one place, and the box stays open.
- **Grab and move**: R3 picks up the highlighted mod and the right stick steps it up or down; R3 again, or B, puts it
  down. All three controls are rebindable on Controls. R3 is the target lock only in gameplay; the game takes no input
  while this menu is open (the owner: *"Obviously, you're not going to target lock while in the menu"*).
- **The side pane fits its names**, and the window widens once when the names and the page do not both fit. The
  System-row window widens leftwards, since it sits against the right edge.
- 18 new strings in all eleven languages, taken from the Skyrim framework's translations.
- amf.menu ops `separator` and `stick` for headless tests. The DevBench argument reader now matches keys only: a value
  equal to a key name had been read as that key.

### Not taken from Skyrim 2.0
- **One centred window for both ways in.** Oblivion keeps its System-row placement on the right.
- **The start-up curtain's splash shapes (2.0.2).** Oblivion's curtain is plain black, with no modlist splash to fit.

## 1.0.5 - 2026-09-30 - untested

### Added
- **The controller triggers reach the menus** (the owner, 2026-09-30: *"mirror the more up-to-date AMF for Skyrim with
  better controller navigation ... and any other convenient controller navigation that's in the Skyrim AMF"*). The
  Skyrim framework's 1.9.9 fix, for Oblivion Remastered's pad: XInput reports L2 and R2 as analog values, not buttons, so
  each now becomes a press past XInput's own threshold and arrives as ImGui's GamepadL2 / GamepadR2 - a mod's page can
  use them. While a mod's Rebind button is waiting, the triggers stay with that capture as before.
- **AMF_IsMenuOpen** (AMF::IsMenuOpen in sdk/include/AMF.h): true while the framework's window is up. A mod that
  reads the controller itself - Improved Wheel Menu reads the D-pad before the pad gate empties the game's reads - stands
  down on it, so the D-pad no longer opens its wheel while the menu is open.

### Fixed
- **A crash when the game rebuilt its menus** (the owner, 2026-09-30: Oblivion crashed equipping the second loadout of
  Simple Loadout System). The System row kept the System page, its own row and the row above it as raw pointers and
  asked each tick whether they were still alive by reading the object's own slot index - from memory the garbage
  collector had already freed (access violation in reflect::IsLive, called from systemrow::Tick). They are now kept as
  handles (the object array's slot, class and name recorded while live; the slot is asked first and the object read
  only while the array still holds it - HUD Position Manager's handle, logic library 8032), the pause code's camera
  manager and controller likewise, and IsLive itself reads the slot index under a fault guard.
- Every engine call that takes a world context (the System row's widget Create, the pause calls) runs fault-guarded
  and refuses a context that is being destroyed. Minimap Menu crashed on quitting to the menu when its world context
  outlived its world (2026-09-30); gate rule or-world-context-calls-are-guarded now refuses the pattern in every package.

### Checked against the Skyrim framework
- The rest of the Skyrim framework's controller navigation was already in this port (it was taken from Skyrim 1.9.8):
  L1 / R1 walk the tabs - a mod's own tab bar when its page declares one (AMF::DeclareInnerTabs), otherwise the
  framework's page bar - Y opens a mod's options, L3 favourites the highlighted mod, the D-pad never steps a tab, the
  on-screen keyboard, the thumbstick API. The bumpers reach a mod's inner tabs only when that mod declares them:
  HUD Position Manager does from its next build.

## 1.0.4 - 2026-09-29 - untested (the System row proven in game 12:5x; this build with the pause row hidden not yet run)

### Added
- The idle vanity camera never takes over while the window is open ([Menu] bKeepCameraAwake, on): the camera manager's own idle timer is stopped while the window is up and restarted when it closes, the way the game does after its pause menu, and a vanity camera already running is left. It used to rotate the view and hide the HUD under a HUD mod's page. A stopgap (the owner, 2026-09-29: "once AMF can properly stop time, there won't be a need for it to interact with the vanity camera") - to be removed when the pause works.
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
