// End-to-end harness: real GL context, real plugin instance, real MIDI out.
//
// Loads MidiRoll.dll, creates an OpenGL 4.x context on a hidden window, and
// drives the plugin exactly like Resolume would: SetTime + SetBeatInfo every
// frame, ProcessOpenGL every frame.
//
// The run has three phases so transport following is actually proven:
//   1. playing   - host clock advances, notes must fire
//   2. paused    - host clock frozen, no note may fire
//   3. playing   - host clock resumes, notes must fire again
//
// Usage:
//   MidiRollHarness <path-to-dll> [--seconds 2] [--port loopMIDI]

#include <windows.h>
#include <mmsystem.h>

#include "ffgl/FFGL.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	typedef FFMixed( __stdcall* PlugMainFn )( FFUInt32, FFMixed, FFInstanceID );

	PlugMainFn g_plugMain   = nullptr;
	FFInstanceID g_instance = nullptr;

	std::mutex g_midiMutex;
	std::vector< std::string > g_midiLog;
	std::atomic< int > g_noteOn{ 0 };
	std::atomic< int > g_noteOff{ 0 };
	std::atomic< int > g_channelNotes[ 17 ];
	int g_filterChannel = 16; // harness uses the last channel so a live Resolume instance cannot pollute the count

	void CALLBACK MidiInProc( HMIDIIN, UINT message, DWORD_PTR, DWORD_PTR data, DWORD_PTR )
	{
		if( message != MIM_DATA )
			return;

		const DWORD packet = static_cast< DWORD >( data );
		const int status   = packet & 0xF0;
		const int channel  = ( packet & 0x0F ) + 1;

		// Other instances (or a DAW) may share the port. Only count the channel
		// this harness configured, otherwise the numbers are meaningless.
		if( g_filterChannel > 0 && channel != g_filterChannel )
			return;

		const int data1    = ( packet >> 8 ) & 0x7F;
		const int data2    = ( packet >> 16 ) & 0x7F;

		if( status == 0x90 && data2 > 0 )
			++g_channelNotes[ channel ];

		char line[ 96 ] = {};
		if( status == 0x90 && data2 > 0 )
		{
			++g_noteOn;
			std::snprintf( line, sizeof( line ), "note on  ch%-2d note %-3d vel %-3d", channel, data1, data2 );
		}
		else if( status == 0x80 || ( status == 0x90 && data2 == 0 ) )
		{
			++g_noteOff;
			std::snprintf( line, sizeof( line ), "note off ch%-2d note %-3d", channel, data1 );
		}
		else if( status == 0xB0 )
		{
			std::snprintf( line, sizeof( line ), "cc       ch%-2d cc %-3d val %-3d", channel, data1, data2 );
		}
		else
		{
			std::snprintf( line, sizeof( line ), "other    status 0x%02X", status );
		}

		std::lock_guard< std::mutex > lock( g_midiMutex );
		if( g_midiLog.size() < 400 )
			g_midiLog.push_back( line );
	}

	std::string Lower( std::string value )
	{
		for( char& c : value )
			c = static_cast< char >( tolower( static_cast< unsigned char >( c ) ) );
		return value;
	}

	std::string DeviceName( const MIDIOUTCAPSW& caps )
	{
		char name[ 256 ] = {};
		WideCharToMultiByte( CP_UTF8, 0, caps.szPname, -1, name, sizeof( name ), nullptr, nullptr );
		return name;
	}

	int FindOutput( const std::string& needle )
	{
		const UINT count = midiOutGetNumDevs();
		for( UINT i = 0; i < count; ++i )
		{
			MIDIOUTCAPSW caps = {};
			if( midiOutGetDevCapsW( i, &caps, sizeof( caps ) ) == MMSYSERR_NOERROR &&
			    Lower( DeviceName( caps ) ).find( needle ) != std::string::npos )
			{
				return static_cast< int >( i );
			}
		}

		return -1;
	}

	int FindInput( const std::string& needle )
	{
		const UINT count = midiInGetNumDevs();
		for( UINT i = 0; i < count; ++i )
		{
			MIDIINCAPSW caps = {};
			if( midiInGetDevCapsW( i, &caps, sizeof( caps ) ) != MMSYSERR_NOERROR )
				continue;

			char name[ 256 ] = {};
			WideCharToMultiByte( CP_UTF8, 0, caps.szPname, -1, name, sizeof( name ), nullptr, nullptr );

			if( Lower( name ).find( needle ) != std::string::npos )
				return static_cast< int >( i );
		}

		return -1;
	}

	float GetFloat( unsigned int index )
	{
		FFMixed input = {};
		input.UIntValue = index;

		const FFMixed result = g_plugMain( FF_GET_PARAMETER, input, g_instance );
		float value = 0.0f;
		std::memcpy( &value, &result.UIntValue, sizeof( value ) );
		return value;
	}

	void SetFloat( unsigned int index, float value )
	{
		SetParameterStruct parameter = {};
		parameter.ParameterNumber    = index;
		std::memcpy( &parameter.NewParameterValue.UIntValue, &value, sizeof( value ) );

		FFMixed input = {};
		input.PointerValue = &parameter;
		g_plugMain( FF_SET_PARAMETER, input, g_instance );
	}

	void SetTime( double seconds )
	{
		FFMixed input = {};
		input.PointerValue = &seconds;
		g_plugMain( FF_SET_TIME, input, g_instance );
	}

	void SetText( unsigned int index, const char* value )
	{
		SetParameterStruct parameter = {};
		parameter.ParameterNumber = index;
		parameter.NewParameterValue.PointerValue = const_cast< char* >( value );

		FFMixed input = {};
		input.PointerValue = &parameter;
		g_plugMain( FF_SET_PARAMETER, input, g_instance );
	}

	std::string GetText( unsigned int index )
	{
		FFMixed input = {};
		input.UIntValue = index;

		const FFMixed result = g_plugMain( FF_GET_PARAMETER, input, g_instance );
		const char* text = static_cast< const char* >( result.PointerValue );
		return text ? std::string( text ) : std::string();
	}

	int GetVisibility( unsigned int index )
	{
		FFMixed input = {};
		input.UIntValue = index;

		const FFMixed result = g_plugMain( FF_GET_PRAMETER_VISIBILITY, input, g_instance );
		return static_cast< int >( result.UIntValue );
	}

	void SetBeatInfo( float bpm, float barPhase )
	{
		SetBeatinfoStruct info = {};
		info.bpm      = bpm;
		info.barPhase = barPhase;

		FFMixed input = {};
		input.PointerValue = &info;
		g_plugMain( FF_SET_BEATINFO, input, g_instance );
	}

	bool CreateGL( HWND& window, HDC& dc, HGLRC& context )
	{
		WNDCLASSA windowClass = {};
		windowClass.lpfnWndProc   = DefWindowProcA;
		windowClass.hInstance     = GetModuleHandleA( nullptr );
		windowClass.lpszClassName = "MidiRollHarness";
		RegisterClassA( &windowClass );

		window = CreateWindowA( "MidiRollHarness", "harness", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, windowClass.hInstance, nullptr );
		if( window == nullptr )
			return false;

		dc = GetDC( window );

		PIXELFORMATDESCRIPTOR format = {};
		format.nSize      = sizeof( format );
		format.nVersion   = 1;
		format.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		format.iPixelType = PFD_TYPE_RGBA;
		format.cColorBits = 32;
		format.cDepthBits = 24;
		format.iLayerType = PFD_MAIN_PLANE;

		const int pixelFormat = ChoosePixelFormat( dc, &format );
		if( pixelFormat == 0 || !SetPixelFormat( dc, pixelFormat, &format ) )
			return false;

		context = wglCreateContext( dc );
		if( context == nullptr )
			return false;

		return wglMakeCurrent( dc, context ) != FALSE;
	}
}

