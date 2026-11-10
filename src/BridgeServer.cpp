#include "BridgeServer.h"

#include "BrowserLauncher.h"

// winsock2 must precede windows.h.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace midiroll
{
	namespace
	{
		constexpr size_t kPatternLength = 128; // 4 lanes x 32 steps
		constexpr int kRecvTimeoutMs    = 1500;

		std::string Lower( std::string value )
		{
			std::transform( value.begin(), value.end(), value.begin(), []( unsigned char c ) {
				return static_cast< char >( std::tolower( c ) );
			} );
			return value;
		}

		std::string QueryValue( const std::string& target, const std::string& key )
		{
			const size_t question = target.find( '?' );
			if( question == std::string::npos )
				return std::string();

			const std::string query = target.substr( question + 1 );
			size_t cursor           = 0;

			while( cursor < query.size() )
			{
				const size_t next = query.find( '&', cursor );
				const std::string pair = query.substr( cursor, next == std::string::npos ? std::string::npos : next - cursor );

				const size_t equals = pair.find( '=' );
				if( equals != std::string::npos && pair.substr( 0, equals ) == key )
					return pair.substr( equals + 1 );

				if( next == std::string::npos )
					break;

				cursor = next + 1;
			}

			return std::string();
		}

		int SlotFromQuery( const std::string& target )
		{
			const std::string value = Lower( QueryValue( target, "slot" ) );
			if( value.size() == 1 && value[ 0 ] >= 'a' && value[ 0 ] < 'a' + BridgeServer::kSlots )
				return value[ 0 ] - 'a';

			return -1;
		}

		std::string BuildResponse( int status, const std::string& contentType, const std::string& body )
		{
			const char* reason = status == 200 ? "OK" : status == 204 ? "No Content" : status == 404 ? "Not Found" : "Bad Request";

			std::ostringstream out;
			out << "HTTP/1.1 " << status << " " << reason << "\r\n"
			    << "Access-Control-Allow-Origin: *\r\n"
			    << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
			    << "Access-Control-Allow-Headers: Content-Type\r\n"
			    << "Cache-Control: no-store\r\n"
			    << "Connection: close\r\n";

			if( status != 204 )
			{
				out << "Content-Type: " << contentType << "\r\n"
				    << "Content-Length: " << body.size() << "\r\n";
			}

			out << "\r\n";

			if( status != 204 )
				out << body;

			return out.str();
		}

		std::string ReadGuiPage()
		{
			const std::wstring path = browser::GuiPath();
			if( path.empty() )
				return std::string();

			std::ifstream file( path, std::ios::binary );
			if( !file )
				return std::string();

			std::ostringstream contents;
			contents << file.rdbuf();
			return contents.str();
		}

		std::string ReadFileBytes( const std::wstring& path )
		{
			std::ifstream file( path, std::ios::binary );
			if( !file )
				return std::string();

			std::ostringstream contents;
			contents << file.rdbuf();
			return contents.str();
		}

		std::wstring ToWide( const std::string& text )
		{
			return std::wstring( text.begin(), text.end() );
		}

		std::string ContentType( const std::string& path )
		{
			const auto endsWith = [ &path ]( const char* suffix ) {
				const size_t length = std::strlen( suffix );
				return path.size() >= length && path.compare( path.size() - length, length, suffix ) == 0;
			};

			if( endsWith( ".css" ) ) return "text/css";
			if( endsWith( ".js" ) ) return "application/javascript";
			if( endsWith( ".svg" ) ) return "image/svg+xml";
			if( endsWith( ".png" ) ) return "image/png";
			if( endsWith( ".ico" ) ) return "image/x-icon";
			if( endsWith( ".woff2" ) ) return "font/woff2";
			if( endsWith( ".woff" ) ) return "font/woff";

			return "application/octet-stream";
		}
	}

	BridgeServer& BridgeServer::Instance()
	{
		// Intentionally leaked: the listener must outlive any static teardown
		// order at process exit. Release() stops the thread when unused.
		static BridgeServer* instance = new BridgeServer();
		return *instance;
	}

	BridgeServer::BridgeServer()
	{
		for( std::string& slot : slots_ )
			slot = std::string( kPatternLength, '0' );
	}

	BridgeServer::~BridgeServer()
	{
		Stop();
	}

	void BridgeServer::Acquire()
	{
		if( ++refCount_ == 1 )
			Start();
	}

	void BridgeServer::Release()
	{
		if( refCount_ <= 0 )
			return;

		if( --refCount_ == 0 )
			Stop();
	}

	bool BridgeServer::IsListening() const
	{
		return listening_;
	}

	std::string BridgeServer::Origin() const
	{
		return "http://127.0.0.1:" + std::to_string( kPort );
	}

	void BridgeServer::Start()
	{
		if( running_ )
			return;

		running_ = true;
		thread_  = std::thread( [ this ] { Run(); } );
	}

	void BridgeServer::Stop()
	{
		if( !running_ )
			return;

		running_ = false;

		if( thread_.joinable() )
			thread_.join();

		if( listenSocket_ != 0 )
		{
			closesocket( static_cast< SOCKET >( listenSocket_ ) );
			listenSocket_ = 0;
		}

		listening_ = false;
		WSACleanup();
	}

	void BridgeServer::Run()
	{
		WSADATA wsa = {};
		if( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 )
		{
			running_ = false;
			return;
		}

		SOCKET listener = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		if( listener == INVALID_SOCKET )
		{
			running_ = false;
			return;
		}

		// Lets a restart of Resolume rebind immediately without TIME_WAIT drama.
		BOOL reuse = TRUE;
		setsockopt( listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast< const char* >( &reuse ), sizeof( reuse ) );

		sockaddr_in address = {};
		address.sin_family      = AF_INET;
		address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
		address.sin_port        = htons( kPort );

		if( bind( listener, reinterpret_cast< sockaddr* >( &address ), sizeof( address ) ) != 0 )
		{
			closesocket( listener );
			running_ = false;
			return;
		}

		if( listen( listener, 8 ) != 0 )
		{
			closesocket( listener );
			running_ = false;
			return;
		}

		listenSocket_ = static_cast< unsigned long long >( listener );
		listening_    = true;

		while( running_ )
		{
			fd_set readable;
			FD_ZERO( &readable );
			FD_SET( listener, &readable );

			timeval timeout = {};
			timeout.tv_usec = 200000; // 200 ms, so Stop() never waits long

			const int ready = select( 0, &readable, nullptr, nullptr, &timeout );
			if( ready <= 0 )
				continue;

			const SOCKET client = accept( listener, nullptr, nullptr );
			if( client == INVALID_SOCKET )
				continue;

			HandleClient( static_cast< unsigned long long >( client ) );
			closesocket( client );
		}
	}

	void BridgeServer::HandleClient( unsigned long long clientHandle )
	{
		const SOCKET client = static_cast< SOCKET >( clientHandle );

		DWORD timeout = kRecvTimeoutMs;
		setsockopt( client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast< const char* >( &timeout ), sizeof( timeout ) );
		setsockopt( client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast< const char* >( &timeout ), sizeof( timeout ) );

		std::string request;
		char buffer[ 4096 ] = {};

		// Read headers.
		size_t headerEnd = std::string::npos;
		while( request.size() < 64 * 1024 )
		{
			const int received = recv( client, buffer, sizeof( buffer ), 0 );
			if( received <= 0 )
				return;

			request.append( buffer, static_cast< size_t >( received ) );
			headerEnd = request.find( "\r\n\r\n" );
			if( headerEnd != std::string::npos )
				break;
		}

		if( headerEnd == std::string::npos )
			return;

		const std::string headers = request.substr( 0, headerEnd );

		size_t contentLength = 0;
		{
			const std::string lowered = Lower( headers );
			const size_t marker       = lowered.find( "content-length:" );
			if( marker != std::string::npos )
				contentLength = static_cast< size_t >( std::strtoul( headers.c_str() + marker + 15, nullptr, 10 ) );
		}

		std::string body = request.substr( headerEnd + 4 );
		while( body.size() < contentLength )
		{
			const int received = recv( client, buffer, sizeof( buffer ), 0 );
			if( received <= 0 )
				break;

			body.append( buffer, static_cast< size_t >( received ) );
		}

		// Request line.
		const size_t lineEnd = headers.find( "\r\n" );
		const std::string requestLine = headers.substr( 0, lineEnd );

		std::istringstream lineStream( requestLine );
		std::string method;
		std::string target;
		lineStream >> method >> target;

		const std::string response = Route( method, target, body );

		size_t sent = 0;
		while( sent < response.size() )
		{
			const int written = send( client, response.data() + sent, static_cast< int >( response.size() - sent ), 0 );
			if( written <= 0 )
				break;

			sent += static_cast< size_t >( written );
		}
	}

	std::string BridgeServer::Route( const std::string& method, const std::string& target, const std::string& body )
	{
		if( method == "OPTIONS" )
			return BuildResponse( 204, "text/plain", std::string() );

		const std::string path = target.substr( 0, target.find( '?' ) );

		if( path == "/" || path == "/index.html" )
		{
			const std::string page = ReadGuiPage();
			if( page.empty() )
				return BuildResponse( 404, "text/plain", "gui/index.html not found next to the plugin." );

			return BuildResponse( 200, "text/html; charset=utf-8", page );
		}

		// Static assets for the bundled web editor.
		if( path.rfind( "/assets/", 0 ) == 0 )
		{
			if( path.find( ".." ) != std::string::npos )
				return BuildResponse( 400, "text/plain", "bad path" );

			const std::wstring directory = browser::GuiDirectory();
			if( directory.empty() )
				return BuildResponse( 404, "text/plain", "gui folder not found" );

			const std::wstring file = directory + L"/" + ToWide( path.substr( 1 ) );
			const std::string body = ReadFileBytes( file );
			if( body.empty() )
				return BuildResponse( 404, "text/plain", "asset not found" );

			return BuildResponse( 200, ContentType( path ), body );
		}

		if( path == "/status" )
		{
			std::lock_guard< std::mutex > lock( mutex_ );

			char transport[ 256 ] = {};
			std::snprintf( transport, sizeof( transport ),
				",\"phase\":%.4f,\"playing\":%s,\"bpm\":%.2f,\"step\":%d,\"bars\":%d,\"loopStart\":%d,\"loopEnd\":%d",
				phase_, playing_ ? "true" : "false", bpm_, step_, bars_, loopStart_, loopEnd_ );

			return BuildResponse( 200, "application/json",
				"{\"ok\":true,\"division\":\"" + division_ + "\",\"patternVersion\":2" + transport + "}" );
		}

		if( path == "/pattern" )
		{
			const int slot = SlotFromQuery( target );
			if( slot < 0 )
				return BuildResponse( 400, "text/plain", "slot must be a..d" );

			if( method == "POST" )
			{
				const std::string pattern = Sanitize( body );
				{
					std::lock_guard< std::mutex > lock( mutex_ );
					slots_[ slot ] = pattern;
					dirty_[ slot ] = true;
				}

				return BuildResponse( 200, "text/plain", pattern );
			}

			std::lock_guard< std::mutex > lock( mutex_ );
			return BuildResponse( 200, "text/plain", slots_[ slot ] );
		}

		return BuildResponse( 404, "text/plain", "unknown endpoint" );
	}

	void BridgeServer::Publish( int slot, const std::string& pattern )
	{
		if( slot < 0 || slot >= kSlots )
			return;

		std::lock_guard< std::mutex > lock( mutex_ );
		slots_[ slot ] = Sanitize( pattern );
		dirty_[ slot ] = false;
	}

	bool BridgeServer::Consume( int slot, std::string& pattern )
	{
		if( slot < 0 || slot >= kSlots )
			return false;

		std::lock_guard< std::mutex > lock( mutex_ );
		if( !dirty_[ slot ] )
			return false;

		dirty_[ slot ] = false;
		pattern        = slots_[ slot ];
		return true;
	}

	std::string BridgeServer::Peek( int slot ) const
	{
		if( slot < 0 || slot >= kSlots )
			return std::string( kPatternLength, '0' );

		std::lock_guard< std::mutex > lock( mutex_ );
		return slots_[ slot ];
	}

	void BridgeServer::SetDivision( const std::string& label )
	{
		std::lock_guard< std::mutex > lock( mutex_ );
		division_ = label;
	}

	void BridgeServer::SetTransport( float phase, bool playing, float bpm, int step, int bars, int loopStart, int loopEnd )
	{
		std::lock_guard< std::mutex > lock( mutex_ );
		phase_     = phase;
		playing_   = playing;
		bpm_       = bpm;
		step_      = step;
		bars_      = bars;
		loopStart_ = loopStart;
		loopEnd_   = loopEnd;
	}

	std::string BridgeServer::Sanitize( const std::string& candidate )
	{
		std::string out;
		out.reserve( kPatternLength );

		for( char c : candidate )
		{
			if( c >= '0' && c <= '3' )
				out.push_back( c );

			if( out.size() == kPatternLength )
				break;
		}

		out.resize( kPatternLength, '0' );
		return out;
	}
}
