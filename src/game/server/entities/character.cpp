/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>

#include "character.h"
#include "laser.h"
#include "projectile.h"
#include "shield.h"

//input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}


MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER)
{
	m_ProximityRadius = ms_PhysSize;
	m_Health = 0;
	m_Armor = 0;
}

void CCharacter::Reset()
{
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_EmoteStop = -1;
	m_LastAction = -1;
	m_LastNoAmmoSound = -1;
	m_ActiveWeapon = WEAPON_GUN;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;
	m_Paused = false;

	m_pPlayer = pPlayer;
	m_Pos = Pos;
	
	m_PrevPos = m_Pos;

	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision());
	m_Core.m_Pos = m_Pos;
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;

	GameServer()->m_pController->OnCharacterSpawn(this);

	m_BloodTicks = 0;
	m_FrozenBy = -1;
	m_MoltenBy = -1;
	m_MoltenAt = -1;
	m_MoltenByHammer = false;
	m_HammeredBy = -1;
	
	m_InvisibleTick = 0;
	m_Invisible = 0;

	return true;
}

void CCharacter::Destroy()
{
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x+m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x-m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	return false;
}


void CCharacter::HandleNinja()
{
	if(m_ActiveWeapon != WEAPON_NINJA)
		return;

	if ((Server()->Tick() - m_Ninja.m_ActivationTick) > (g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000))
	{
		// time's up, return
		TakeNinja();
		return;
	}

	// force ninja Weapon
	SetWeapon(WEAPON_NINJA);

	m_Ninja.m_CurrentMoveTime--;

	if (m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir*m_Ninja.m_OldVelAmount;
	}

	if (m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		{
			CCharacter *aEnts[MAX_CLIENTS];
			vec2 Dir = m_Pos - OldPos;
			float Radius = m_ProximityRadius * 2.0f;
			vec2 Center = OldPos + Dir * 0.5f;
			int Num = GameServer()->m_World.FindEntities(Center, Radius, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				if (aEnts[i] == this)
					continue;

				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for (int j = 0; j < m_NumObjectsHit; j++)
				{
					if (m_apHitObjects[j] == aEnts[i])
						bAlreadyHit = true;
				}
				if (bAlreadyHit)
					continue;

				// check so we are sufficiently close
				if (distance(aEnts[i]->m_Pos, m_Pos) > (m_ProximityRadius * 2.0f))
					continue;

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(aEnts[i]->m_Pos, SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if(m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = aEnts[i];

				aEnts[i]->TakeDamage(vec2(0, -10.0f), g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage, m_pPlayer->GetCID(), WEAPON_NINJA);
			}
		}

		return;
	}

	return;
}


void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1 || m_aWeapons[WEAPON_NINJA].m_Got)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(Next < 128) // make sure we only try sane stuff
	{
		while(Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon+1)%NUM_WEAPONS;
			if(m_aWeapons[WantedWeapon].m_Got)
				Next--;
		}
	}

	if(Prev < 128) // make sure we only try sane stuff
	{
		while(Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon-1)<0?NUM_WEAPONS-1:WantedWeapon-1;
			if(m_aWeapons[WantedWeapon].m_Got)
				Prev--;
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon-1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;

	DoWeaponSwitch();
}

