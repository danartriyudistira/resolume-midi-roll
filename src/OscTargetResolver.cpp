#include "OscTargetResolver.h"

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <cstdlib>
#include <functional>

namespace midiroll
{
	namespace
	{
		constexpr wchar_t kHost[] = L"127.0.0.1";
		constexpr INTERNET_PORT kPort = 8080;

		bool HttpGet( const wchar_t* path, std::string& body )
		{
			body.clear();

			HINTERNET session = WinHttpOpen( L"MidiRoll/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
				WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
			if( session == nullptr )
				return false;

			WinHttpSetTimeouts( session, 3000, 3000, 3000, 5000 );

			HINTERNET connect = WinHttpConnect( session, kHost, kPort, 0 );
			HINTERNET request = nullptr;

			if( connect != nullptr )
			{
				request = WinHttpOpenRequest( connect, L"GET", path, nullptr,
					WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0 );
			}

			bool ok = false;

			if( request != nullptr
				&& WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
					WINHTTP_NO_REQUEST_DATA, 0, 0, 0 )
				&& WinHttpReceiveResponse( request, nullptr ) )
			{
				DWORD status = 0;
				DWORD statusSize = sizeof( status );

				if( WinHttpQueryHeaders( request,
						WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
						WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
						WINHTTP_NO_HEADER_INDEX )
					&& status == 200 )
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

						if( body.size() > 2 * 1024 * 1024 )
							break;
					}

					ok = !body.empty();
				}
			}

			if( request != nullptr )
				WinHttpCloseHandle( request );

			if( connect != nullptr )
				WinHttpCloseHandle( connect );

