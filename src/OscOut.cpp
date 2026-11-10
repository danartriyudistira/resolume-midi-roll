#include "OscOut.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <cstring>

namespace midiroll
{
	OscOut::OscOut() = default;

	OscOut::~OscOut()
	{
		Close();
	}

	bool OscOut::Open( int port )
	{
		Close();

		WSADATA data = {};
		if( WSAStartup( MAKEWORD( 2, 2 ), &data ) != 0 )
			return false;

		winsockReady_ = true;

		const SOCKET socket = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
		if( socket == INVALID_SOCKET )
		{
			WSACleanup();
			winsockReady_ = false;
			return false;
		}

		// Never let a full send buffer block the render thread.
		u_long nonBlocking = 1;
		ioctlsocket( socket, FIONBIO, &nonBlocking );

		socket_     = static_cast< unsigned long long >( socket );
		targetIp_   = inet_addr( "127.0.0.1" );
		targetPort_ = htons( static_cast< unsigned short >( port ) );
		return true;
	}

	void OscOut::Close()
	{
		if( socket_ != ~0ull )
		{
			closesocket( static_cast< SOCKET >( socket_ ) );
			socket_ = ~0ull;
		}

		if( winsockReady_ )
		{
			WSACleanup();
			winsockReady_ = false;
		}
	}

	bool OscOut::IsOpen() const
	{
		return socket_ != ~0ull;
	}

	void OscOut::SendFloat( const char* address, float value )
	{
		if( socket_ == ~0ull || address == nullptr )
			return;

		// OSC: address (null padded to 4), type tag ",f" (padded to 4), then a
		// big-endian float. Everything fits in 256 bytes for our fixed paths.
		char buffer[ 256 ] = {};
		const size_t addressLength = std::strlen( address );

		if( addressLength + 16 > sizeof( buffer ) )
			return;

		std::memcpy( buffer, address, addressLength );
		size_t offset = ( addressLength + 4 ) & ~static_cast< size_t >( 3 );

		buffer[ offset++ ] = ',';
		buffer[ offset++ ] = 'f';
		offset = ( offset + 4 ) & ~static_cast< size_t >( 3 );

		std::uint32_t bits = 0;
		std::memcpy( &bits, &value, sizeof( bits ) );
		bits = htonl( bits );
		std::memcpy( buffer + offset, &bits, sizeof( bits ) );
		offset += sizeof( bits );

		sockaddr_in target = {};
		target.sin_family      = AF_INET;
		target.sin_addr.s_addr = targetIp_;
		target.sin_port        = targetPort_;

		sendto( static_cast< SOCKET >( socket_ ), buffer, static_cast< int >( offset ), 0,
			reinterpret_cast< const sockaddr* >( &target ), sizeof( target ) );
	}
}