void CCharacter::FireWeapon()
{
	if(m_ReloadTimer != 0)
		return;

	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	bool FullAuto = false;
	if(m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE
	    || (g_Config.m_SvAutoHammer && m_ActiveWeapon == WEAPON_HAMMER))
		FullAuto = true;


	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(FullAuto && (m_LatestInput.m_Fire&1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if (!g_Config.m_SvNinja && m_ActiveWeapon == WEAPON_NINJA)
		WillFire = false;

	if(!WillFire)
		return;

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo)
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		if(m_LastNoAmmoSound+Server()->TickSpeed() <= Server()->Tick())
		{
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			m_LastNoAmmoSound = Server()->Tick();
		}
		return;
	}

	vec2 ProjStartPos = m_Pos+Direction*m_ProximityRadius*0.75f;
	
	if(m_pPlayer->m_Invisible)
	{
		m_InvisibleTick = 0;
		m_Invisible = 0;	
	}

	switch(m_ActiveWeapon)
	{
		case WEAPON_HAMMER:
		{
			// reset objects Hit
			m_NumObjectsHit = 0;
			GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);

			CCharacter *apEnts[MAX_CLIENTS];
			int Hits = 0;
			int Num = GameServer()->m_World.FindEntities(ProjStartPos, m_ProximityRadius*0.5f, (CEntity**)apEnts,
														MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				CCharacter *pTarget = apEnts[i];

				if ((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->m_Pos, NULL, NULL))
					continue;

				// set his velocity to fast upward (for now)
				if(length(pTarget->m_Pos-ProjStartPos) > 0.0f)
					GameServer()->CreateHammerHit(pTarget->m_Pos-normalize(pTarget->m_Pos-ProjStartPos)*m_ProximityRadius*0.5f);
				else
					GameServer()->CreateHammerHit(ProjStartPos);

				vec2 Dir;
				if (length(pTarget->m_Pos - m_Pos) > 0.0f)
					Dir = normalize(pTarget->m_Pos - m_Pos);
				else
					Dir = vec2(0.f, -1.f);

				bool MeltHit = pTarget->GetPlayer()->GetTeam() == GetPlayer()->GetTeam() && pTarget->GetFreezeTicks() > 0;
				
				bool TeamFreeze = pTarget->GetPlayer()->GetTeam() == GetPlayer()->GetTeam() && pTarget->GetFreezeTicks() <= 0;

				vec2 Force = (vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f);
				if (!MeltHit)
				{
					Force.x *= g_Config.m_SvHammerScaleX*0.01f;
					Force.y *= g_Config.m_SvHammerScaleY*0.01f;
				}
				else
				{
					Force.x *= g_Config.m_SvMeltHammerScaleX*0.01f;
					Force.y *= g_Config.m_SvMeltHammerScaleY*0.01f;
				}

				pTarget->TakeDamage(Force, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage, m_pPlayer->GetCID(), m_ActiveWeapon);
				Hits++;
				
				pTarget->m_HammeredBy = GetPlayer()->GetCID();

				if (MeltHit)
				{
					pTarget->Freeze(pTarget->GetFreezeTicks() - g_Config.m_SvHammerMelt * Server()->TickSpeed());
					if (pTarget->GetFreezeTicks() <= 0)
					{
						pTarget->m_MoltenBy = m_pPlayer->GetCID();
						pTarget->m_MoltenAt = Server()->Tick();
						pTarget->m_MoltenByHammer = true;
					}
				}
				
				if (GameServer()->m_apPlayers[pTarget->GetPlayer()->GetCID()] && GameServer()->m_apPlayers[pTarget->GetPlayer()->GetCID()]->m_LastEmoticon < Server()->Tick() && TeamFreeze)
		        {
			        CCharacter *pChr = GameServer()->m_apPlayers[pTarget->GetPlayer()->GetCID()]->GetCharacter();
			        if (pChr)
			        {
				        GameServer()->m_apPlayers[pTarget->GetPlayer()->GetCID()]->m_LastEmoticon = Server()->Tick() + Server()->TickSpeed()*g_Config.m_SvEmoticonDelay;
				        GameServer()->SendEmoticon(pTarget->GetPlayer()->GetCID(), EMOTICON_SPLATTEE);
						GameServer()->CreateDeath(pTarget->m_Pos, pTarget->GetPlayer()->GetCID());
						GameServer()->CreateSound(GameServer()->m_apPlayers[pTarget->GetPlayer()->GetCID()]->m_ViewPos, SOUND_NINJA_FIRE, CmaskOne(pTarget->GetPlayer()->GetCID()));
				        pTarget->m_EmoteType = EMOTE_ANGRY;
				        pTarget->m_EmoteStop = Server()->Tick() + Server()->TickSpeed()*g_Config.m_SvEmoticonDelay;
			        }
		        }

				if (GameServer()->m_apPlayers[GetPlayer()->GetCID()] && GameServer()->m_apPlayers[GetPlayer()->GetCID()]->m_LastEmoticon < Server()->Tick() && TeamFreeze)
		        {
			        CCharacter *pChr = GameServer()->m_apPlayers[GetPlayer()->GetCID()]->GetCharacter();
			        if (pChr)
			        {
				        GameServer()->m_apPlayers[GetPlayer()->GetCID()]->m_LastEmoticon = Server()->Tick() + Server()->TickSpeed()*g_Config.m_SvEmoticonDelay;
				        GameServer()->SendEmoticon(GetPlayer()->GetCID(), EMOTICON_EYES);
				        GameServer()->CreateSound(GameServer()->m_apPlayers[GetPlayer()->GetCID()]->m_ViewPos, SOUND_PLAYER_SPAWN, CmaskOne(GetPlayer()->GetCID()));
				        pChr->m_EmoteType = EMOTE_HAPPY;
				        pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed()*g_Config.m_SvEmoticonDelay;
			        }
		        }
			}

			// if we Hit anything, we have to wait for the reload
			if(Hits)
				m_ReloadTimer = Server()->TickSpeed()/3;

		} break;

		case WEAPON_GUN:
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
				m_pPlayer->GetCID(),
				ProjStartPos,
				Direction,
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
				1, 0, 0, -1, WEAPON_GUN);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(1);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);

			Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());

			GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
		} break;

		case WEAPON_SHOTGUN:
		{
			int ShotSpread = 2;

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(ShotSpread*2+1);

			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float Spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
				float a = GetAngle(Direction);
				a += Spreading[i+2];
				float v = 1-(absolute(i)/(float)ShotSpread);
				float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
					m_pPlayer->GetCID(),
					ProjStartPos,
					vec2(cosf(a), sinf(a))*Speed,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime),
					1, 0, 0, -1, WEAPON_SHOTGUN);

				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);

				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);
			}

			Server()->SendMsg(&Msg, 0,m_pPlayer->GetCID());

			GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
		} break;

		case WEAPON_GRENADE:
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GRENADE,
				m_pPlayer->GetCID(),
				ProjStartPos,
				Direction,
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
				1, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(1);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
			Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());

			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		} break;

		case WEAPON_RIFLE:
		{
			if(m_pPlayer->m_RifleSpread)
			{
			    int ShotSpread = 1;
				float Spreading[18*2+1];
				for(int i = 0; i < 18*2+1; i++)
					Spreading[i] = -1.260f + 0.070f * i;
			
				for(int i = -ShotSpread; i < ShotSpread; ++i)
				{
					float a = GetAngle(Direction);
					a += Spreading[i+18];
					new CLaser(GameWorld(), m_Pos, vec2(cosf(a), sinf(a)), GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID());
				}  
			}
			else
			    new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID());
			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
		} break;

		case WEAPON_NINJA:
		{
			// reset Hit objects
			m_NumObjectsHit = 0;

			m_Ninja.m_ActivationDir = Direction;
			m_Ninja.m_CurrentMoveTime = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
			m_Ninja.m_OldVelAmount = length(m_Core.m_Vel);

			GameServer()->CreateSound(m_Pos, SOUND_NINJA_FIRE);
		} break;

	}

	m_AttackTick = Server()->Tick();
	
	if(GetPlayer()->m_GrenadeLauncher)
	{
	    if(!g_Config.m_SvUnlimitedAmmo && m_aWeapons[m_ActiveWeapon].m_Ammo <= 1)
	    {
		    if(m_ActiveWeapon == WEAPON_GRENADE)
		        TakeWeapon(m_ActiveWeapon);
		}
	}

	if (!g_Config.m_SvUnlimitedAmmo && m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
		if(m_ActiveWeapon != WEAPON_RIFLE)
	        m_aWeapons[m_ActiveWeapon].m_Ammo--;

	if(!m_ReloadTimer)
		m_ReloadTimer = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() / 1000;
}

