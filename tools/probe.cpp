// Offline sanity check for a built FFGL plugin.
//
// Loads the DLL, asks the SDK for the plugin info and every registered
// parameter, and prints the result. No OpenGL context is created, so this
// validates registration and metadata only — not rendering.

#include "ffgl/FFGL.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
	typedef FFMixed( __stdcall* PlugMainFn )( FFUInt32, FFMixed, FFInstanceID );

	std::string Trim( const char* data, size_t maxLength )
	{
		std::string out( data, strnlen( data, maxLength ) );
		while( !out.empty() && ( out.back() == ' ' || out.back() == '\0' ) )
			out.pop_back();
		return out;
	}

	const char* TypeName( FFUInt32 type )
	{
		switch( type )
		{
		case FF_TYPE_BOOLEAN: return "boolean";
		case FF_TYPE_EVENT: return "event";
		case FF_TYPE_STANDARD: return "standard";
		case FF_TYPE_OPTION: return "option";
		case FF_TYPE_INTEGER: return "integer";
		case FF_TYPE_TEXT: return "text";
		case FF_TYPE_RED: return "red";
		case FF_TYPE_GREEN: return "green";
		case FF_TYPE_BLUE: return "blue";
		case FF_TYPE_HUE: return "hue";
		default: return "other";
		}
	}
}

int main( int argc, char** argv )
{
	std::setvbuf( stdout, nullptr, _IONBF, 0 );

	const char* dllPath = argc > 1 ? argv[ 1 ] : "build\\Release\\MidiRoll.dll";
	int holdSeconds     = 0;

	for( int i = 2; i < argc; ++i )
	{
		if( std::strcmp( argv[ i ], "--hold" ) == 0 && i + 1 < argc )
			holdSeconds = std::atoi( argv[ ++i ] );
	}

	HMODULE module = LoadLibraryA( dllPath );
	if( module == nullptr )
	{
		std::printf( "FAIL: could not load %s (error %lu)\n", dllPath, GetLastError() );
		return 1;
	}

	auto plugMain = reinterpret_cast< PlugMainFn >( GetProcAddress( module, "plugMain" ) );
	if( plugMain == nullptr )
	{
		std::printf( "FAIL: plugMain not exported\n" );
		FreeLibrary( module );
		return 1;
	}

	FFMixed empty = {};
	FFMixed result = plugMain( FF_INITIALISE_V2, empty, nullptr );
	if( result.UIntValue != FF_SUCCESS )
	{
		std::printf( "FAIL: initialise returned %u\n", result.UIntValue );
		FreeLibrary( module );
		return 1;
	}

	auto* info = static_cast< PluginInfoStruct* >( plugMain( FF_GET_INFO, empty, nullptr ).PointerValue );
	if( info == nullptr )
	{
		std::printf( "FAIL: no plugin info\n" );
		return 1;
	}

	std::printf( "Plugin : %s\n", Trim( info->PluginName, 16 ).c_str() );
	std::printf( "Unique : %s\n", Trim( info->PluginUniqueID, 4 ).c_str() );
	std::printf( "API    : %u.%u\n", info->APIMajorVersion, info->APIMinorVersion );
	std::printf( "Type   : %s\n", info->PluginType == FF_EFFECT ? "effect" : info->PluginType == FF_SOURCE ? "source" : "mixer" );

	auto* extended = static_cast< PluginExtendedInfoStruct* >( plugMain( FF_GET_EXTENDED_INFO, empty, nullptr ).PointerValue );
	if( extended != nullptr )
	{
		std::printf( "Version: %u.%u\n", extended->PluginMajorVersion, extended->PluginMinorVersion );
		std::printf( "About  : %s\n", extended->Description ? extended->Description : "" );
	}

	const unsigned int paramCount = plugMain( FF_GET_NUM_PARAMETERS, empty, nullptr ).UIntValue;
	std::printf( "\nParameters: %u\n", paramCount );
	std::printf( "  %-3s %-16s %-9s %-8s %s\n", "#", "name", "type", "default", "elements" );

	for( unsigned int i = 0; i < paramCount; ++i )
	{
		FFMixed index = {};
		index.UIntValue = i;

		const char* rawName = static_cast< const char* >( plugMain( FF_GET_PARAMETER_NAME, index, nullptr ).PointerValue );
		const FFUInt32 type = plugMain( FF_GET_PARAMETER_TYPE, index, nullptr ).UIntValue;
		const FFMixed defaultValue = plugMain( FF_GET_PARAMETER_DEFAULT, index, nullptr );
		const unsigned int elements = plugMain( FF_GET_NUM_PARAMETER_ELEMENTS, index, nullptr ).UIntValue;

		char defaultText[ 64 ] = {};
		if( type == FF_TYPE_TEXT )
		{
			const char* text = static_cast< const char* >( defaultValue.PointerValue );
			std::snprintf( defaultText, sizeof( defaultText ), "%s", text ? text : "" );
		}
		else
		{
			// The SDK returns float defaults as the raw bit pattern in UIntValue,
			// not as a pointer. Text/file types are the exception.
			float value = 0.0f;
			std::memcpy( &value, &defaultValue.UIntValue, sizeof( value ) );
			std::snprintf( defaultText, sizeof( defaultText ), "%.4f", value );
		}

		std::printf( "  %-3u %-16s %-9s %-8s %u\n",
			i,
			rawName ? Trim( rawName, 16 ).c_str() : "?",
			TypeName( type ),
			defaultText,
			elements );

		if( type == FF_TYPE_OPTION )
		{
			for( unsigned int element = 0; element < elements; ++element )
			{
				struct ElementQuery
				{
					FFUInt32 ParameterNumber;
					FFUInt32 ElementNumber;
				};

				ElementQuery query = { i, element };
				FFMixed elementInput = {};
				elementInput.PointerValue = &query;

				const char* elementName = static_cast< const char* >( plugMain( FF_GET_PARAMETER_ELEMENT_NAME, elementInput, nullptr ).PointerValue );
				std::printf( "        [%u] %s\n", element, elementName ? elementName : "?" );
			}
		}
	}

	if( holdSeconds > 0 )
	{
		std::printf( "\nholding for %d s — bridge listening\n", holdSeconds );
		Sleep( static_cast< DWORD >( holdSeconds ) * 1000 );
	}

	plugMain( FF_DEINITIALISE, empty, nullptr );
	FreeLibrary( module );

	std::printf( "\nOK\n" );
	return 0;
}
