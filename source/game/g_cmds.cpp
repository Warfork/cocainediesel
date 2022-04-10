/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "game/g_local.h"

/*
* G_Teleport
*
* Teleports client to specified position
* If client is not spectator teleporting is only done if position is free and teleport effects are drawn.
*/
static bool G_Teleport( edict_t * ent, Vec3 origin, Vec3 angles ) {
	if( !ent->r.inuse || !ent->r.client ) {
		return false;
	}

	if( ent->r.client->ps.pmove.pm_type != PM_SPECTATOR ) {
		trace_t tr;

		G_Trace( &tr, origin, ent->r.mins, ent->r.maxs, origin, ent, MASK_PLAYERSOLID );
		if( ( tr.fraction != 1.0f || tr.startsolid ) && ( game.edicts[ tr.ent ].s.team != TEAM_PLAYERS && game.edicts[ tr.ent ].s.team != ent->s.team ) ) {
			return false;
		}

		G_TeleportEffect( ent, false );
	}

	ent->s.origin = origin;
	ent->olds.origin = origin;
	ent->s.teleported = true;

	ent->velocity = Vec3( 0.0f );
	ent->r.client->ps.pmove.pm_time = 1;
	ent->r.client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;

	if( ent->r.client->ps.pmove.pm_type != PM_SPECTATOR ) {
		G_TeleportEffect( ent, true );
	}

	// set angles
	ent->s.angles = angles;
	ent->r.client->ps.viewangles = angles;

	// set the delta angle
	ent->r.client->ps.pmove.delta_angles[ 0 ] = ANGLE2SHORT( ent->r.client->ps.viewangles.x ) - ent->r.client->ucmd.angles[ 0 ];
	ent->r.client->ps.pmove.delta_angles[ 1 ] = ANGLE2SHORT( ent->r.client->ps.viewangles.y ) - ent->r.client->ucmd.angles[ 1 ];
	ent->r.client->ps.pmove.delta_angles[ 2 ] = ANGLE2SHORT( ent->r.client->ps.viewangles.z ) - ent->r.client->ucmd.angles[ 2 ];

	return true;
}


//=================================================================================

static void Cmd_Noclip_f( edict_t * ent, msg_t args ) {
	const char *msg;

	if( sv_cheats->integer == 0 ) {
		G_PrintMsg( ent, "Cheats are not enabled on this server.\n" );
		return;
	}

	if( ent->movetype == MOVETYPE_NOCLIP ) {
		ent->movetype = MOVETYPE_PLAYER;
		msg = "noclip OFF\n";
	} else {
		ent->movetype = MOVETYPE_NOCLIP;
		msg = "noclip ON\n";
	}

	G_PrintMsg( ent, "%s", msg );
}

static void Cmd_GameOperator_f( edict_t * ent, msg_t args ) {
	if( StrEqual( g_operator_password->value, "" ) ) {
		G_PrintMsg( ent, "Operator is disabled in this server\n" );
		return;
	}

	if( Cmd_Argc() < 2 ) {
		G_PrintMsg( ent, "Usage: 'operator <password>' or 'op <password>'\n" );
		return;
	}

	if( StrEqual( Cmd_Argv( 1 ), g_operator_password->value ) ) {
		if( !ent->r.client->isoperator ) {
			G_PrintMsg( NULL, "%s" S_COLOR_WHITE " is now a game operator\n", ent->r.client->netname );
		}

		ent->r.client->isoperator = true;
		return;
	}

	G_PrintMsg( ent, "Incorrect operator password.\n" );
}

static void Cmd_Kill_f( edict_t * ent, msg_t args ) {
	if( ent->r.solid == SOLID_NOT ) {
		return;
	}

	ent->health = 0;

	// wsw : pb : fix /kill command
	G_Killed( ent, ent, ent, -1, WorldDamage_Suicide, 100000 );
}

void Cmd_ChaseNext_f( edict_t * ent, msg_t args ) {
	G_ChaseStep( ent, 1 );
}

void Cmd_ChasePrev_f( edict_t * ent, msg_t args ) {
	G_ChaseStep( ent, -1 );
}

