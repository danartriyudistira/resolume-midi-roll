#include "DashboardRest.h"

#include <windows.h>
#include <winhttp.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace midiroll
{
	namespace
	{
		constexpr wchar_t kHost[] = L"127.0.0.1";
		constexpr INTERNET_PORT kPort = 8080;
		constexpr unsigned long long kSendIntervalMs = 50; // 20 Hz per dial
	}

	DashboardRest::DashboardRest() = default;

	DashboardRest::~DashboardRest()
	{
		Stop();
	}

	void DashboardRest::Start()
	{
		if( running_.exchange( true ) )
			return;

		thread_ = std::thread( &DashboardRest::Run, this );
	}

	void DashboardRest::Stop()
	{
		if( !running_.exchange( false ) )
			return;

		if( thread_.joinable() )
			thread_.join();

		CloseConnection();
	}

	void DashboardRest::SetDial( int dial, float value )
	{
		if( dial < 0 || dial >= 8 )
			return;

		std::lock_guard< std::mutex > lock( mutex_ );
		pending_[ dial ] = value;
		dirty_[ dial ]   = true;
	}

	void DashboardRest::Run()
	{
		unsigned long long lastRefresh = 0;

		while( running_.load() )
		{
			const unsigned long long now = GetTickCount64();

			// Refresh the dial parameter IDs at startup and every 10 s. IDs
			// change when a composition is reloaded.
			if( now - lastRefresh > 10000 )
			{
				lastRefresh = now;
				RefreshIds();
			}

			for( int dial = 0; dial < 8; ++dial )
			{
				float value = 0.0f;
				bool  send  = false;

				{
					std::lock_guard< std::mutex > lock( mutex_ );
					if( dirty_[ dial ] && ids_[ dial ] >= 0 && now - lastSent_[ dial ] >= kSendIntervalMs )
					{
						value = pending_[ dial ];
						dirty_[ dial ] = false;
						send = true;
					}
				}

				if( !send )
					continue;

				if( std::fabs( value - sent_[ dial ] ) < 0.002f )
					continue;

				if( PutValue( dial, value ) )
				{
					sent_[ dial ]     = value;
					lastSent_[ dial ] = now;
				}
			}

			Sleep( 15 );
		}
	}

	bool DashboardRest::EnsureConnection()
	{
		if( session_ == nullptr )
		{
			session_ = WinHttpOpen( L"MidiRoll/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
				WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );

			if( session_ == nullptr )
				return false;
		}

		if( connect_ == nullptr )
		{
			connect_ = WinHttpConnect( static_cast< HINTERNET >( session_ ), kHost, kPort, 0 );

			if( connect_ == nullptr )
				return false;
		}

		return true;
	}

	void DashboardRest::CloseConnection()
	{
		if( connect_ != nullptr )
		{
			WinHttpCloseHandle( static_cast< HINTERNET >( connect_ ) );
			connect_ = nullptr;
		}

		if( session_ != nullptr )
		{
			WinHttpCloseHandle( static_cast< HINTERNET >( session_ ) );
			session_ = nullptr;
		}
	}

	bool DashboardRest::RefreshIds()
	{
		if( !EnsureConnection() )
			return false;

		HINTERNET request = WinHttpOpenRequest( static_cast< HINTERNET >( connect_ ), L"GET",
			L"/api/v1/composition", nullptr, WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES, 0 );

		if( request == nullptr )
		{
			CloseConnection();
			return false;
		}

		bool ok = WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
			WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) != FALSE
			&& WinHttpReceiveResponse( request, nullptr ) != FALSE;

		std::string body;

		if( ok )
		{
			DWORD available = 0;

			while( WinHttpQueryDataAvailable( request, &available ) && available > 0 )
			{
				std::string chunk;
				chunk.resize( available );

				DWORD read = 0;
				if( !WinHttpReadData( request, chunk.data(), available, &read ) || read == 0 )
					break;

				chunk.resize( read );
				body += chunk;

				if( body.size() > 1024 * 1024 )
					break;
			}
		}

		WinHttpCloseHandle( request );

		if( !ok )
			return false;

		const size_t dashboard = body.find( "\"dashboard\":{" );
		if( dashboard == std::string::npos )
			return false;

		bool foundAny = false;

		for( int dial = 0; dial < 8; ++dial )
		{
			const std::string key = "\"Link " + std::to_string( dial + 1 ) + "\"";
			const size_t keyPos = body.find( key, dashboard );

			if( keyPos == std::string::npos )
				continue;

			const size_t idPos = body.find( "\"id\":", keyPos );
			if( idPos == std::string::npos )
				continue;

			const long long id = std::strtoll( body.c_str() + idPos + 5, nullptr, 10 );
			if( id > 0 )
			{
				ids_[ dial ] = id;
				foundAny = true;
			}
		}

		return foundAny;
	}

	bool DashboardRest::PutValue( int dial, float value )
	{
		if( !EnsureConnection() || dial < 0 || dial >= 8 || ids_[ dial ] < 0 )
			return false;

		wchar_t path[ 64 ] = {};
		swprintf_s( path, L"/api/v1/parameter/by-id/%lld", ids_[ dial ] );

		HINTERNET request = WinHttpOpenRequest( static_cast< HINTERNET >( connect_ ), L"PUT",
			path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0 );

		if( request == nullptr )
		{
			CloseConnection();
			return false;
		}

		char body[ 64 ] = {};
		const int bodyLength = std::snprintf( body, sizeof( body ), "{\"value\":%.4f}", value );

		const wchar_t headers[] = L"Content-Type: application/json\r\n";
		const BOOL sent = WinHttpSendRequest( request, headers, static_cast< DWORD >( -1L ),
			body, static_cast< DWORD >( bodyLength ), static_cast< DWORD >( bodyLength ), 0 );

		const BOOL received = sent ? WinHttpReceiveResponse( request, nullptr ) : FALSE;
		WinHttpCloseHandle( request );

		if( !received )
		{
			CloseConnection();
			return false;
		}

		return true;
	}
}
