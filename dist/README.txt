ApocryphaRealm Menu Framework - Oblivion Remastered
===================================================
Version 1.0.7

An original, GPL-3.0-or-later in-game menu framework (embedding Dear ImGui) for The Elder
Scrolls IV: Oblivion Remastered, loaded by OBSE64. It is the same framework as Apocrypha Menu
Framework for Skyrim - the same menu, themes, controls and mod API - drawn over Oblivion
Remastered's DirectX 12 renderer.

WHAT YOU GET
------------
  * F1 opens the menu in a window of its own; F1 or Escape closes it. The key can be rebound
    on the Controls page. The window has no title bar - its frame runs round all four sides -
    and it opens in the same place each time; drag an edge or a corner to resize it.
  * The Mod Control Panel: a side list (Framework Settings / Controls / Help, then every
    registered mod) with a content pane for the selected mod's settings pages. The side list
    widens to fit its names.
  * Separators in the mod list, like Mod Organizer 2's: Y (or right-click) on a mod for New
    separator above and Send to; A folds a separator. Move to the top, Reorder (an up / down
    box) and grab-and-move (R3 picks a mod up, the right stick moves it) set the order.
  * Seven themes - Oblivion (the default: an embroidered map's edge in gold and brown on
    parchment), Skyrim (the knotwork frame), Untarnished (clean lines, no frame art), and
    Vel'dun, Oathvein, Norden and Norden - Black - plus a font picker (drop a .ttf into
    OBSE/Plugins/ApocryphaMenuFramework/fonts) and a text-size slider.
  * A Screenshot control (F11, or View on a controller, while the menu is open) that saves the
    screen WITH the menu on it - Steam's F12 leaves the menu out. The pictures go to the game's
    Data\AMF Screenshots; under Mod Organizer 2 they land in the overwrite folder.
  * Mouse, keyboard and controller. The menu follows whatever you last used; there is nothing
    to switch on.
  * While the menu is open the game does not see your keys, mouse or clicks, so nothing you do
    in the menu also happens in the game.
  * Rebind buttons on other mods' pages: press Rebind, then the key, mouse button, mouse wheel,
    controller button, trigger or stick direction you want. That press only sets the binding -
    the menu and the game ignore it, so B and A can be bound too. Esc cancels on the keyboard.
  * A row in the game's own System page - "Apocrypha Menu Framework", under Save, Load and
    Quit - reached with the D-pad like the game's rows and opening this menu. It is added to the
    page as it opens, not by replacing a game file, so it works with any menu artwork; the
    "Mod settings in the game's System menu" switch turns it off (takes effect at the next launch).
    Opened from that row, the window sits on the right so the page's rows stay in view.
  * The idle vanity camera never takes over while the menu is open: its timer is held while
    the window is up and runs again when it closes ([Menu] bKeepCameraAwake, on).

USING IT WITH A CONTROLLER
--------------------------
  * The D-pad and the left stick move through the list and across into the options.
  * A takes hold of a slider; the RIGHT stick then moves it. A again lets go.
  * B cancels, START closes the menu.
  * Y opens a mod's options, L3 favourites it, R3 picks it up to move it with the right stick.
  * View takes a screenshot (rebindable on Controls).

NOT YET IN THIS VERSION
-----------------------
  * Pausing the game while the menu is open (the setting is read but does nothing yet).
  * A controller button that opens the menu (F1 only for now).
  * The game's HUD opacity setting is not read; the menu is drawn fully opaque.

REQUIREMENTS
------------
  * The Elder Scrolls IV: Oblivion Remastered (Steam, runtime 1.512.105)
  * OBSE64 (Oblivion Script Extender 64)
  * Address Library for OBSE Plugins

INSTALLING
----------
The plugin goes beside the game executable, in
OblivionRemastered\Binaries\Win64\OBSE\Plugins\. With Mod Organizer 2 that means the Root
folder layout (Root Builder); launch the game through OBSE64.

FILES
-----
  * OBSE/Plugins/ApocryphaMenuFramework.dll - the framework
  * OBSE/Plugins/ApocryphaMenuFramework.ini - its settings (menu key, theme, text size, log level)
  * OBSE/Plugins/ApocryphaMenuFramework/themes - the theme files and their art
  * OBSE/Plugins/ApocryphaMenuFramework/Translations - its text in eleven languages
  * The log is Documents/My Games/Oblivion Remastered/OBSE/Logs/ApocryphaMenuFramework.log.
    It is written at info; set uLogLevel=0 in the INI for everything when reporting a problem.

LICENCE
-------
GPL-3.0-or-later, original work. Dear ImGui and cimgui are MIT (see THIRD_PARTY_NOTICES.md).
