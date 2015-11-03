/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "laser.h"
#include "health.h"

CLaser::CLaser(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = Pos;
	m_Owner = Owner;
	m_Energy = StartEnergy;
	m_Dir = Direction;
	m_Bounces = 0;
	m_EvalTick = 0;
	GameWorld()->InsertEntity(this);
	DoBounce();
}


bool CLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pHit = 0;
	CCharacter *pSkipChar = pOwnerChar;
	vec2 Pos = m_Pos;
	while (length(From-Pos) + length(Pos-To) < length(From-To) + 1e-5)
	{
		pSkipChar = GameServer()->m_World.IntersectCharacter(Pos, To, 0.f, At, pSkipChar);
		if (!pSkipChar)
			break;
		Pos = At + normalize(To-From)*(pSkipChar->m_ProximityRadius+1e-5);

		if(g_Config.m_SvLaserSkipFrozen && pSkipChar->GetFreezeTicks() > 0)
			continue;

		if (pSkipChar == pOwnerChar) //can actually happen on bounce
			continue;

		if (pOwnerChar && g_Config.m_SvLaserSkipTeammates && pOwnerChar->GetPlayer()->GetTeam() == pSkipChar->GetPlayer()->GetTeam())
			continue;

		pHit = pSkipChar;
		break;
	}

	if (!pHit)
		return false;

	m_From = From;
	m_Pos = At;
	m_Energy = -1;
	pHit->TakeDamage(vec2(0.f, 0.f), GameServer()->Tuning()->m_LaserDamage, m_Owner, WEAPON_RIFLE);

	if (pOwnerChar && pOwnerChar->GetPlayer()->GetTeam() != pHit->GetPlayer()->GetTeam())
	{
		if (pHit->GetFreezeTicks() <= 0 && (pHit->MoltenByHammer() || pHit->GetMeltTick() + g_Config.m_SvMeltSafeticks < Server()->Tick()))
		{
			pHit->Freeze(GameServer()->Tuning()->m_LaserDamage * Server()->TickSpeed(), m_Owner);
			
			if(pOwnerChar && pOwnerChar->GetPlayer()->m_RifleSwap)
			{
				vec2 TempPos = pOwnerChar->m_Pos;
			/*  pOwnerChar->Core()->m_Pos = pHit->m_Pos;  */ // Stupid Thing (:
			    pHit->Core()->m_Pos = TempPos;
			}
		}
	}
	
	if(pOwnerChar && pOwnerChar->GetPlayer()->m_TeamProtect && pHit->GetPlayer()->m_Spree <= (g_Config.m_SvKillingspreeKills * 3))
	{
	    if (pOwnerChar && pOwnerChar->GetPlayer()->GetTeam() == pHit->GetPlayer()->GetTeam())
		{
			if (pHit->GetFreezeTicks() > 0 && (!pHit->MoltenByHammer() || pHit->GetMeltTick() + g_Config.m_SvMeltSafeticks > Server()->Tick()))
			{
			    if (!pHit->Core()->m_ProtectedBy)
				{
				    pHit->Core()->m_ProtectedBy = true;
				    new CHealth(&GameServer()->m_World, pHit->Core()->m_Pos, pHit->GetPlayer()->GetCID(), 1);
					pHit->Freeze(pHit->GetFreezeTicks() - g_Config.m_SvHammerMelt * Server()->TickSpeed());
				}
			}
		}
	}

	return true;
}

void CLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		GameServer()->m_World.DestroyEntity(this);
		return;
	}

	vec2 To = m_Pos + m_Dir * m_Energy;

	if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
	{
		if(!HitCharacter(m_Pos, To))
		{
			// intersected
			m_From = m_Pos;
			m_Pos = To;

			vec2 TempPos = m_Pos;
			vec2 TempDir = m_Dir * 4.0f;

			GameServer()->Collision()->MovePoint(&TempPos, &TempDir, 1.0f, 0);
			m_Pos = TempPos;
			m_Dir = normalize(TempDir);

			m_Energy -= distance(m_From, m_Pos) + GameServer()->Tuning()->m_LaserBounceCost;
			m_Bounces++;

			if(m_Bounces > GameServer()->Tuning()->m_LaserBounceNum)
				m_Energy = -1;

			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_BOUNCE);
		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy = -1;
		}
	}
}

void CLaser::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CLaser::Tick()
{
	if(Server()->Tick() > m_EvalTick+(Server()->TickSpeed()*GameServer()->Tuning()->m_LaserBounceDelay)/1000.0f)
		DoBounce();
}

void CLaser::TickPaused()
{
	++m_EvalTick;
}

void CLaser::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_From.x;
	pObj->m_FromY = (int)m_From.y;
	pObj->m_StartTick = m_EvalTick;
}
