#pragma once

class CBasePlayer;
class CBaseMonster;

void Radar_PrecacheSounds();
void Radar_RegisterUserMessages();
void Radar_StartFrame();
void Radar_PlayerDamagedByMonster(CBasePlayer* pPlayer, CBaseMonster* pMonster);
void Radar_MarkPlayerForSilentSync(CBasePlayer* pPlayer);
void Radar_MarkAllPlayersForSilentSync();