static void Cmd_Position_f( edict_t * ent, msg_t args ) {
	const char *action;

	if( !sv_cheats->integer && server_gs.gameState.match_state > MatchState_Warmup &&
		ent->r.client->ps.pmove.pm_type != PM_SPECTATOR ) {
		G_PrintMsg( ent, "Position command is only available in warmup and in spectator mode.\n" );
		return;
	}

	// flood protect
	if( ent->r.client->teamstate.position_lastcmd + 500 > svs.realtime ) {
		return;
	}
	ent->r.client->teamstate.position_lastcmd = svs.realtime;

	action = Cmd_Argv( 1 );

	if( StrCaseEqual( action, "save" ) ) {
		ent->r.client->teamstate.position_saved = true;
		ent->r.client->teamstate.position_origin = ent->s.origin;
		ent->r.client->teamstate.position_angles = ent->s.angles;
		G_PrintMsg( ent, "Position saved.\n" );
	} else if( StrCaseEqual( action, "load" ) ) {
		if( !ent->r.client->teamstate.position_saved ) {
			G_PrintMsg( ent, "No position saved.\n" );
		} else {
			if( G_Teleport( ent, ent->r.client->teamstate.position_origin, ent->r.client->teamstate.position_angles ) ) {
				G_PrintMsg( ent, "Position loaded.\n" );
			} else {
				G_PrintMsg( ent, "Position not available.\n" );
			}
		}
	} else if( StrCaseEqual( action, "set" ) && Cmd_Argc() == 7 ) {
		Vec3 origin = Vec3( atof( Cmd_Argv( 2 ) ), atof( Cmd_Argv( 3 ) ), atof( Cmd_Argv( 4 ) ) );
		Vec3 angles = Vec3( atof( Cmd_Argv( 5 ) ), atof( Cmd_Argv( 6 ) ), 0.0f );

		if( G_Teleport( ent, origin, angles ) ) {
			G_PrintMsg( ent, "Position not available.\n" );
		} else {
			G_PrintMsg( ent, "Position set.\n" );
		}
	} else {
		G_PrintMsg( ent,
			"Usage:\n"
			"position save - Save current position\n"
			"position load - Teleport to saved position\n"
			"position set <x> <y> <z> <pitch> <yaw> - Teleport to specified position\n"
			"Current position: %.4f %.4f %.4f %.4f %.4f\n",
			ent->s.origin.x, ent->s.origin.y, ent->s.origin.z, ent->s.angles.x, ent->s.angles.y );
	}
}

bool CheckFlood( edict_t * ent, bool teamonly ) {
	int i;
	gclient_t *client;

	assert( ent != NULL );

	client = ent->r.client;
	assert( client != NULL );

	if( g_floodprotection_messages->modified ) {
		if( g_floodprotection_messages->integer < 0 ) {
			Cvar_Set( "g_floodprotection_messages", "0" );
		}
		if( g_floodprotection_messages->integer > MAX_FLOOD_MESSAGES ) {
			Cvar_SetInteger( "g_floodprotection_messages", MAX_FLOOD_MESSAGES );
		}
		g_floodprotection_messages->modified = false;
	}

	if( g_floodprotection_team->modified ) {
		if( g_floodprotection_team->integer < 0 ) {
			Cvar_Set( "g_floodprotection_team", "0" );
		}
		if( g_floodprotection_team->integer > MAX_FLOOD_MESSAGES ) {
			Cvar_SetInteger( "g_floodprotection_team", MAX_FLOOD_MESSAGES );
		}
		g_floodprotection_team->modified = false;
	}

	if( g_floodprotection_seconds->modified ) {
		if( g_floodprotection_seconds->number <= 0 ) {
			Cvar_Set( "g_floodprotection_seconds", "4" );
		}
		g_floodprotection_seconds->modified = false;
	}

	if( g_floodprotection_penalty->modified ) {
		if( g_floodprotection_penalty->number < 0 ) {
			Cvar_Set( "g_floodprotection_penalty", "10" );
		}
		g_floodprotection_penalty->modified = false;
	}

	// old protection still active
	if( !teamonly || g_floodprotection_team->integer ) {
		if( svs.realtime < client->level.flood_locktill ) {
			G_PrintMsg( ent, "You can't talk for %d more seconds\n",
						(int)( ( client->level.flood_locktill - svs.realtime ) / 1000.0f ) + 1 );
			return true;
		}
	}


	if( teamonly ) {
		if( g_floodprotection_team->integer && g_floodprotection_penalty->number > 0 ) {
			i = client->level.flood_team_whenhead - g_floodprotection_team->integer + 1;
			if( i < 0 ) {
				i = MAX_FLOOD_MESSAGES + i;
			}

			if( client->level.flood_team_when[i] && client->level.flood_team_when[i] <= svs.realtime &&
				( svs.realtime < client->level.flood_team_when[i] + g_floodprotection_seconds->integer * 1000 ) ) {
				client->level.flood_locktill = svs.realtime + g_floodprotection_penalty->integer * 1000;
				G_PrintMsg( ent, "Flood protection: You can't talk for %d seconds.\n", g_floodprotection_penalty->integer );
				return true;
			}
		}

		client->level.flood_team_whenhead = ( client->level.flood_team_whenhead + 1 ) % MAX_FLOOD_MESSAGES;
		client->level.flood_team_when[client->level.flood_team_whenhead] = svs.realtime;
	} else {
		if( g_floodprotection_messages->integer && g_floodprotection_penalty->number > 0 ) {
			i = client->level.flood_whenhead - g_floodprotection_messages->integer + 1;
			if( i < 0 ) {
				i = MAX_FLOOD_MESSAGES + i;
			}

			if( client->level.flood_when[i] && client->level.flood_when[i] <= svs.realtime &&
				( svs.realtime < client->level.flood_when[i] + g_floodprotection_seconds->integer * 1000 ) ) {
				client->level.flood_locktill = svs.realtime + g_floodprotection_penalty->integer * 1000;
				G_PrintMsg( ent, "Flood protection: You can't talk for %d seconds.\n", g_floodprotection_penalty->integer );
				return true;
			}
		}

		client->level.flood_whenhead = ( client->level.flood_whenhead + 1 ) % MAX_FLOOD_MESSAGES;
		client->level.flood_when[client->level.flood_whenhead] = svs.realtime;
	}

	return false;
}