void CCharacter::HandleWeapons()
{
	//ninja
	HandleNinja();

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();

	// ammo regen
	int AmmoRegenTime = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Ammoregentime;
	/*if(m_aWeapons[m_ActiveWeapon].m_Ammo > -1) // i dont need this (:
	{
		switch(m_ActiveWeapon)
		{
			case WEAPON_GUN: AmmoRegenTime = 125*5; break;
			case WEAPON_GRENADE: AmmoRegenTime = 1000; break;
			case WEAPON_RIFLE: AmmoRegenTime = 1200; break;
			case WEAPON_SHOTGUN: AmmoRegenTime = 1000; break;
		}
	}*/
	if(AmmoRegenTime)
	{
		// If equipped and not active, regen ammo?
		if (m_ReloadTimer <= 0)
		{
			if (m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart < 0)
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = Server()->Tick();

			if ((Server()->Tick() - m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
			{
				// Add some ammo
				m_aWeapons[m_ActiveWeapon].m_Ammo = min(m_aWeapons[m_ActiveWeapon].m_Ammo + 1, 10);
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
			}
		}
		else
		{
			m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
		}
	}

	return;
}

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	if(m_aWeapons[Weapon].m_Ammo < g_pData->m_Weapons.m_aId[Weapon].m_Maxammo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = min(g_pData->m_Weapons.m_aId[Weapon].m_Maxammo, Ammo);
		return true;
	}
	return false;
}

void CCharacter::GiveNinja(bool Silent)
{
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	if (m_ActiveWeapon != WEAPON_NINJA)
		m_LastWeapon = m_ActiveWeapon;
	m_ActiveWeapon = WEAPON_NINJA;

	if (!Silent)
		GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA);
}

void CCharacter::TakeNinja()
{
	if (m_ActiveWeapon != WEAPON_NINJA)
		return;

	m_aWeapons[WEAPON_NINJA].m_Got = false;
	m_ActiveWeapon = m_LastWeapon;
	if(m_ActiveWeapon == WEAPON_NINJA)
		m_ActiveWeapon = WEAPON_HAMMER;
	//SetWeapon(m_ActiveWeapon); //has no effect
}

