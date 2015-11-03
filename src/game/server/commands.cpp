/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "gamecontext.h"
#include <engine/shared/config.h>
#include <engine/server/server.h>
#include <game/version.h>
#include <game/server/entities/loltext.h>
#include <game/generated/nethash.cpp>
#if defined(CONF_SQL)
#include <game/server/score/sql_score.h>
#endif

bool CheckClientID(int ClientID)
{
	dbg_assert(ClientID >= 0 || ClientID < MAX_CLIENTS,
			"The Client ID is wrong");
	if (ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	return true;
}
//bool CheckRights(int ClientID, int Victim, CGameContext *GameContext);

void CGameContext::ConFake(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->GetInteger(0);

	pSelf->SendChat(Victim, CHAT_ALL, pResult->GetString(1), -1);
}

void CGameContext::ConFakeTo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int sender = pResult->GetInteger(0), receiver = pResult->GetInteger(1);

	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = CheckClientID(sender) ? sender : -1;
	Msg.m_pMessage = pResult->GetString(2);
	pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, receiver);

}

void CGameContext::ConSkin(IConsole::IResult *pResult, void *pUserData)
{
	//if(!CheckRights(pResult->m_ClientID, pResult->GetVictim(), (CGameContext *)pUserData)) return;
	const char *Skin = pResult->GetString(0);
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->GetVictim();

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	CCharacter* pChr = pSelf->m_apPlayers[Victim]->GetCharacter();;
	if(!pChr)
		return;

	//change skin
	str_copy(pSelf->m_apPlayers[Victim]->m_TeeInfos.m_SkinName, Skin, sizeof(pSelf->m_apPlayers[Victim]->m_TeeInfos.m_SkinName));
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s's skin changed to %s" ,pSelf->Server()->ClientName(Victim), Skin);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", aBuf);
}

void CGameContext::ConRename(IConsole::IResult *pResult, void *pUserData)
{
	const char *newName = pResult->GetString(0);
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->GetVictim();

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	CCharacter* pChr = pSelf->m_apPlayers[Victim]->GetCharacter();
	if(!pChr)
		return;

	//change name
	char oldName[MAX_NAME_LENGTH];
	str_copy(oldName, pSelf->Server()->ClientName(Victim), MAX_NAME_LENGTH);

	pSelf->Server()->SetClientName(Victim, newName);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s has changed %s's name to '%s'", pSelf->Server()->ClientName(pResult->m_ClientID), oldName, pSelf->Server()->ClientName(Victim));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", aBuf);

	str_format(aBuf, sizeof(aBuf), "%s changed your name to %s.", pSelf->Server()->ClientName(pResult->m_ClientID), pSelf->Server()->ClientName(Victim));
	pSelf->SendChatTarget(Victim, aBuf);
}

void CGameContext::ConReclan(IConsole::IResult *pResult, void *pUserData)
{
	const char *newClan = pResult->GetString(0);
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->GetVictim();

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	CCharacter* pChr = pSelf->m_apPlayers[Victim]->GetCharacter();
	if(!pChr)
		return;

	//change name
	char oldClan[MAX_CLAN_LENGTH];
	str_copy(oldClan, pSelf->Server()->ClientClan(Victim), MAX_CLAN_LENGTH);

	pSelf->Server()->SetClientClan(Victim, newClan);
}

void CGameContext::ConGoLeft(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	if (!CheckClientID(pResult->m_ClientID))
		return;
	pSelf->MoveCharacter(pResult->m_ClientID, -1, 0);
}

void CGameContext::ConGoRight(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	if (!CheckClientID(pResult->m_ClientID))
		return;
	pSelf->MoveCharacter(pResult->m_ClientID, 1, 0);
}

void CGameContext::ConGoDown(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	if (!CheckClientID(pResult->m_ClientID))
		return;
	pSelf->MoveCharacter(pResult->m_ClientID, 0, 1);
}

void CGameContext::ConGoUp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	if (!CheckClientID(pResult->m_ClientID))
		return;
	pSelf->MoveCharacter(pResult->m_ClientID, 0, -1);
}

