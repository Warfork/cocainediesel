#include "qcommon/base.h"
#include "qcommon/array.h"
#include "qcommon/locked.h"
#include "qcommon/threads.h"
#include "qcommon/time.h"
#include "qcommon/version.h"
#include "client/client.h"
#include "client/server_browser.h"

#include "tracy/Tracy.hpp"

struct MasterServer {
	NetAddress address;
	Thread * resolver_thread;
	bool query_next_frame;
};

struct MasterServers {
	MasterServer servers[ ARRAY_COUNT( MASTER_SERVERS ) ];
	size_t num_dns_queries_in_flight;
};

static Locked< MasterServers > locked_master_servers;
static NonRAIIDynamicArray< ServerBrowserEntry > servers;

void InitServerBrowser() {
	locked_master_servers.data = { };
	locked_master_servers.mutex = NewMutex();

	servers.init( sys_allocator );
}

void ShutdownServerBrowser() {
	servers.shutdown();

	// leak the mutex and resolver threads because we can't shut them down
	// quickly and reliably
}

Span< const ServerBrowserEntry > GetServerBrowserEntries() {
	return servers.span();
}

static void GetMasterServerAddress( void * data ) {
#if TRACY_ENABLE
	tracy::SetThreadName( "Master server resolver" );
#endif

	size_t idx = size_t( uintptr_t( data ) );

	NetAddress address = NULL_ADDRESS;
	bool ok = DNS( MASTER_SERVERS[ idx ], &address );
	if( !ok ) {
		Com_Printf( "Failed to resolve master server address: %s\n", MASTER_SERVERS[ idx ] );
	}
	else {
		address.port = PORT_MASTER;
	}

	DoUnderLock( &locked_master_servers, [ idx, address ]( MasterServers * master_servers ) {
		master_servers->servers[ idx ].address = address;
		master_servers->servers[ idx ].query_next_frame = true;
		master_servers->num_dns_queries_in_flight--;
	} );
}

static void QueryMasterServer( MasterServer * master ) {
	const char * command = master->address.family == AddressFamily_IPv4 ? "getservers" : "getserversExt";

	TempAllocator temp = cls.frame_arena.temp();
	const char * query = temp( "{} {} {} full empty", command, APPLICATION_NOSPACES, s32( APP_PROTOCOL_VERSION ) );
	Netchan_OutOfBandPrint( cls.socket, master->address, "%s", query );
	master->query_next_frame = false;
}

void ServerBrowserFrame() {
	DoUnderLock( &locked_master_servers, []( MasterServers * master_servers ) {
		for( MasterServer & master : master_servers->servers ) {
			if( master.query_next_frame ) {
				JoinThread( master.resolver_thread );
				QueryMasterServer( &master );
			}
		}
	} );
}

void RefreshServerBrowser() {
	servers.clear();

	// query LAN servers
	{
		TempAllocator temp = cls.frame_arena.temp();
		const char * query = temp( "info {}", cls.monotonicTime.flicks );

		NetAddress broadcast = GetBroadcastAddress( PORT_SERVER );
		Netchan_OutOfBandPrint( cls.socket, broadcast, "%s", query );
	}

	// query master servers
	DoUnderLock( &locked_master_servers, []( MasterServers * master_servers ) {
		for( MasterServer & master : master_servers->servers ) {
			QueryMasterServer( &master );
		}

		if( master_servers->num_dns_queries_in_flight == 0 ) {
			for( size_t i = 0; i < ARRAY_COUNT( MASTER_SERVERS ); i++ ) {
				if( master_servers->servers[ i ].address == NULL_ADDRESS ) {
					master_servers->servers[ i ].resolver_thread = NewThread( GetMasterServerAddress, ( void * ) uintptr_t( i ) );
				}
			}
		}
	} );
}

void ParseMasterServerResponse( msg_t * msg, bool allow_ipv6 ) {
	MSG_BeginReading( msg );
	MSG_ReadInt32( msg ); // skip the -1
	MSG_SkipData( msg, strlen( allow_ipv6 ? "getserversExtResponse" : "getServersResponse" ) );

	size_t old_num_servers = servers.size();
	bool ok = false;

	while( true ) {
		char separator = MSG_ReadInt8( msg );
		NetAddress address = NULL_ADDRESS;

		if( separator == '\\' ) {
			address.family = AddressFamily_IPv4;
			MSG_ReadData( msg, &address.ipv4, sizeof( address.ipv4 ) );
		}
		else if( separator == '/' ) {
			if( !allow_ipv6 ) {
				Com_Printf( "Master server responded with an IPv6 address when we only asked for IPv4\n" );
				break;
			}

			address.family = AddressFamily_IPv6;
			MSG_ReadData( msg, &address.ipv6, sizeof( address.ipv6 ) );
		}
		else {
			Com_Printf( "Bad separator in master server response\n" );
			break;
		}

		address.port = Bswap( MSG_ReadUint16( msg ) );

		if( address.port == 0 && msg->readcount == msg->cursize ) {
			ok = true;
			break;
		}

		if( msg->readcount > msg->cursize ) {
			Com_Printf( "Truncated master server response\n" );
			break;
		}

		bool is_new = true;
		for( const ServerBrowserEntry & server : servers ) {
			if( server.address == address ) {
				is_new = false;
				break;
			}
		}

		if( is_new ) {
			ServerBrowserEntry server = { };
			server.address = address;
			servers.add( server );
		}
	}

	if( ok ) {
		// query game servers
		TempAllocator temp = cls.frame_arena.temp();
		const char * query = temp( "info {}", cls.monotonicTime.flicks );

		for( size_t i = old_num_servers; i < servers.size(); i++ ) {
			NetAddress address = servers[ i ].address;
			Netchan_OutOfBandPrint( cls.socket, address, "%s", query );
		}
	}
	else {
		servers.resize( old_num_servers );
	}
}

void ParseGameServerResponse( msg_t * msg, const NetAddress & address ) {
	Time timestamp;
	char name[ 128 ];
	char map[ 32 ];
	int num_players;
	int max_players;

	const char * info = MSG_ReadString( msg );
	int parsed = sscanf( info, "%" SCNu64 "\\\\n\\\\%127[^\\]\\\\m\\\\%31[^\\]\\\\u\\\\%d/%d\\\\EOT", &timestamp.flicks, name, map, &num_players, &max_players );
	if( parsed != 5 ) {
		return;
	}

	ServerBrowserEntry * server = NULL;
	for( ServerBrowserEntry & s : servers ) {
		if( s.address == address ) {
			server = &s;
			break;
		}
	}

	if( server == NULL ) {
		server = servers.add();
		server->address = address;
	}

	server->have_details = true;
	Q_strncpyz( server->name, name, sizeof( server->name ) );
	Q_strncpyz( server->map, map, sizeof( server->map ) );
	server->ping = ToSeconds( Min2( cls.monotonicTime - timestamp, Milliseconds( 9999 ) ) ) * 1000.0f;
	server->num_players = num_players;
	server->max_players = max_players;
}