bool CCharacter::TakeWeapon(int Weapon)
{
	int NumWeps = 0;
	for(int i = 0; i < NUM_WEAPONS; i++)
		if (m_aWeapons[i].m_Got)
			NumWeps++;

	if (Weapon < 0 || Weapon >= NUM_WEAPONS || NumWeps <= 1 || !m_aWeapons[Weapon].m_Got)
		return false;

	m_aWeapons[Weapon].m_Got = false;

	if (m_ActiveWeapon == Weapon)
	{
		int NewWeap = 0;
		if (m_LastWeapon != -1 && m_LastWeapon != Weapon && m_aWeapons[m_LastWeapon].m_Got)
			NewWeap = m_LastWeapon;
		else
			for(; NewWeap < NUM_WEAPONS && !m_aWeapons[NewWeap].m_Got; NewWeap++);

		SetWeapon(NewWeap);
	}

	if (m_LastWeapon != -1 && !m_aWeapons[m_LastWeapon].m_Got)
		m_LastWeapon = m_ActiveWeapon;

	if (m_QueuedWeapon != -1 && !m_aWeapons[m_QueuedWeapon].m_Got)
		m_QueuedWeapon = -1;

	return true;
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

int CCharacter::GetFreezeTicks()
{
	return m_Core.m_Frozen;
}

void CCharacter::Freeze(int Ticks, int By)
{
	if (Ticks < 0)
		Ticks = 0;
	if (By != -1 && Ticks > 0)
		m_FrozenBy = By;
	m_Core.m_Frozen = Ticks;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire&1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{
	if(m_pPlayer->m_ForceBalanced)
	{
		char Buf[128];
		str_format(Buf, sizeof(Buf), "You were moved to %s due to team balancing", GameServer()->m_pController->GetTeamName(m_pPlayer->GetTeam()));
		GameServer()->SendBroadcast(Buf, m_pPlayer->GetCID());

		m_pPlayer->m_ForceBalanced = false;
	}
	
	if (m_Paused)
		return;
	
	if(GetPlayer()->m_Invisible)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			//CCharacterCore *pCharCore = m_pWorld->m_apCharacters[i];
			CCharacter *pCharCore = GameServer()->GetPlayerChar(i);
			if(!pCharCore)
				continue;
			
			//player *p = (player*)ent;
			if(pCharCore == this) // || !(p->flags&FLAG_ALIVE)
				continue; // make sure that we don't nudge our self

			// handle player <-> player collision
			float Distance = distance(m_Pos, pCharCore->m_Pos);
			vec2 Dir = normalize(m_Pos - pCharCore->m_Pos);
			if(Distance < 28.0f*1.25f && Distance > 0.0f)
			{
				//GameServer()->SendChatTarget(-1, "test");
				m_Invisible = 0;
				m_InvisibleTick = 0;
				new CShield(&GameServer()->m_World, m_Pos, GetPlayer()->GetCID(), 1); 
			}
		}
	}

	m_Core.m_Input = m_Input;
	m_Core.Tick(true);
	
	/*if(GameServer()->Collision()->GetHitPos((int)m_Pos.x, (int)m_Pos.y, TILE_AIR))*/
	/*if(Server()->Tick() % (1 * Server()->TickSpeed() * 60) == 0)*/ // One Min
	
	if(GetPlayer()->m_JetPack && GetFreezeTicks() <= 0)
	{
	    vec2 Direction = normalize(vec2(0, 10.0f));
	    if(!(m_Core.m_Jumped&1))
		{
	        if(m_Input.m_Jump)
			{
	            /*TakeDamage(Direction * -1.0f * (400.0f / 100.0f / 6.11f), 0, m_pPlayer->GetCID(), m_ActiveWeapon);*/ // fix botdetect bug
				m_Core.m_Vel += Direction * -1.5f * (400.0f / 100.0f / 6.11f);
				
                if(Server()->Tick() % (1 * Server()->TickSpeed() / 10) == 0)
				{
					if(m_pPlayer->m_Invisible){
		            m_Invisible = 0;
		            m_InvisibleTick = 0;}
					GameServer()->CreateSound(m_Core.m_Pos, SOUND_WEAPON_SWITCH);
					GameServer()->CreateDamageInd(m_Core.m_Vel.y < 0 ? m_Core.m_Pos - vec2((m_Input.m_Direction != 0 ? (m_Input.m_Direction > 0 ? 28 : -28) : 0), -28) : m_Core.m_Pos - vec2((m_Input.m_Direction != 0 ? (m_Input.m_Direction > 0 ? 28 : -28) : 0), 28), m_Core.m_Vel.y < 0 ? 0.3f : 60.0f, m_Input.m_Jump);
				}
			}
		}
	}
	
	if(GetPlayer()->m_SpeedRunner && GetFreezeTicks() <= 0)
    {
        const float MaxSpeed = IsGrounded() ? GameServer()->m_World.m_Core.m_Tuning.m_GroundControlSpeed * 3 : 0.0f;
        const float Accel = IsGrounded() ? GameServer()->m_World.m_Core.m_Tuning.m_GroundControlAccel : 0.0f;
        if(m_Input.m_Direction == 1)
            m_Core.m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Core.m_Vel.x, Accel);
        else if(m_Input.m_Direction == -1)
            m_Core.m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Core.m_Vel.x, -Accel);
    }
	
	if(m_Core.m_ProtectedBy && GetFreezeTicks() > 0)
	{
		const float MaxSpeed = IsGrounded() ? GameServer()->m_World.m_Core.m_Tuning.m_GroundControlSpeed * 2 : GameServer()->m_World.m_Core.m_Tuning.m_AirControlSpeed * 4;
        const float Accel = IsGrounded() ? GameServer()->m_World.m_Core.m_Tuning.m_GroundControlAccel : GameServer()->m_World.m_Core.m_Tuning.m_AirControlAccel;
        if(m_Input.m_Direction == 1)
            m_Core.m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Core.m_Vel.x, Accel);
        else if(m_Input.m_Direction == -1)
            m_Core.m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Core.m_Vel.x, -Accel);
	}

	if (m_BloodTicks > 0)
	{
		if (m_BloodTicks % g_Config.m_SvBloodInterval == 0)
			GameServer()->CreateDeath(m_Core.m_Pos, m_pPlayer->GetCID());
		--m_BloodTicks;
	}

	int Col = GameServer()->Collision()->GetCollisionAt(m_Pos.x, m_Pos.y);
	if((Col <= 7 && Col&CCollision::COLFLAG_DEATH) || GameLayerClipped(m_Pos)) //seriously.
	{
		// handle death-tiles and leaving gamelayer
		m_Core.m_Frozen = 0; //we just unfreeze so it never counts as a sacrifice
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	if (m_Core.m_Frozen)
	{
		if (m_ActiveWeapon != WEAPON_NINJA)
			GiveNinja(true);
		else if (m_Ninja.m_ActivationTick + 5 * Server()->TickSpeed() < Server()->Tick())
			m_Ninja.m_ActivationTick = Server()->Tick(); // this should fix the end-of-ninja missprediction bug

		//openfng handles this in mod gamectl
		//if ((m_Core.m_Frozen+1) % Server()->TickSpeed() == 0)
		//	GameServer()->CreateDamageInd(m_Pos, 0, (m_Core.m_Frozen+1) / Server()->TickSpeed());

		m_MoltenBy = -1;
		m_MoltenAt = -1;
		m_MoltenByHammer = false;
	}
	else
	{
		if (m_ActiveWeapon == WEAPON_NINJA)
		{
			TakeNinja();
			m_MoltenAt = Server()->Tick();
			if(m_Core.m_ProtectedBy)
			    m_Core.m_ProtectedBy = false;
		}
		m_FrozenBy = -1;
	}

	// handle Weapons
	HandleWeapons();

	// Previnput
	m_PrevInput = m_Input;
	
	//Invisible
	if(m_pPlayer->m_Invisible && m_Invisible == 0 && !GetPlayer()->m_Paused)
		m_InvisibleTick = m_InvisibleTick + 1;
	
	if(m_pPlayer->m_Invisible && m_InvisibleTick >= 100 && GetFreezeTicks() <= 0)
	{
		m_Invisible = 1;
		m_InvisibleTick = 0;
		new CShield(&GameServer()->m_World, m_Pos, GetPlayer()->GetCID(), 1); 
	}
	
	if (m_Core.m_HookedPlayer != -1 && GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->m_Invisible)
	{
		GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->GetCharacter()->m_Invisible = 0;
		GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->GetCharacter()->m_InvisibleTick = 0;	
	}
	
	if(m_pPlayer->m_Invisible && (m_Core.m_HookedPlayer != -1 || m_Input.m_Hook ))
	{
		m_InvisibleTick = 0;
		m_Invisible = 0;	
	}
	
	m_PrevPos = m_Core.m_Pos;
	return;
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false);
		m_ReckoningCore.Move();
		m_ReckoningCore.Quantize();
	}

	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.Move();
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		}StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	int Events = m_Core.m_TriggeredEvents;
	int Mask = CmaskAllExceptOne(m_pPlayer->GetCID());

	if(Events&COREEVENT_GROUND_JUMP) GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP, Mask);

	if(Events&COREEVENT_HOOK_ATTACH_PLAYER) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_PLAYER, CmaskAll());
	if(Events&COREEVENT_HOOK_ATTACH_GROUND) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND, Mask);
	if(Events&COREEVENT_HOOK_HIT_NOHOOK) GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, Mask);


	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_ReckoningTick+Server()->TickSpeed()*3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0
				|| (m_Core.m_Frozen > 0 && !(Server()->Tick()&1)))
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_DamageTakenTick;
	++m_Ninja.m_ActivationTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(m_Health >= 10)
		return false;
	m_Health = clamp(m_Health+Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor+Amount, 0, 10);
	return true;
}

