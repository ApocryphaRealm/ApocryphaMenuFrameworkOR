ApocryphaRealm Menu Framework - Oblivion Remastered
===================================================
Version 1.0.1

An original, GPL-3.0-or-later in-game menu framework (embedding Dear ImGui) for The Elder
Scrolls IV: Oblivion Remastered, loaded by OBSE64. It is the same framework as Apocrypha Menu
Framework for Skyrim - the same menu, themes, controls and mod API - drawn over Oblivion
Remastered's DirectX 12 renderer.

WHAT YOU GET
------------
  * F1 opens the menu in a window of its own; F1 or Escape closes it. The key can be rebound
    on the Controls page.
  * The Mod Control Panel: a side list (Framework Settings / Controls / Help, then every
    registered mod) with a content pane for the selected mod's settings pages.
  * Six themes - Skyrim (the knotwork frame, the default), Untarnished (clean lines, no frame
    art), and Vel'dun, Oathvein, Norden and Norden - Black - plus a font picker (drop a .ttf
    into OBSE/Plugins/ApocryphaMenuFramework/fonts) and a text-size slider.
  * Mouse, keyboard and controller. The menu follows whatever you last used; there is nothing
    to switch on.
  * While the menu is open the game does not see your keys, mouse or clicks, so nothing you do
    in the menu also happens in the game.

USING IT WITH A CONTROLLER
--------------------------
  * The D-pad and the left stick move through the list and across into the options.
  * A takes hold of a slider; the RIGHT stick then moves it. A again lets go.
  * B cancels, START closes the menu.

NOT YET IN THIS VERSION
-----------------------
  * Opening the menu from the game's own pause menu, and pausing the game while it is open.
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
