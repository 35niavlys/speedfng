/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_SERVER_COMMANDS_H
#define GAME_SERVER_COMMANDS_H
#undef GAME_SERVER_COMMANDS_H // this file can be included several times
#ifndef CONSOLE_COMMAND
#define CONSOLE_COMMAND(name, params, flags, callback, userdata, help)
#endif

CONSOLE_COMMAND("fake", "ir", CFGFLAG_SERVER, ConFake, this, "Send chat message from player's name")
CONSOLE_COMMAND("faketo", "iir", CFGFLAG_SERVER, ConFakeTo, this, "Send chat message from player's name to another player")
CONSOLE_COMMAND("skin", "vs", CFGFLAG_SERVER, ConSkin, this, "Changes the skin from i in s")
CONSOLE_COMMAND("rename", "vr", CFGFLAG_SERVER, ConRename, this, "Renames i name to s")
CONSOLE_COMMAND("reclan", "vr", CFGFLAG_SERVER, ConReclan, this, "Reclans i name to s")
CONSOLE_COMMAND("msg", "ffr", CFGFLAG_SERVER, ConCreateText, this, "LolText") 

CONSOLE_COMMAND("left", "", CFGFLAG_SERVER, ConGoLeft, this, "Makes you move 1 tile left")
CONSOLE_COMMAND("right", "", CFGFLAG_SERVER, ConGoRight, this, "Makes you move 1 tile right")
CONSOLE_COMMAND("up", "", CFGFLAG_SERVER, ConGoUp, this, "Makes you move 1 tile up")
CONSOLE_COMMAND("down", "", CFGFLAG_SERVER, ConGoDown, this, "Makes you move 1 tile down")

CONSOLE_COMMAND("tele", "v?i", CFGFLAG_SERVER, ConTeleport, this, "Teleports you (or player v) to player i")

CONSOLE_COMMAND("mute", "", CFGFLAG_SERVER, ConMute, this, "");
CONSOLE_COMMAND("muteid", "vi", CFGFLAG_SERVER, ConMuteID, this, "");
CONSOLE_COMMAND("muteip", "si", CFGFLAG_SERVER, ConMuteIP, this, "");
CONSOLE_COMMAND("unmute", "v", CFGFLAG_SERVER, ConUnmute, this, "");
CONSOLE_COMMAND("mutes", "", CFGFLAG_SERVER, ConMutes, this, "");

CONSOLE_COMMAND("force_pause", "ii", CFGFLAG_SERVER, ConForcePause, this, "Force i to pause for i seconds")
CONSOLE_COMMAND("force_unpause", "i", CFGFLAG_SERVER, ConForcePause, this, "Set force-pause timer of v to 0.")

CONSOLE_COMMAND("pause", "", CFGFLAG_CHAT|CFGFLAG_SERVER, ConTogglePause, this, "Toggles pause")
CONSOLE_COMMAND("spec", "", CFGFLAG_CHAT|CFGFLAG_SERVER, ConToggleSpec, this, "Toggles spec (if not activated on the server, it toggles pause)")

#undef CONSOLE_COMMAND

#endif