void CCharacter::Die(int Killer, int Weapon, bool NoKillMsg)
{
	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
		Killer, Server()->ClientName(Killer),
		m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	EndSpree(Killer);
	
	// send the kill message, except for when are sacrificed (openfng)
	// because mod gamectrl will create it in that case
	if (!NoKillMsg)
	{
		CNetMsg_Sv_KillMsg Msg;
		Msg.m_Killer = Killer;
		Msg.m_Victim = m_pPlayer->GetCID();
		Msg.m_Weapon = Weapon;
		Msg.m_ModeSpecial = ModeSpecial;

	//if (GetFreezeTicks() <= 0 || WasFrozenBy() < 0 || 
	  //                      !(GameServer()->IsClientReady(WasFrozenBy()) && GameServer()->IsClientPlayer(WasFrozenBy())))
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon)
{
	m_Core.m_Vel += Force;

	if(!g_Config.m_SvDamage || (GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && !g_Config.m_SvTeamdamage))
		return false;
	
	if(GameServer()->m_pController->IsOpenFng())
	{
		if(GameServer()->GetPlayerChar(From))
			GameServer()->GetPlayerChar(From)->CheckBot(this);
		
		/*
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From] && GameServer()->m_apPlayers[From]->m_LastEmoticon < Server()->Tick())
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				GameServer()->m_apPlayers[From]->m_LastEmoticon = Server()->Tick() + Server()->TickSpeed()*g_Config.m_SvEmoticonDelay;
				GameServer()->SendEmoticon(From, EMOTICON_EYES);
				GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_PLAYER_SPAWN, CmaskOne(From));
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed()*g_Config.m_SvEmoticonDelay;
			}
		}*/
		
		if(m_pPlayer->m_Invisible)
	    {
		    m_Invisible = 0;
		    m_InvisibleTick = 0;
		    //new CShield(&GameServer()->m_World, m_Pos, GetPlayer()->GetCID(), 1);
	    }
		
		if(m_pPlayer->m_PlayerFlags == PLAYERFLAG_CHATTING && Weapon != WEAPON_HAMMER && !GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && GetFreezeTicks() <= 0 && g_Config.m_SvShowChatkills)
		{
			char abuf[256];
			str_format(abuf, sizeof(abuf), "%s made a chatkill!", Server()->ClientName(From));
			GameServer()->SendChat(-1, CGameContext::CHAT_ALL, abuf);
		}
		return true;
	}

	// m_pPlayer only inflicts half damage on self
	if(From == m_pPlayer->GetCID())
		Dmg = max(1, Dmg/2);

	m_DamageTaken++;

	// create healthmod indicator
	if(Server()->Tick() < m_DamageTakenTick+25)
	{
		// make sure that the damage indicators doesn't group together
		GameServer()->CreateDamageInd(m_Pos, m_DamageTaken*0.25f, Dmg);
	}
	else
	{
		m_DamageTaken = 0;
		GameServer()->CreateDamageInd(m_Pos, 0, Dmg);
	}

	if(Dmg)
	{
		if(m_Armor)
		{
			if(Dmg > 1)
			{
				m_Health--;
				Dmg--;
			}

			if(Dmg > m_Armor)
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
			else
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
		}

		m_Health -= Dmg;
	}

	m_DamageTakenTick = Server()->Tick();

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
	{
		int Mask = CmaskOne(From);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS && GameServer()->m_apPlayers[i]->m_SpectatorID == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}

	// check for death
	if(m_Health <= 0)
	{
		Die(From, Weapon);

		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		return false;
	}

	if (Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

	m_EmoteType = EMOTE_PAIN;
	m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;

	return true;
}

void CCharacter::Bleed(int Ticks)
{
	m_BloodTicks = Ticks;
}

void CCharacter::Snap(int SnappingClient)
{
	CNetObj_Character Measure; // used only for measuring the offset between vanilla and extended core
	if(NetworkClipped(SnappingClient))
		return;
	
	if(GetPlayer()->m_Invisible && GetPlayer()->GetCID() != SnappingClient && m_Invisible == 1 && GameServer()->m_apPlayers[SnappingClient]->GetTeam() != TEAM_SPECTATORS && GetFreezeTicks() <= 0 && !GetPlayer()->m_Paused)
		return;
	
	if(SnappingClient > -1)
	{
		CCharacter* SnapChar = GameServer()->GetPlayerChar(SnappingClient);
		CPlayer* SnapPlayer = GameServer()->m_apPlayers[SnappingClient];

		if((SnapPlayer->GetTeam() == TEAM_SPECTATORS || SnapPlayer->m_Paused) && SnapPlayer->m_SpectatorID != -1)
			return;
	}

	if (m_Paused)
		return;

	IServer::CClientInfo CltInfo;
	Server()->GetClientInfo(SnappingClient, &CltInfo);

	// measure distance between start and and first vanilla field
	size_t Offset = (char*)(&Measure.m_Tick) - (char*)(&Measure);

	// vanilla size for vanilla clients, extended for custom client
	size_t Sz = sizeof (CNetObj_Character) - (CltInfo.m_CustClt?0:Offset);

	// create a snap item of the size the client expects
	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, m_pPlayer->GetCID(), Sz));
	if(!pCharacter)
		return;

	// for vanilla clients, make pCharacter point before the start our snap item start, so that the vanilla core
	// aligns with the snap item. we may not access the extended fields then, since they are out of bounds (to the left)
	if (!CltInfo.m_CustClt)
		pCharacter = (CNetObj_Character*)(((char*)pCharacter)-Offset); // moar cookies.

	// write down the m_Core
	if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter, !CltInfo.m_CustClt);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter, !CltInfo.m_CustClt);
	}

	// set emote
	if (m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = EMOTE_NORMAL;
		m_EmoteStop = -1;
	}

	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;

	pCharacter->m_Weapon = m_ActiveWeapon;
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = m_Armor;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}
	
	if(GetPlayer()->m_Paused)
		pCharacter->m_Emote = EMOTE_BLINK;

	if(pCharacter->m_Emote == EMOTE_NORMAL)
	{
		if(250 - ((Server()->Tick() - m_LastAction)%(250)) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}

	pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
}