int main( int argc, char** argv )
{
	std::setvbuf( stdout, nullptr, _IONBF, 0 );

	const char* dllPath  = argc > 1 ? argv[ 1 ] : "build\\Release\\MidiRoll.dll";
	int secondsPerPhase  = 2;
	int listenSeconds    = 0;
	float division       = 16.0f;
	int expectLanes      = 4;
	int minPerPhase      = 3;
	int templateIndex    = -1;
	int genreIndex       = 0;
	int rootIndex        = 0;
	int scaleIndex       = 1;
	int generatorBars    = 1;
	float density        = 0.5f;
	std::string progression;
	bool randomize       = false;
	bool previous        = false;
	bool testCc          = false;
	int ccChannel        = 0;
	int testLane         = 0;
	int envDials[ 4 ]    = { 0, 0, 0, 0 };
	std::string notesJson;
	std::string portHint = "loopmidi";

	for( int i = 2; i < argc; ++i )
	{
		if( std::strcmp( argv[ i ], "--seconds" ) == 0 && i + 1 < argc )
			secondsPerPhase = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--expect-lanes" ) == 0 && i + 1 < argc )
			expectLanes = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--min-notes" ) == 0 && i + 1 < argc )
			minPerPhase = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--template" ) == 0 && i + 1 < argc )
			templateIndex = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--genre" ) == 0 && i + 1 < argc )
			genreIndex = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--random" ) == 0 )
			randomize = true;
		else if( std::strcmp( argv[ i ], "--previous" ) == 0 )
			previous = true;
		else if( std::strcmp( argv[ i ], "--test-cc" ) == 0 )
			testCc = true;
		else if( std::strcmp( argv[ i ], "--cc-channel" ) == 0 && i + 1 < argc )
			ccChannel = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--test-lane" ) == 0 && i + 1 < argc )
			testLane = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--env1-dial" ) == 0 && i + 1 < argc )
			envDials[ 0 ] = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--env2-dial" ) == 0 && i + 1 < argc )
			envDials[ 1 ] = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--env3-dial" ) == 0 && i + 1 < argc )
			envDials[ 2 ] = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--env4-dial" ) == 0 && i + 1 < argc )
			envDials[ 3 ] = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--root" ) == 0 && i + 1 < argc )
			rootIndex = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--scale" ) == 0 && i + 1 < argc )
			scaleIndex = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--bars" ) == 0 && i + 1 < argc )
			generatorBars = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--density" ) == 0 && i + 1 < argc )
			density = static_cast< float >( std::atof( argv[ ++i ] ) );
		else if( std::strcmp( argv[ i ], "--progression" ) == 0 && i + 1 < argc )
			progression = argv[ ++i ];
		else if( std::strcmp( argv[ i ], "--port" ) == 0 && i + 1 < argc )
			portHint = Lower( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--listen" ) == 0 && i + 1 < argc )
			listenSeconds = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--channel" ) == 0 && i + 1 < argc )
			g_filterChannel = std::atoi( argv[ ++i ] );
		else if( std::strcmp( argv[ i ], "--division" ) == 0 && i + 1 < argc )
			division = static_cast< float >( std::atoi( argv[ ++i ] ) );
		else if( std::strcmp( argv[ i ], "--notes" ) == 0 && i + 1 < argc )
			notesJson = argv[ ++i ];
		else if( std::strcmp( argv[ i ], "--notes-file" ) == 0 && i + 1 < argc )
		{
			std::ifstream file( argv[ ++i ], std::ios::binary );
			std::ostringstream buffer;
			buffer << file.rdbuf();
			notesJson = buffer.str();
		}
	}

	// Listen-only mode: measure what another process (Resolume) is sending,
	// without instantiating anything of our own.
	if( listenSeconds > 0 )
	{
		const int index = FindInput( portHint );
		if( index < 0 )
		{
			std::printf( "FAIL: port '%s' not found\n", portHint.c_str() );
			return 1;
		}

		g_filterChannel = 0; // count every channel

		HMIDIIN listener = nullptr;
		if( midiInOpen( &listener, static_cast< UINT >( index ), reinterpret_cast< DWORD_PTR >( &MidiInProc ), 0, CALLBACK_FUNCTION ) != MMSYSERR_NOERROR )
		{
			std::printf( "FAIL: cannot open port (already in use?)\n" );
			return 1;
		}

		midiInStart( listener );
		Sleep( static_cast< DWORD >( listenSeconds ) * 1000 );
		midiInStop( listener );
		midiInClose( listener );

		std::printf( "Listening %d s on '%s'\n", listenSeconds, portHint.c_str() );
		for( int channel = 1; channel <= 16; ++channel )
		{
			const int count = g_channelNotes[ channel ].load();
			if( count > 0 )
				std::printf( "  channel %-2d : %d note on  (%.2f /s)\n", channel, count, count / static_cast< double >( listenSeconds ) );
		}

		return 0;
	}

	// --- OpenGL -----------------------------------------------------------
	HWND window = nullptr;
	HDC dc      = nullptr;
	HGLRC gl    = nullptr;

	if( !CreateGL( window, dc, gl ) )
	{
		std::printf( "FAIL: no OpenGL context\n" );
		return 1;
	}

	if( glewInit() != GLEW_OK )
	{
		std::printf( "FAIL: glewInit\n" );
		return 1;
	}

	std::printf( "GL     : %s\n", reinterpret_cast< const char* >( glGetString( GL_VERSION ) ) );

	// --- Plugin -----------------------------------------------------------
	HMODULE module = LoadLibraryA( dllPath );
	if( module == nullptr )
	{
		std::printf( "FAIL: load %s (error %lu)\n", dllPath, GetLastError() );
		return 1;
	}

	g_plugMain = reinterpret_cast< PlugMainFn >( GetProcAddress( module, "plugMain" ) );
	if( g_plugMain == nullptr )
	{
		std::printf( "FAIL: plugMain missing\n" );
		return 1;
	}

	FFMixed empty = {};
	if( g_plugMain( FF_INITIALISE_V2, empty, nullptr ).UIntValue != FF_SUCCESS )
	{
		std::printf( "FAIL: initialise\n" );
		return 1;
	}

	FFGLViewportStruct viewport = { 0, 0, 64, 64 };
	FFMixed viewportInput = {};
	viewportInput.PointerValue = &viewport;

	g_instance = g_plugMain( FF_INSTANTIATE_GL, viewportInput, nullptr ).PointerValue;
	if( g_instance == nullptr )
	{
		std::printf( "FAIL: instantiate (InitGL)\n" );
		return 1;
	}

	std::printf( "Plugin : instantiated\n" );

	// --- Input texture ----------------------------------------------------
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );

	std::vector< unsigned char > pixels( 64 * 64 * 4, 96 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	FFGLTextureStruct textureStruct = { 64, 64, 64, 64, texture };
	FFGLTextureStruct* texturePtr   = &textureStruct;

	ProcessOpenGLStruct process = {};
	process.numInputTextures = 1;
	process.inputTextures    = &texturePtr;
	process.HostFBO          = 0;

	// --- MIDI capture -----------------------------------------------------
	const int outputIndex = FindOutput( portHint );
	const int inputIndex  = FindInput( portHint );

	HMIDIIN midiIn = nullptr;
	if( inputIndex >= 0 )
	{
		if( midiInOpen( &midiIn, static_cast< UINT >( inputIndex ), reinterpret_cast< DWORD_PTR >( &MidiInProc ), 0, CALLBACK_FUNCTION ) == MMSYSERR_NOERROR )
			midiInStart( midiIn );
		else
			midiIn = nullptr;
	}

	std::printf( "MIDI   : output index %d, input capture %s\n",
		outputIndex,
		midiIn ? "aktif" : "tidak tersedia" );

	// --- Configure --------------------------------------------------------
	g_plugMain( FF_CONNECT, empty, g_instance );

	if( outputIndex >= 0 )
		SetFloat( 0, static_cast< float >( outputIndex + 1 ) ); // Midi Port
	SetFloat( 1, 16.0f );   // Midi Channel = 16
	SetFloat( 2, division ); // Length: 4, 8, 16 or 32
	SetFloat( 3, 0.30f );   // Decay ~0.93 s
	SetFloat( 24, static_cast< float >( ccChannel ) ); // CC Channel: 0 = same as Midi
	SetFloat( 30, static_cast< float >( testLane ) );  // Test Lane: 0..3

	for( int lane = 0; lane < 4; ++lane )
		SetFloat( 25 + lane, static_cast< float >( envDials[ lane ] ) ); // Env N Dial

	if( testCc )
		SetFloat( 29, 1.0f ); // Test CC sweep

	if( !notesJson.empty() )
		SetText( 10, notesJson.c_str() ); // Notes (free note list)

	if( templateIndex >= 0 )
	{
		SetFloat( 12, static_cast< float >( rootIndex ) );    // Root
		SetFloat( 13, static_cast< float >( scaleIndex ) );   // Scale
		SetFloat( 14, static_cast< float >( genreIndex ) );   // Genre
		SetFloat( 15, static_cast< float >( templateIndex ) ); // Template
		SetFloat( 18, static_cast< float >( generatorBars ) ); // Bars
		SetText( 19, progression.c_str() );                   // Progression
		SetFloat( 20, density );                              // Density
		SetFloat( randomize ? 22 : 21, 1.0f );                 // Random / Load
		if( previous )
			SetFloat( 23, 1.0f );                              // Previous
	}

	// --- Run --------------------------------------------------------------
	const double bpm      = 120.0;
	const double barSeconds = ( 60.0 / bpm ) * 4.0; // one bar at 120 BPM = 2 s

	std::printf( "\n  phase    t(s)   Env1   Env2   Env3   Env4   note-on\n" );

	float envMax[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };

	auto runPhase = [ & ]( const char* label, bool playing ) -> int {
		const int startNotes = g_noteOn.load();
		const int frames     = secondsPerPhase * 60;

		double hostTime = 0.0;
		(void)hostTime;
		auto lastTick   = std::chrono::steady_clock::now();

		// The host clock only advances while "playing", exactly like a paused
		// composition: phase freezes, so the sequencer must freeze too.
		static double transportTime = 0.0;

		for( int frame = 0; frame < frames; ++frame )
		{
			const auto now = std::chrono::steady_clock::now();
			const double elapsed = std::chrono::duration< double >( now - lastTick ).count();
			lastTick = now;

			// Deterministic host clock: exactly 60 frames per second, so the test
			// does not drift with real sleep jitter.
			if( playing )
				transportTime += 1.0 / 60.0;

			const float barPhase = static_cast< float >( std::fmod( transportTime / barSeconds, 1.0 ) );

			SetTime( transportTime );
			SetBeatInfo( static_cast< float >( bpm ), barPhase );

			FFMixed processInput = {};
			processInput.PointerValue = &process;

			const FFResult result = g_plugMain( FF_PROCESS_OPENGL, processInput, g_instance ).UIntValue;
			if( result != FF_SUCCESS )
			{
				std::printf( "FAIL: ProcessOpenGL returned %u\n", result );
				return -1;
			}

			for( int lane = 0; lane < 4; ++lane )
				envMax[ lane ] = std::max( envMax[ lane ], GetFloat( 5 + lane ) );

			if( frame % 10 == 0 )
			{
				std::printf( "  %-7s %5.2f  %5.2f  %5.2f  %5.2f  %5.2f   %d\n",
					label,
					frame / 60.0f,
					GetFloat( 5 ), GetFloat( 6 ), GetFloat( 7 ), GetFloat( 8 ),
					g_noteOn.load() - startNotes );
			}

			Sleep( 16 );
		}

		return g_noteOn.load() - startNotes;
	};

	const int playOne  = runPhase( "play", true );
	const int paused   = runPhase( "pause", false );
	const int playTwo  = runPhase( "play", true );

	// Read the mirrored parameters before tearing the instance down.
	const bool captureParams = !notesJson.empty() || templateIndex >= 0 || randomize;
	const std::string finalNotes = captureParams ? GetText( 10 ) : std::string();
	const float finalRoot = captureParams ? GetFloat( 12 ) : 0.0f;
	const float finalScale = captureParams ? GetFloat( 13 ) : 0.0f;
	const float finalBars = captureParams ? GetFloat( 18 ) : 0.0f;
	const float finalDensity = captureParams ? GetFloat( 20 ) : 0.0f;
	const std::string finalProgression = captureParams ? GetText( 19 ) : std::string();

	int envVisible[ 4 ] = { -1, -1, -1, -1 };
	for( int lane = 0; lane < 4; ++lane )
		envVisible[ lane ] = GetVisibility( 5 + lane );

	// --- Teardown ---------------------------------------------------------
	g_plugMain( FF_DISCONNECT, empty, g_instance );
	g_plugMain( FF_DEINSTANTIATE_GL, empty, g_instance );
	g_plugMain( FF_DEINITIALISE, empty, nullptr );

	if( midiIn )
	{
		midiInStop( midiIn );
		midiInClose( midiIn );
	}

	glDeleteTextures( 1, &texture );
	wglMakeCurrent( nullptr, nullptr );
	wglDeleteContext( gl );
	ReleaseDC( window, dc );
	DestroyWindow( window );
	FreeLibrary( module );

	// --- Report -----------------------------------------------------------
	std::printf( "\nNote on per fase: play %d, pause %d, play %d\n", playOne, paused, playTwo );
	std::printf( "MIDI diterima  : %d note on, %d note off\n", g_noteOn.load(), g_noteOff.load() );

	{
		std::lock_guard< std::mutex > lock( g_midiMutex );
		int printed = 0;
		for( size_t i = 0; i < g_midiLog.size() && printed < 10; ++i )
		{
			const std::string& line = g_midiLog[ i ];

			// Other apps may share the port and flood it with unrelated CCs.
			// Keep the envelope CCs (21..24) and all note messages.
			if( line.rfind( "cc ", 0 ) == 0 )
			{
				int channel = 0;
				int cc      = -1;
				if( std::sscanf( line.c_str(), "cc ch%d cc %d", &channel, &cc ) == 2 && cc < 21 )
					continue;
			}

			std::printf( "  %s\n", line.c_str() );
			++printed;
		}
	}

	std::printf( "Env puncak     : %.2f %.2f %.2f %.2f\n", envMax[ 0 ], envMax[ 1 ], envMax[ 2 ], envMax[ 3 ] );

	if( captureParams )
	{
		std::printf( "Root/Scale     : %.0f / %.0f\n", finalRoot, finalScale );
		std::printf( "Bars/Density   : %.0f / %.2f\n", finalBars, finalDensity );
		std::printf( "Progression    : %s\n", finalProgression.c_str() );
		std::printf( "Notes keluar   : %s\n", finalNotes.c_str() );
	}

	std::printf( "Env visible    : %d %d %d %d\n",
		envVisible[ 0 ], envVisible[ 1 ], envVisible[ 2 ], envVisible[ 3 ] );

	bool ok = true;

	// A loop can deliberately exclude whole envelope lanes, so the caller can
	// lower the expectations when testing loop points.
	int activeLanes = 0;
	for( float peak : envMax )
	{
		if( peak > 0.3f )
			++activeLanes;
	}

	ok = ok && activeLanes >= expectLanes;
	ok = ok && playOne >= minPerPhase && playTwo >= minPerPhase;
	ok = ok && paused == 0;

	if( midiIn && ( playOne > 0 || playTwo > 0 ) )
		ok = ok && g_noteOff.load() >= 1;

	std::printf( "\n%s\n", ok ? "HASIL: LULUS" : "HASIL: GAGAL" );
	return ok ? 0 : 1;
}