			WinHttpCloseHandle( session );
			return ok;
		}

		// Returns the position of the matching close character, skipping strings.
		size_t FindMatching( const std::string& text, size_t open, char openChar, char closeChar )
		{
			if( open >= text.size() || text[ open ] != openChar )
				return std::string::npos;

			int depth = 0;
			bool inString = false;
			bool escaped = false;

			for( size_t i = open; i < text.size(); ++i )
			{
				const char c = text[ i ];

				if( inString )
				{
					if( escaped )
						escaped = false;
					else if( c == '\\' )
						escaped = true;
					else if( c == '"' )
						inString = false;

					continue;
				}

				if( c == '"' )
				{
					inString = true;
					continue;
				}

				if( c == openChar )
					++depth;
				else if( c == closeChar )
				{
					--depth;
					if( depth == 0 )
						return i;
				}
			}

			return std::string::npos;
		}

		// Iterates every object at the top level of a JSON array.
		bool ForEachObject( const std::string& text, size_t arrayOpen,
			const std::function< bool( const std::string&, int ) >& callback )
		{
			const size_t arrayClose = FindMatching( text, arrayOpen, '[', ']' );
			if( arrayClose == std::string::npos )
				return false;

			size_t i = arrayOpen + 1;
			int index = 0;

			while( i < arrayClose )
			{
				while( i < arrayClose && ( std::isspace( static_cast< unsigned char >( text[ i ] ) ) || text[ i ] == ',' ) )
					++i;

				if( i >= arrayClose )
					break;

				if( text[ i ] != '{' )
					break;

				const size_t close = FindMatching( text, i, '{', '}' );
				if( close == std::string::npos || close > arrayClose )
					break;

				if( !callback( text.substr( i, close - i + 1 ), index ) )
					return true;

				++index;
				i = close + 1;
			}

			return true;
		}

		bool FindArray( const std::string& text, const char* key, size_t& arrayOpen )
		{
			const std::string needle = std::string( "\"" ) + key + "\":";
			const size_t pos = text.find( needle );
			if( pos == std::string::npos )
				return false;

			const size_t open = text.find( '[', pos + needle.size() );
			if( open == std::string::npos )
				return false;

			arrayOpen = open;
			return true;
		}

		bool ExtractFirstId( const std::string& text, long long& id )
		{
			const size_t pos = text.find( "\"id\":" );
			if( pos == std::string::npos )
				return false;

			const char* start = text.c_str() + pos + 5;
			char* end = nullptr;
			const long long value = std::strtoll( start, &end, 10 );

			if( end == start || value <= 0 )
				return false;

			id = value;
			return true;
		}

		bool ResolveClipFromComposition( const std::string& composition, long long clipId,
			int& layer, int& column )
		{
			size_t layersOpen = 0;
			if( !FindArray( composition, "layers", layersOpen ) )
				return false;

			bool found = false;
			int layerIndex = 0;

			ForEachObject( composition, layersOpen, [&]( const std::string& layerObject, int ) {
				++layerIndex;

				size_t clipsOpen = 0;
				if( !FindArray( layerObject, "clips", clipsOpen ) )
					return true;

				int columnIndex = 0;
				ForEachObject( layerObject, clipsOpen, [&]( const std::string& clipObject, int ) {
					++columnIndex;

					long long id = 0;
					if( ExtractFirstId( clipObject, id ) && id == clipId )
					{
						layer = layerIndex;
						column = columnIndex;
						found = true;
						return false;
					}

					return true;
				} );

				return !found;
			} );

			return found;
		}
	}

	OscTargetResolver::OscTargetResolver() = default;

	OscTargetResolver::~OscTargetResolver()
	{
		Stop();
	}

	void OscTargetResolver::Start()
	{
		if( running_.exchange( true ) )
			return;

		thread_ = std::thread( &OscTargetResolver::Run, this );
	}

	void OscTargetResolver::Stop()
	{
		if( !running_.exchange( false ) )
			return;

		if( thread_.joinable() )
			thread_.join();
	}

	void OscTargetResolver::RequestClip()
	{
		task_.store( 1 );
	}

	void OscTargetResolver::RequestLayer()
	{
		task_.store( 2 );
	}

	bool OscTargetResolver::Consume( Result& result )
	{
		std::lock_guard< std::mutex > lock( mutex_ );
		if( !hasResult_ )
			return false;

		result = result_;
		hasResult_ = false;
		return true;
	}

	void OscTargetResolver::Run()
	{
		while( running_.load() )
		{
			const int task = task_.exchange( 0 );

			if( task == 0 )
			{
				Sleep( 50 );
				continue;
			}

			Result result;

			if( task == 1 )
				ResolveClip( result );
			else
				ResolveLayer( result );

			if( !result.valid && result.reason.empty() )
				result.reason = "REST request failed";

			{
				std::lock_guard< std::mutex > lock( mutex_ );
				result_ = result;
				hasResult_ = true;
			}
		}
	}

	bool OscTargetResolver::ResolveClip( Result& result )
	{
		std::string clipJson;
		if( !HttpGet( L"/api/v1/composition/clips/selected", clipJson ) )
		{
			result.reason = "No selected clip or REST unavailable";
			return false;
		}

		// Check a stable parameter name instead of the effect name: the effect
		// can be renamed by the user.
		if( clipJson.find( "Env 1 OSC" ) == std::string::npos )
		{
			result.reason = "Selected clip has no Midi Roll effect";
			return false;
		}

		long long clipId = 0;
		if( !ExtractFirstId( clipJson, clipId ) )
		{
			result.reason = "Selected clip has no id";
			return false;
		}

		std::string composition;
		if( !HttpGet( L"/api/v1/composition", composition ) )
		{
			result.reason = "Cannot read composition";
			return false;
		}

		int layer = 0;
		int column = 0;
		if( !ResolveClipFromComposition( composition, clipId, layer, column ) )
		{
			result.reason = "Selected clip not found in composition";
			return false;
		}

		result.valid     = true;
		result.layerOnly = false;
		result.layer     = layer;
		result.column    = column;
		return true;
	}

	bool OscTargetResolver::ResolveLayer( Result& result )
	{
		// Use the layer of the selected clip, not the UI-selected layer: the
		// selected layer can differ from the clip that contains Midi Roll.
		std::string clipJson;
		if( !HttpGet( L"/api/v1/composition/clips/selected", clipJson ) )
		{
			result.reason = "No selected clip or REST unavailable";
			return false;
		}

		// Check a stable parameter name instead of the effect name: the effect
		// can be renamed by the user.
		if( clipJson.find( "Env 1 OSC" ) == std::string::npos )
		{
			result.reason = "Selected clip has no Midi Roll effect";
			return false;
		}

		long long clipId = 0;
		if( !ExtractFirstId( clipJson, clipId ) )
		{
			result.reason = "Selected clip has no id";
			return false;
		}

		std::string composition;
		if( !HttpGet( L"/api/v1/composition", composition ) )
		{
			result.reason = "Cannot read composition";
			return false;
		}

		int layer = 0;
		int column = 0;
		if( !ResolveClipFromComposition( composition, clipId, layer, column ) )
		{
			result.reason = "Selected clip not found in composition";
			return false;
		}

		result.valid     = true;
		result.layerOnly = true;
		result.layer     = layer;
		result.column    = 0;
		return true;
	}
}