void Cmd_Say_f( edict_t * ent, bool arg0, bool checkflood ) {
	char *p;
	char text[2048];
	size_t arg0len = 0;

	if( checkflood ) {
		if( CheckFlood( ent, false ) ) {
			return;
		}
	}

	if( Cmd_Argc() < 2 && !arg0 ) {
		return;
	}

	text[0] = 0;

	if( arg0 ) {
		Q_strncatz( text, Cmd_Argv( 0 ), sizeof( text ) );
		Q_strncatz( text, " ", sizeof( text ) );
		arg0len = strlen( text );
		Q_strncatz( text, Cmd_Args(), sizeof( text ) );
	} else {
		p = Cmd_Args();
		Q_strncatz( text, p, sizeof( text ) );
	}

	// don't let text be too long for malicious reasons
	text[arg0len + ( MAX_CHAT_BYTES - 1 )] = 0;

	G_ChatMsg( NULL, ent, false, "%s", text );
}

static void Cmd_SayCmd_f( edict_t * ent, msg_t args ) {
	if( !G_ISGHOSTING( ent ) ) {
		edict_t * event = G_PositionedSound( ent->s.origin, "sounds/typewriter/return" );
		event->s.ownerNum = ent->s.number;
		event->s.svflags |= SVF_NEVEROWNER;
	}
	Cmd_Say_f( ent, false, true );
}

static void Cmd_SayTeam_f( edict_t * ent, msg_t args ) {
	if( !G_ISGHOSTING( ent ) ) {
		edict_t * event = G_PositionedSound( ent->s.origin, "sounds/typewriter/return" );
		event->s.ownerNum = ent->s.number;
		event->s.svflags |= SVF_NEVEROWNER;
	}
	G_Say_Team( ent, Cmd_Args(), true );
}

static void Cmd_Clack_f( edict_t * ent, msg_t args ) {
	bool space = StrCaseEqual( Cmd_Argv( 0 ), "typewriterspace" );
	if( !G_ISGHOSTING( ent ) ) {
		StringHash sound = space ? StringHash( "sounds/typewriter/space" ) : StringHash( "sounds/typewriter/clack" );
		edict_t * event = G_PositionedSound( ent->s.origin, sound );
		event->s.ownerNum = ent->s.number;
		event->s.svflags |= SVF_NEVEROWNER;
	}
}