void CCharacter::CheckBot(CCharacter *pChr)
{
	vec2 AimPos = m_Pos+vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY);
	if(distance(pChr->m_Pos, AimPos) <= (float)g_Config.m_SvDetectRange || length(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY)) > 1000.f)
	{
		if(!m_pPlayer->m_Detects)
			m_pPlayer->m_ResetDetectsTime = Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvResetDetectsSeconds;

		if(Server()->Tick() > m_pPlayer->m_ResetDetectsTime)
		{
			m_pPlayer->m_Detects = 0;
			m_pPlayer->m_ResetDetectsTime = Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvResetDetectsSeconds;
		}

		m_pPlayer->m_Detects++;
		GameServer()->OnDetect(m_pPlayer->GetCID());

		if(m_pPlayer->m_Detects >= g_Config.m_SvDetectsNeeded)
			m_pPlayer->m_Detects = 0;
	}
}

void CCharacter::Pause(bool Pause)
{
	m_Paused = Pause;
	if(Pause)
	{
		GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
		GameServer()->m_World.RemoveEntity(this);

		if (m_Core.m_HookedPlayer != -1) // Keeping hook would allow cheats
		{
			m_Core.m_HookedPlayer = -1;
			m_Core.m_HookState = HOOK_RETRACTED;
			m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
		}
	}
	else
	{
		m_Core.m_Vel = vec2(0,0);
		GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;
		GameServer()->m_World.InsertEntity(this);
	}
}