void CGameContext::MoveCharacter(int ClientID, int X, int Y, bool Raw)
{
	CCharacter* pChr = GetPlayerChar(ClientID);

	if (!pChr)
		return;

	pChr->m_Core.m_Pos.x += ((Raw) ? 1 : 32) * X;
	pChr->m_Core.m_Pos.y += ((Raw) ? 1 : 32) * Y;
}
/*
void CGameContext::ConMove(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	if (!CheckClientID(pResult->m_ClientID))
		return;
	pSelf->MoveCharacter(pResult->m_ClientID, pResult->GetInteger(0),
			pResult->GetInteger(1));
}

void CGameContext::ConMoveRaw(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	if (!CheckClientID(pResult->m_ClientID))
		return;
	pSelf->MoveCharacter(pResult->m_ClientID, pResult->GetInteger(0),
			pResult->GetInteger(1), true);
}
*/
void CGameContext::ConTeleport(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	int TeleTo = pResult->GetInteger(0);
	int Tele = pResult->m_ClientID;
	if (pResult->NumArguments() > 0)
		Tele = pResult->GetVictim();

	if (pSelf->m_apPlayers[TeleTo])
	{
		CCharacter* pChr = pSelf->GetPlayerChar(Tele);
		if (pChr && pSelf->GetPlayerChar(TeleTo))
		{
			pChr->Core()->m_Pos = pSelf->m_apPlayers[TeleTo]->m_ViewPos;
			pChr->m_Pos = pSelf->m_apPlayers[TeleTo]->m_ViewPos;
	        pChr->m_PrevPos = pSelf->m_apPlayers[TeleTo]->m_ViewPos;
		}
	}
}

void CGameContext::Mute(IConsole::IResult *pResult, NETADDR *Addr, int Secs,
		const char *pDisplayName)
{
	char aBuf[128];
	int Found = 0;
	// find a matching mute for this ip, update expiration time if found
	for (int i = 0; i < m_NumMutes; i++)
	{
		if (net_addr_comp(&m_aMutes[i].m_Addr, Addr) == 0)
		{
			m_aMutes[i].m_Expire = Server()->Tick()
							+ Secs * Server()->TickSpeed();
			Found = 1;
		}
	}

	if (!Found) // nothing found so far, find a free slot..
	{
		if (m_NumMutes < MAX_MUTES)
		{
			m_aMutes[m_NumMutes].m_Addr = *Addr;
			m_aMutes[m_NumMutes].m_Expire = Server()->Tick()
							+ Secs * Server()->TickSpeed();
			m_NumMutes++;
			Found = 1;
		}
	}
	if (Found)
	{
		str_format(aBuf, sizeof aBuf, "'%s' has been muted for %d seconds.",
				pDisplayName, Secs);
		SendChat(-1, CHAT_ALL, aBuf);
	}
	else // no free slot found
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mutes", "mute array is full");
}

void CGameContext::ConMute(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"mutes",
			"Use either 'muteid <client_id> <seconds>' or 'muteip <ip> <seconds>'");
}

// mute through client id
void CGameContext::ConMuteID(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	int Victim = pResult->GetVictim();

	NETADDR Addr;
	pSelf->Server()->GetClientAddr(Victim, &Addr);

	pSelf->Mute(pResult, &Addr, clamp(pResult->GetInteger(0), 1, 86400),
			pSelf->Server()->ClientName(Victim));
}

// mute through ip, arguments reversed to workaround parsing
void CGameContext::ConMuteIP(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	NETADDR Addr;
	if (net_addr_from_str(&Addr, pResult->GetString(0)))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mutes",
				"Invalid network address to mute");
	}
	pSelf->Mute(pResult, &Addr, clamp(pResult->GetInteger(1), 1, 86400),
			pResult->GetString(0));
}

// unmute by mute list index
void CGameContext::ConUnmute(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	char aIpBuf[64];
	char aBuf[64];
	int Victim = pResult->GetVictim();

	if (Victim < 0 || Victim >= pSelf->m_NumMutes)
		return;

	pSelf->m_NumMutes--;
	pSelf->m_aMutes[Victim] = pSelf->m_aMutes[pSelf->m_NumMutes];

	net_addr_str(&pSelf->m_aMutes[Victim].m_Addr, aIpBuf, sizeof(aIpBuf), false);
	str_format(aBuf, sizeof(aBuf), "Unmuted %s", aIpBuf);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mutes", aBuf);
}

// list mutes
void CGameContext::ConMutes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	char aIpBuf[64];
	char aBuf[128];
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mutes",
			"Active mutes:");
	for (int i = 0; i < pSelf->m_NumMutes; i++)
	{
		net_addr_str(&pSelf->m_aMutes[i].m_Addr, aIpBuf, sizeof(aIpBuf), false);
		str_format(
				aBuf,
				sizeof aBuf,
				"%d: \"%s\", %d seconds left",
				i,
				aIpBuf,
				(pSelf->m_aMutes[i].m_Expire - pSelf->Server()->Tick())
				/ pSelf->Server()->TickSpeed());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mutes", aBuf);
	}
}

void CGameContext::ConForcePause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CServer* pServ = (CServer*)pSelf->Server();
	int Victim = pResult->GetVictim();
	int Seconds = 0;
	if (pResult->NumArguments() > 0)
		Seconds = clamp(pResult->GetInteger(0), 0, 360);

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if (!pPlayer)
		return;

	pPlayer->m_ForcePauseTime = Seconds*pServ->TickSpeed();
	pPlayer->m_Paused = CPlayer::PAUSED_FORCE;
}