static void Cmd_Spray_f( edict_t * ent, msg_t args ) {
	if( G_ISGHOSTING( ent ) )
		return;

	if( ent->r.client->level.last_spray + 2500 > svs.realtime )
		return;

	Vec3 forward;
	AngleVectors( ent->r.client->ps.viewangles, &forward, NULL, NULL );

	constexpr float range = 96.0f;
	Vec3 start = ent->s.origin + Vec3( 0.0f, 0.0f, ent->r.client->ps.viewheight );
	Vec3 end = start + forward * range;

	trace_t trace;
	G_Trace( &trace, start, Vec3( 0.0f ), Vec3( 0.0f ), end, ent, MASK_OPAQUE );

	if( trace.ent != 0 )
		return;

	ent->r.client->level.last_spray = svs.realtime;

	edict_t * event = G_SpawnEvent( EV_SPRAY, Random64( &svs.rng ), &trace.endpos );
	event->s.angles = ent->r.client->ps.viewangles;
	event->s.scale = ent->s.scale;
	event->s.origin2 = trace.plane.normal;
}

struct g_vsays_t {
	const char *name;
	int id;
};

static const g_vsays_t g_vsays[] = {
	{ "sorry", Vsay_Sorry },
	{ "thanks", Vsay_Thanks },
	{ "goodgame", Vsay_GoodGame },
	{ "boomstick", Vsay_BoomStick },
	{ "acne", Vsay_Acne },
	{ "valley", Vsay_Valley },
	{ "mike", Vsay_Mike },
	{ "user", Vsay_User },
	{ "guyman", Vsay_Guyman },
	{ "helena", Vsay_Helena },
	{ "fart", Vsay_Fart },
	{ "zombie", Vsay_Zombie },
	{ "larp", Vsay_Larp },

	{ NULL, 0 }
};

static void G_vsay_f( edict_t * ent, msg_t args ) {
	if( G_ISGHOSTING( ent ) && server_gs.gameState.match_state < MatchState_PostMatch ) {
		return;
	}

	if( ent->r.client->level.last_vsay > svs.realtime - 500 ) {
		return;
	}
	ent->r.client->level.last_vsay = svs.realtime;

	for( const g_vsays_t * vsay = g_vsays; vsay->name; vsay++ ) {
		if( !StrCaseEqual( Cmd_Argv( 1 ), vsay->name ) )
			continue;

		u64 entropy = Random32( &svs.rng );
		u64 parm = u64( vsay->id ) | ( entropy << 16 );

		edict_t * event = G_SpawnEvent( EV_VSAY, parm, NULL );
		event->s.svflags |= SVF_BROADCAST; // force sending even when not in PVS
		event->s.ownerNum = ent->s.number;

		return;
	}

	G_PrintMsg( ent, "Unknown vsay %s", Cmd_Argv( 1 ) );
}

static void Cmd_Join_f( edict_t * ent, msg_t args ) {
	if( CheckFlood( ent, false ) ) {
		return;
	}

	G_Teams_Join_Cmd( ent, args );
}

static void Cmd_Timeout_f( edict_t * ent, msg_t args ) {
	int num;

	if( ent->s.team == TEAM_SPECTATOR || server_gs.gameState.match_state != MatchState_Playing ) {
		return;
	}

	if( level.gametype.isTeamBased ) {
		num = ent->s.team;
	} else {
		num = ENTNUM( ent ) - 1;
	}

	if( GS_MatchPaused( &server_gs ) && ( level.timeout.endtime - level.timeout.time ) >= 2 * TIMEIN_TIME ) {
		G_PrintMsg( ent, "Timeout already in progress\n" );
		return;
	}

	if( g_maxtimeouts->integer != -1 && level.timeout.used[num] >= g_maxtimeouts->integer ) {
		if( g_maxtimeouts->integer == 0 ) {
			G_PrintMsg( ent, "Timeouts are not allowed on this server\n" );
		} else if( level.gametype.isTeamBased ) {
			G_PrintMsg( ent, "Your team doesn't have any timeouts left\n" );
		} else {
			G_PrintMsg( ent, "You don't have any timeouts left\n" );
		}
		return;
	}

	G_PrintMsg( NULL, "%s%s called a timeout\n", ent->r.client->netname, S_COLOR_WHITE );

	if( !GS_MatchPaused( &server_gs ) ) {
		G_AnnouncerSound( NULL, StringHash( "sounds/announcer/timeout" ), GS_MAX_TEAMS, true, NULL );
	}

	level.timeout.used[num]++;
	G_GamestatSetFlag( GAMESTAT_FLAG_PAUSED, true );
	level.timeout.caller = num;
	level.timeout.endtime = level.timeout.time + TIMEOUT_TIME + FRAMETIME;
}