/*GoJE GrEEN !*/
void CCharacter::AddSpree()
{
	m_pPlayer->m_Spree++;
	const int NumMsg = 5;
	char aBuf[128];
	
	if(m_pPlayer->m_Spree % g_Config.m_SvKillingspreeKills == 0)
	{
		static const char aaSpreeMsg[NumMsg][32] = { "is on a killing spree", "is on a rampage", "is dominating", "is unstoppable", "is godlike"};
		int No = m_pPlayer->m_Spree/NumMsg-1;
		int bigman = 0,numkills = m_pPlayer->m_Spree, condition = g_Config.m_SvKillingspreeKills;
		if(((numkills / condition)-1) == 0) bigman = 0;
		else if (((numkills / condition)-1) == 1) bigman = 1;
		else if (((numkills / condition)-1) == 2) bigman = 2;
		else if (((numkills / condition)-1) == 3) bigman = 3;
		else if (((numkills / condition)-1) >= 4) bigman = 4;

		str_format(aBuf, sizeof(aBuf), "%s %s with %d kills!", Server()->ClientName(m_pPlayer->GetCID()), aaSpreeMsg[bigman], m_pPlayer->m_Spree);
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
		
		if(g_Config.m_SvKillingspreeAward)
		{
		    if(str_comp("is on a killing spree", aaSpreeMsg[bigman]) == 0 && !m_pPlayer->m_HammerFreeze)
	        {
		        m_pPlayer->m_HammerFreeze = true;
		        GameServer()->SendBroadcast("you got the killingspree award [IceHammer]", m_pPlayer->GetCID());
	        }
		
		    if(str_comp("is on a rampage", aaSpreeMsg[bigman]) == 0 && !m_pPlayer->m_JetPack)
	        {
		        m_pPlayer->m_JetPack = true;
		        GameServer()->SendBroadcast("you got the killingspree award [JetPack]", m_pPlayer->GetCID());
	        }
		
		    if(str_comp("is dominating", aaSpreeMsg[bigman]) == 0 && !m_pPlayer->m_SpeedRunner)
	        {
                m_pPlayer->m_SpeedRunner = true;
		        GameServer()->SendBroadcast("you got the killingspree award [MaxSpeed]", m_pPlayer->GetCID());
	        }
		
		    if(str_comp("is unstoppable", aaSpreeMsg[bigman]) == 0 && !m_pPlayer->m_RifleSpread)
	        {
		        m_pPlayer->m_RifleSpread = true;
		        GameServer()->SendBroadcast("you got the killingspree award [Laser2x]", m_pPlayer->GetCID());
	        }
	
	        if(str_comp("is godlike", aaSpreeMsg[bigman]) == 0 && !m_Core.m_Protected)
	        {
		        m_Core.m_Protected = true;
		        GameServer()->SendBroadcast("you got the killingspree award [Protection]", m_pPlayer->GetCID());
	        }
		
		    if((m_pPlayer->m_Spree == (g_Config.m_SvKillingspreeKills * 5) + g_Config.m_SvKillingspreeKills) && !m_pPlayer->m_Invisible)
	        {
		        m_pPlayer->m_Invisible = true;
				GameServer()->SendBroadcast("you got the killingspree award [Invisibility]", m_pPlayer->GetCID());
	        }
			
			if((m_pPlayer->m_Spree == (g_Config.m_SvKillingspreeKills * 5) + (g_Config.m_SvKillingspreeKills * 2)) && !m_pPlayer->m_RifleSwap)
	        {
		        m_pPlayer->m_RifleSwap = true;
				GameServer()->SendBroadcast("you got the killingspree award [LaserSwap]", m_pPlayer->GetCID());
	        }
			
			if((m_pPlayer->m_Spree == (g_Config.m_SvKillingspreeKills * 5) + (g_Config.m_SvKillingspreeKills * 3)) && !m_pPlayer->m_Pauseable)
	        {
		        m_pPlayer->m_Pauseable = true;
				GameServer()->SendBroadcast("you got the killingspree award [Pauseable]", m_pPlayer->GetCID());
	        }
			
			if((m_pPlayer->m_Spree == (g_Config.m_SvKillingspreeKills * 5) + (g_Config.m_SvKillingspreeKills * 4)) && !m_pPlayer->m_TeamProtect)
	        {
		        m_pPlayer->m_TeamProtect = true;
				GameServer()->SendBroadcast("you got the killingspree award [TeamProtection]", m_pPlayer->GetCID());
	        }
			
			if((m_pPlayer->m_Spree == (g_Config.m_SvKillingspreeKills * 5) + (g_Config.m_SvKillingspreeKills * 5)) && !m_pPlayer->m_GrenadeLauncher)
	        {
		        m_pPlayer->m_GrenadeLauncher = true;
				GameServer()->SendBroadcast("you got the last killingspree award [Grenade]", m_pPlayer->GetCID());
	        }
		}
	}
}