void CGameContext::ConTogglePause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	if (!CheckClientID(pResult->m_ClientID))
		return;
	char aBuf[128];

	if(!g_Config.m_SvPauseable)
	{
		ConToggleSpec(pResult, pUserData);
		return;
	}

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientID];
	if (!pPlayer)
		return;
	
	CCharacter* pChr = pSelf->GetPlayerChar(pResult->m_ClientID);
	if (!pChr)
		return;

	if (pPlayer->GetCharacter() == 0)
	{
	    pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "pause",
	    "You can't pause while you are dead/a spectator.");
	    return;
	}
	
	if(!pPlayer->m_Authed)
	{
	    if(!pPlayer->m_Pauseable)
	    {
		    pSelf->SendChatTarget(pResult->m_ClientID, "You can't pause , will be active when you got the award.", pResult->m_ClientID);
	        return;
	    }
	}
	
	if (pPlayer->m_Paused == CPlayer::PAUSED_SPEC && g_Config.m_SvPauseable)
	{
		ConToggleSpec(pResult, pUserData);
		return;
	}

	if (pPlayer->m_Paused == CPlayer::PAUSED_FORCE)
	{
		str_format(aBuf, sizeof(aBuf), "You are force-paused. %ds left.", pPlayer->m_ForcePauseTime/pSelf->Server()->TickSpeed());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "pause", aBuf);
		return;
	}
	
	if(pChr)
	{
		if((pPlayer->m_Pauseable && !pPlayer->m_Paused) || (pPlayer->m_Authed && pPlayer->m_Invisible && !pPlayer->m_Paused))
		{
		    pChr->m_Invisible = 0;
		    pChr->m_InvisibleTick = 0;
		}
	}

	pPlayer->m_Paused = (pPlayer->m_Paused == CPlayer::PAUSED_PAUSED) ? CPlayer::PAUSED_NONE : CPlayer::PAUSED_PAUSED;
}

void CGameContext::ConToggleSpec(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientID(pResult->m_ClientID)) return;
	char aBuf[128];

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientID];
	if(!pPlayer)
		return;
	
	CCharacter* pChr = pSelf->GetPlayerChar(pResult->m_ClientID);
	if (!pChr)
		return;

	if (pPlayer->GetCharacter() == 0)
	{
	    pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "spec",
	    "You can't spec while you are dead/a spectator.");
	    return;
	}
	
	if(!pPlayer->m_Authed)
	{
	    if(!pPlayer->m_Pauseable)
	    {
		    pSelf->SendChatTarget(pResult->m_ClientID, "You can't spec , will be active when you got the award.", pResult->m_ClientID);
	        return;
	    }
	}

	if(pPlayer->m_Paused == CPlayer::PAUSED_FORCE)
	{
		str_format(aBuf, sizeof(aBuf), "You are force-paused. %ds left.", pPlayer->m_ForcePauseTime/pSelf->Server()->TickSpeed());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "spec", aBuf);
		return;
	}
	
	if(pChr)
	{
		if((pPlayer->m_Pauseable && !pPlayer->m_Paused) || (pPlayer->m_Authed && pPlayer->m_Invisible && !pPlayer->m_Paused))
		{
		    pChr->m_Invisible = 0;
		    pChr->m_InvisibleTick = 0;
		}
	}

	pPlayer->m_Paused = (pPlayer->m_Paused == CPlayer::PAUSED_SPEC) ? CPlayer::PAUSED_NONE : CPlayer::PAUSED_SPEC;
}

void CGameContext::ConCreateText(IConsole::IResult *pResult, void *pUserData)
{
    CGameContext *pSelf = (CGameContext *) pUserData;
    const int ClientID = pResult->m_ClientID;

  	CCharacter * pChar = pSelf->GetPlayerChar(ClientID);
 	if(pChar)
 		pSelf->CreateText(pChar, true, vec2(pResult->GetFloat(0), pResult->GetFloat(1)), vec2(0, 0), 600, pResult->GetString(2));
}

/*
bool CheckRights(int ClientID, int Victim, CGameContext *GameContext)
{
	if(!CheckClientID(ClientID)) return false;
	if(!CheckClientID(Victim)) return false;

	if (ClientID == Victim)
		return true;

	if (!GameContext->m_apPlayers[ClientID] || !GameContext->m_apPlayers[Victim])
		return false;

	if(GameContext->m_apPlayers[ClientID]->m_Authed <= GameContext->m_apPlayers[Victim]->m_Authed)
		return false;

	return true;
}
*/