static void Cmd_Timein_f( edict_t * ent, msg_t args ) {
	int num;

	if( ent->s.team == TEAM_SPECTATOR ) {
		return;
	}

	if( !GS_MatchPaused( &server_gs ) ) {
		G_PrintMsg( ent, "No timeout in progress.\n" );
		return;
	}

	if( level.timeout.endtime - level.timeout.time <= 2 * TIMEIN_TIME ) {
		G_PrintMsg( ent, "The timeout is about to end already.\n" );
		return;
	}

	if( level.gametype.isTeamBased ) {
		num = ent->s.team;
	} else {
		num = ENTNUM( ent ) - 1;
	}

	if( level.timeout.caller != num ) {
		if( level.gametype.isTeamBased ) {
			G_PrintMsg( ent, "Your team didn't call this timeout.\n" );
		} else {
			G_PrintMsg( ent, "You didn't call this timeout.\n" );
		}
		return;
	}

	level.timeout.endtime = level.timeout.time + TIMEIN_TIME + FRAMETIME;

	G_AnnouncerSound( NULL, StringHash( "sounds/announcer/timein" ), GS_MAX_TEAMS, true, NULL );

	G_PrintMsg( NULL, "%s%s called a timein\n", ent->r.client->netname, S_COLOR_WHITE );
}

//===========================================================
//	client commands
//===========================================================

static gamecommandfunc_t g_Commands[ ClientCommand_Count ];

void G_AddCommand( ClientCommandType command, gamecommandfunc_t callback ) {
	assert( g_Commands[ command ] == NULL );
	g_Commands[ command ] = callback;
}

void G_InitGameCommands() {
	memset( g_Commands, 0, sizeof( g_Commands ) );

	G_AddCommand( ClientCommand_Position, Cmd_Position_f );
	G_AddCommand( ClientCommand_Say, Cmd_SayCmd_f );
	G_AddCommand( ClientCommand_SayTeam, Cmd_SayTeam_f );
	G_AddCommand( ClientCommand_Noclip, Cmd_Noclip_f );
	G_AddCommand( ClientCommand_Suicide, Cmd_Kill_f );
	G_AddCommand( ClientCommand_Spectate, []( edict_t * ent, msg_t args ) { Cmd_Spectate( ent ); } );
	G_AddCommand( ClientCommand_ChaseNext, Cmd_ChaseNext_f );
	G_AddCommand( ClientCommand_ChasePrev, Cmd_ChasePrev_f );
	G_AddCommand( ClientCommand_ToggleFreeFly, []( edict_t * ent, msg_t args ) { Cmd_ToggleFreeFly( ent ); } );
	G_AddCommand( ClientCommand_Timeout, Cmd_Timeout_f );
	G_AddCommand( ClientCommand_Timein, Cmd_Timein_f );
	G_AddCommand( ClientCommand_DemoList, []( edict_t * ent, msg_t args ) { SV_DemoList_f( ent ); } );
	G_AddCommand( ClientCommand_DemoGetURL, SV_DemoGetUrl_f );

	// callvotes commands
	G_AddCommand( ClientCommand_Callvote, G_CallVote_Cmd );
	G_AddCommand( ClientCommand_Vote, G_CallVotes_CmdVote );

	G_AddCommand( ClientCommand_Operator, Cmd_GameOperator_f );
	G_AddCommand( ClientCommand_OpCall, G_OperatorVote_Cmd );

	// teams commands
	G_AddCommand( ClientCommand_Ready, []( edict_t * ent, msg_t args ) { G_Match_Ready( ent ); } );
	G_AddCommand( ClientCommand_Unready, []( edict_t * ent, msg_t args ) { G_Match_NotReady( ent ); } );
	G_AddCommand( ClientCommand_ToggleReady, []( edict_t * ent, msg_t args ) { G_Match_ToggleReady( ent ); } );
	G_AddCommand( ClientCommand_Join, Cmd_Join_f );

	G_AddCommand( ClientCommand_TypewriterClack, Cmd_Clack_f );
	G_AddCommand( ClientCommand_TypewriterSpace, Cmd_Clack_f );

	G_AddCommand( ClientCommand_Spray, Cmd_Spray_f );

	G_AddCommand( ClientCommand_Vsay, G_vsay_f );
}

void ClientCommand( edict_t * ent, ClientCommandType command, msg_t args ) {
	G_Client_UpdateActivity( ent->r.client ); // activity detected

	if( g_Commands[ command ] != NULL ) {
		g_Commands[ command ]( ent, args );
	}
}