void CCharacter::EndSpree(int Killer)
{
	if(m_pPlayer->m_Spree >= g_Config.m_SvKillingspreeKills)
	{
		if(m_pPlayer->m_Spree >= (g_Config.m_SvKillingspreeKills * 5) + (g_Config.m_SvKillingspreeKills * 2))
		{
		    GameServer()->CreateRingExplosion(m_Pos, m_pPlayer->GetCID(), 1, 15, 10, true);
		}
		else
		{
			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
		    GameServer()->CreateExplosion(m_Pos, m_pPlayer->GetCID(), WEAPON_RIFLE, true);
		}
		
		if(g_Config.m_SvPrintKillingSpree)
        {			
			if(Killer != m_pPlayer->GetCID())
		    {
		        char aBuf[128];
		        str_format(aBuf, sizeof(aBuf), "%s %d-kills killing spree was ended by %s", Server()->ClientName(m_pPlayer->GetCID()), m_pPlayer->m_Spree, Server()->ClientName(Killer));
		        GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
		        GameServer()->SendBroadcast("You lost all of your items", m_pPlayer->GetCID());
		    }
		    else
		    {
		        char aBuf[128];
		        str_format(aBuf, sizeof(aBuf), "%s %d-kills killing spree was ended.", Server()->ClientName(m_pPlayer->GetCID()), m_pPlayer->m_Spree);
		        GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
		        GameServer()->SendBroadcast("You lost all of your items", m_pPlayer->GetCID());
		    }
	    }
	}

	m_pPlayer->m_HammerFreeze = false;
	m_pPlayer->m_JetPack = false;
	m_pPlayer->m_SpeedRunner = false;
	m_pPlayer->m_RifleSpread = false;
	m_Core.m_Protected = false;
	m_pPlayer->m_Invisible = false;
	m_pPlayer->m_RifleSwap = false;
	m_pPlayer->m_Pauseable = false;
	m_pPlayer->m_TeamProtect = false;
	m_pPlayer->m_GrenadeLauncher = false;
	m_pPlayer->m_Spree = 0;
}