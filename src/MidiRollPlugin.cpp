#include "MidiRollPlugin.h"

#include "BridgeServer.h"
#include "BrowserLauncher.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

using namespace ffglex;

namespace
{
	const char* kVertexShader = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
)";

	const char* kFragmentShader = R"(#version 410 core
uniform sampler2D InputTexture;
uniform vec2 MaxUV;

in vec2 uv;
out vec4 fragColor;

void main()
{
	fragColor = texture( InputTexture, uv * MaxUV );
}
)";

	float Clamp01( float value )
	{
		return std::clamp( value, 0.0f, 1.0f );
	}

	std::wstring ToWide( const std::string& text )
	{
		return std::wstring( text.begin(), text.end() );
	}
}

MidiRollPlugin::MidiRollPlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	midi_.RefreshDevices();

	// --- Midi port ---------------------------------------------------------
	// Element 0 is Off so the plugin stays silent until the user picks a port.
	const unsigned int portElements = 1 + static_cast< unsigned int >( midi_.DeviceCount() );
	SetOptionParamInfo( PT_PORT, "Midi Port", portElements, 0.0f );
	SetParamElementInfo( PT_PORT, 0, "Off", 0.0f );

	// A separator makes Resolume render the choice as a dropdown with categories
	// instead of a row of segmented buttons.
	if( midi_.DeviceCount() > 0 )
		AddElementSeparator( PT_PORT, 1 );

	for( int i = 0; i < midi_.DeviceCount(); ++i )
	{
		SetParamElementInfo( PT_PORT, static_cast< unsigned int >( i + 1 ), midi_.DeviceName( i ).c_str(), static_cast< float >( i + 1 ) );
	}

	// --- Midi channel ------------------------------------------------------
	SetOptionParamInfo( PT_CHANNEL, "Midi Channel", 16, 1.0f );
	for( int i = 0; i < 16; ++i )
	{
		const std::string label = "Channel " + std::to_string( i + 1 );
		SetParamElementInfo( PT_CHANNEL, static_cast< unsigned int >( i ), label.c_str(), static_cast< float >( i + 1 ) );
	}

	// --- Grid --------------------------------------------------------------
	// Editor grid / legacy step resolution. The piano roll itself always stores
	// notes at 1/32, so this only changes the grid labels and legacy patterns.
	SetOptionParamInfo( PT_LENGTH, "Grid", 4, 16.0f );
	SetParamElementInfo( PT_LENGTH, 0, "1/4", 4.0f );
	SetParamElementInfo( PT_LENGTH, 1, "1/8", 8.0f );
	SetParamElementInfo( PT_LENGTH, 2, "1/16", 16.0f );
	SetParamElementInfo( PT_LENGTH, 3, "1/32", 32.0f );

	// The grid belongs to the external piano roll now; keep the parameter for
	// preset compatibility but hide it from the effect panel.
	SetParamVisibility( PT_LENGTH, false, false );

	// --- Tone --------------------------------------------------------------
	SetParamInfo( PT_DECAY, "Decay", FF_TYPE_STANDARD, decayNormalized_ );
	SetParamInfo( PT_EDIT_MODE, "Edit Mode", FF_TYPE_EVENT, false );

	// --- Envelope outputs --------------------------------------------------
	SetParamInfo( PT_ENV1, "Env 1", FF_TYPE_STANDARD, 0.0f );
	SetParamInfo( PT_ENV2, "Env 2", FF_TYPE_STANDARD, 0.0f );
	SetParamInfo( PT_ENV3, "Env 3", FF_TYPE_STANDARD, 0.0f );
	SetParamInfo( PT_ENV4, "Env 4", FF_TYPE_STANDARD, 0.0f );

	// No default groove: a freshly added Midi Roll is silent. Users pick a
	// template from the effect panel or draw notes in the piano roll.

	decaySeconds_ = 0.05f + decayNormalized_ * 2.95f;

	// --- Bridge ------------------------------------------------------------
	// The legacy 4 lane pattern lives in Pattern; the piano roll writes the free
	// note list into Notes instead.
	patternText_ = EncodePattern();
	SetParamInfo( PT_PATTERN, "Pattern", FF_TYPE_TEXT, patternText_.c_str() );

	// Free note list from the piano roll: [[pitch, step, length, velocity], ...]
	// Empty means "fall back to the legacy 4 lane pattern".
	SetParamInfo( PT_NOTES, "Notes", FF_TYPE_TEXT, "" );

	// Shift note timing to compensate for latency further down the chain
	// (loopMIDI, DAW buffer, synth). 0.5 is neutral.
	SetParamInfo( PT_OFFSET, "Midi Offset", FF_TYPE_STANDARD, offsetNormalized_ );

	// --- Musical key -------------------------------------------------------
	// Root and scale drive bassline and melody generation; drums stay fixed.
	SetOptionParamInfo( PT_ROOT, "Root", 12, 0.0f );
	for( int i = 0; i < 12; ++i )
	{
		static const char* kNoteNames[ 12 ] = {
			"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
		};
		SetParamElementInfo( PT_ROOT, static_cast< unsigned int >( i ), kNoteNames[ i ], static_cast< float >( i ) );
	}

	SetOptionParamInfo( PT_SCALE, "Scale", 6, 1.0f );
	SetParamElementInfo( PT_SCALE, 0, "Major", 0.0f );
	SetParamElementInfo( PT_SCALE, 1, "Natural Minor", 1.0f );
	SetParamElementInfo( PT_SCALE, 2, "Dorian", 2.0f );
	SetParamElementInfo( PT_SCALE, 3, "Mixolydian", 3.0f );
	SetParamElementInfo( PT_SCALE, 4, "Pentatonic Minor", 4.0f );
	SetParamElementInfo( PT_SCALE, 5, "Pentatonic Major", 5.0f );

	// --- Built-in templates ------------------------------------------------
	// Pick a genre and a part, then press Load Template. Applying only on the
	// event keeps preset loads from overwriting saved notes with the template.
	SetOptionParamInfo( PT_GENRE, "Genre", 5, 0.0f );
	SetParamElementInfo( PT_GENRE, 0, "Techno", 0.0f );
	SetParamElementInfo( PT_GENRE, 1, "House", 1.0f );
	SetParamElementInfo( PT_GENRE, 2, "Trance", 2.0f );
	SetParamElementInfo( PT_GENRE, 3, "Drum & Bass", 3.0f );
	SetParamElementInfo( PT_GENRE, 4, "Hip-Hop", 4.0f );

	SetOptionParamInfo( PT_TEMPLATE, "Template", 5, 0.0f );
	SetParamElementInfo( PT_TEMPLATE, 0, "Empty", 0.0f );
	SetParamElementInfo( PT_TEMPLATE, 1, "Drums", 1.0f );
	SetParamElementInfo( PT_TEMPLATE, 2, "Bassline", 2.0f );
	SetParamElementInfo( PT_TEMPLATE, 3, "Melody", 3.0f );
	SetParamElementInfo( PT_TEMPLATE, 4, "Full Groove", 4.0f );

	SetParamInfo( PT_LOAD_TEMPLATE, "Load Template", FF_TYPE_EVENT, false );
	SetParamInfo( PT_RANDOM_TEMPLATE, "Random", FF_TYPE_EVENT, false );

	// The old events stay for preset compatibility but are hidden; the visible
	// Load/Random/Previous live after Bars/Progression/Density.
	SetParamVisibility( PT_LOAD_TEMPLATE, false, false );
	SetParamVisibility( PT_RANDOM_TEMPLATE, false, false );

	// --- Generator settings -------------------------------------------------
	// Bars is the generated pattern length (1..8). Progression holds one root
	// per bar, e.g. "A D A G". Density only affects Random.
	SetOptionParamInfo( PT_BARS_GEN, "Bars", 8, 1.0f );
	for( int i = 0; i < 8; ++i )
	{
		const std::string label = std::to_string( i + 1 );
		SetParamElementInfo( PT_BARS_GEN, static_cast< unsigned int >( i ), label.c_str(), static_cast< float >( i + 1 ) );
	}

	SetParamInfo( PT_PROGRESSION, "Progression", FF_TYPE_TEXT, "" );
	SetParamInfo( PT_DENSITY, "Density", FF_TYPE_STANDARD, density_ );

	SetParamInfo( PT_LOAD_V2, "Load", FF_TYPE_EVENT, false );
	SetParamInfo( PT_RANDOM_V2, "Random", FF_TYPE_EVENT, false );
	SetParamInfo( PT_PREVIOUS, "Previous", FF_TYPE_EVENT, false );

	// --- Envelope CC routing ----------------------------------------------
	// Notes and CC can use different channels: keep the synth on the note
	// channel and send the envelope CCs to Resolume on another channel.
	// Element 0 preserves the old behaviour (CC follows Midi Channel).
	SetOptionParamInfo( PT_CC_CHANNEL, "CC Channel", 17, 0.0f );
	SetParamElementInfo( PT_CC_CHANNEL, 0, "Same as Midi", 0.0f );
	for( int i = 0; i < 16; ++i )
	{
		const std::string label = "Channel " + std::to_string( i + 1 );
		SetParamElementInfo( PT_CC_CHANNEL, static_cast< unsigned int >( i + 1 ), label.c_str(), static_cast< float >( i + 1 ) );
	}

	// Toggles a continuous 0..127..0 sweep on CC 21..24 for MIDI learn.
	SetParamInfo( PT_TEST_CC, "Test CC", FF_TYPE_EVENT, false );

	// Seed the constrained randomizer once per instance.
	rngState_ = static_cast< unsigned int >(
		std::chrono::steady_clock::now().time_since_epoch().count() ) | 1u;

	midiroll::BridgeServer::Instance().Acquire();
	PublishCurrentPattern();

	FFGLLog::LogToHost( "Midi Roll created" );
}

MidiRollPlugin::~MidiRollPlugin()
{
	StopAll();
	midi_.Close();
	midiroll::BridgeServer::Instance().Release();
}

// ---------------------------------------------------------------------------
// Host transport
// ---------------------------------------------------------------------------

FFResult MidiRollPlugin::SetTime( double time )
{
	if( hostTimeSeen_ && time > lastHostTime_ + 1e-6 )
		hostTimeAdvancing_ = true;

	hostTime_     = time;
	lastHostTime_ = time;
	hostTimeSeen_ = true;

	return CFFGLPlugin::SetTime( time );
}

void MidiRollPlugin::SetBeatInfo( float bpm, float barPhase )
{
	CFFGLPlugin::SetBeatInfo( bpm, barPhase );

	if( bpm > 1.0f )
	{
		const float clamped = Clamp01( barPhase );
		const double now = std::chrono::duration< double >( std::chrono::steady_clock::now().time_since_epoch() ).count();

		if( std::fabs( clamped - hostBarPhase_ ) > 0.0005f )
			lastHostPhaseChange_ = now;

		hostBarPhase_ = clamped;
		hostBpm_      = bpm;
		phaseAnchor_     = clamped;
		phaseAnchorTime_ = now;

		if( !beatInfoSeen_ )
		{
			beatInfoSeen_  = true;
			firstBeatWall_ = now;
		}

		if( !beatInfoLogged_ )
		{
			beatInfoLogged_ = true;

			char message[ 128 ] = {};
			std::snprintf( message, sizeof( message ), "Host beat info: bpm %.2f, bar phase %.3f", bpm, barPhase );
			FFGLLog::LogToHost( message );
		}
	}
}

// The host hands us the phase once per rendered frame. Between those updates we
// advance it with our own clock so notes and the playhead are not stuck at the
// frame rate.
float MidiRollPlugin::CurrentPhase() const
{
	const double now = std::chrono::duration< double >( std::chrono::steady_clock::now().time_since_epoch() ).count();

	if( !beatInfoSeen_ || hostBpm_ < 1.0f )
		return currentPhase_;

	// No phase movement for a while means the host is paused: hold position.
	if( ( now - lastHostPhaseChange_ ) > 0.15 )
		return hostBarPhase_;

	const double elapsed = now - phaseAnchorTime_;
	const float barsPerSecond = hostBpm_ / 60.0f / kBeatsPerBar;
	float phase = phaseAnchor_ + static_cast< float >( elapsed ) * barsPerSecond;

	return phase - std::floor( phase );
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

unsigned int MidiRollPlugin::Connect()
{
	OpenConfiguredPort();

	// Adopt an edit made while the plugin wasn't rendering; otherwise publish
	// our pattern so a freshly opened page hydrates from the source of truth.
	std::string pending;
	if( midiroll::BridgeServer::Instance().Consume( slot_, pending ) )
		ApplyEncodedPattern( pending );
	else
		PublishCurrentPattern();

	hasLastFrame_ = false;
	lastStep_     = -1;
	barCount_     = 0;
	lastBarPhase_ = 0.0f;
	fallbackBeats_ = 0.0;

	PublishDivision();
	UpdateEnvelopeLanes( false );
	lanesDirty_ = true;

	return CFFGLPlugin::Connect();
}

unsigned int MidiRollPlugin::Disconnect()
{
	StopAll();
	midi_.Close();
	return CFFGLPlugin::Disconnect();
}

FFResult MidiRollPlugin::InitGL( const FFGLViewportStruct* vp )
{
	if( !shader_.Compile( kVertexShader, kFragmentShader ) )
	{
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad_.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}

	return CFFGLPlugin::InitGL( vp );
}

FFResult MidiRollPlugin::DeInitGL()
{
	shader_.FreeGLResources();
	quad_.Release();
	return FF_SUCCESS;
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

FFResult MidiRollPlugin::SetFloatParameter( unsigned int index, float value )
{
	switch( index )
	{
	case PT_PORT:
	{
		// Option params arrive as the element's value, not a normalized float.
		const int element = static_cast< int >( std::lround( value ) );
		portElement_      = std::clamp( element, 0, midi_.DeviceCount() );
		OpenConfiguredPort();
		return FF_SUCCESS;
	}

	case PT_CHANNEL:
	{
		const int element = static_cast< int >( std::lround( value ) );
		midiChannel_      = std::clamp( element - 1, 0, 15 );
		return FF_SUCCESS;
	}

	case PT_LENGTH:
	{
		// Option values are the note denominators themselves: 4, 8, 16, 32.
		const int requested = static_cast< int >( std::lround( value ) );
		const int next      = ( requested == 4 || requested == 8 || requested == 16 || requested == 32 ) ? requested : 16;

		if( next != division_ )
		{
			division_ = next;
			lastStep_ = -1;
			PublishDivision();
		}

		return FF_SUCCESS;
	}

	case PT_DECAY:
		decayNormalized_ = Clamp01( value );
		decaySeconds_    = 0.05f + decayNormalized_ * 2.95f;
		return FF_SUCCESS;

	case PT_OFFSET:
		offsetNormalized_ = Clamp01( value );
		offsetMs_         = ( offsetNormalized_ - 0.5f ) * 160.0f; // -80 .. +80 ms
		return FF_SUCCESS;

	case PT_EDIT_MODE:
		if( value != 0.0f )
		{
			std::string error;

			// Prefer the plugin's own origin: same-origin page, secure context
			// for Web MIDI, and no CORS to negotiate.
			if( midiroll::BridgeServer::Instance().IsListening() )
			{
				const std::string origin = midiroll::BridgeServer::Instance().Origin();
				if( !midiroll::browser::OpenUrl( ToWide( origin ), &error ) )
					FFGLLog::LogToHost( error.c_str() );
			}
			else if( !midiroll::browser::OpenGui( &error ) )
			{
				FFGLLog::LogToHost( error.c_str() );
			}
		}
		return FF_SUCCESS;

	case PT_ENV1:
	case PT_ENV2:
	case PT_ENV3:
	case PT_ENV4:
		// Envelope parameters are outputs. The host echoes values back when it
		// applies our value change events; ignore them.
		return FF_SUCCESS;

	case PT_ROOT:
		rootIndex_ = std::clamp( static_cast< int >( std::lround( value ) ), 0, 11 );
		return FF_SUCCESS;

	case PT_SCALE:
		scaleIndex_ = std::clamp( static_cast< int >( std::lround( value ) ), 0, 5 );
		return FF_SUCCESS;

	case PT_GENRE:
		genreIndex_ = std::clamp( static_cast< int >( std::lround( value ) ), 0, 4 );
		return FF_SUCCESS;

	case PT_TEMPLATE:
		templateIndex_ = std::clamp( static_cast< int >( std::lround( value ) ), 0, 4 );
		return FF_SUCCESS;

	case PT_LOAD_TEMPLATE:
	case PT_LOAD_V2:
		if( value >= 0.5f )
		{
			PushHistory();
			ApplyTemplate( genreIndex_, templateIndex_ );
		}
		return FF_SUCCESS;

	case PT_RANDOM_TEMPLATE:
	case PT_RANDOM_V2:
		if( value >= 0.5f )
		{
			PushHistory();
			ApplyRandomTemplate( genreIndex_, templateIndex_ );
		}
		return FF_SUCCESS;

	case PT_PREVIOUS:
		if( value >= 0.5f )
			RestorePrevious();
		return FF_SUCCESS;

	case PT_CC_CHANNEL:
	{
		const int element = static_cast< int >( std::lround( value ) );
		ccChannelElement_ = std::clamp( element, 0, 16 );
		// Force the next frame to resend on the new channel.
		envCcSent_[ 0 ] = envCcSent_[ 1 ] = envCcSent_[ 2 ] = envCcSent_[ 3 ] = -1;
		return FF_SUCCESS;
	}

	case PT_TEST_CC:
		if( value >= 0.5f )
		{
			ccTestActive_  = !ccTestActive_;
			ccTestElapsed_ = 0.0f;

			if( !ccTestActive_ )
				envCcSent_[ 0 ] = envCcSent_[ 1 ] = envCcSent_[ 2 ] = envCcSent_[ 3 ] = -1;
		}
		return FF_SUCCESS;

	case PT_BARS_GEN:
		generatorBars_ = std::clamp( static_cast< int >( std::lround( value ) ), 1, 8 );
		return FF_SUCCESS;

	case PT_DENSITY:
		density_ = Clamp01( value );
		return FF_SUCCESS;

	default:
		return FF_FAIL;
	}
}

float MidiRollPlugin::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_PORT:
		return static_cast< float >( portElement_ );
	case PT_CHANNEL:
		return static_cast< float >( midiChannel_ + 1 );
	case PT_LENGTH:
		return static_cast< float >( division_ );
	case PT_DECAY:
		return decayNormalized_;
	case PT_OFFSET:
		return offsetNormalized_;
	case PT_EDIT_MODE:
		return 0.0f;
	case PT_ENV1:
		return envelopes_.Value( 0 );
	case PT_ENV2:
		return envelopes_.Value( 1 );
	case PT_ENV3:
		return envelopes_.Value( 2 );
	case PT_ENV4:
		return envelopes_.Value( 3 );
	case PT_GENRE:
		return static_cast< float >( genreIndex_ );
	case PT_ROOT:
		return static_cast< float >( rootIndex_ );
	case PT_SCALE:
		return static_cast< float >( scaleIndex_ );
	case PT_TEMPLATE:
		return static_cast< float >( templateIndex_ );
	case PT_LOAD_TEMPLATE:
		return 0.0f;
	case PT_RANDOM_TEMPLATE:
		return 0.0f;
	case PT_LOAD_V2:
		return 0.0f;
	case PT_RANDOM_V2:
		return 0.0f;
	case PT_PREVIOUS:
		return 0.0f;
	case PT_CC_CHANNEL:
		return static_cast< float >( ccChannelElement_ );
	case PT_TEST_CC:
		return 0.0f;
	case PT_BARS_GEN:
		return static_cast< float >( generatorBars_ );
	case PT_DENSITY:
		return density_;
	default:
		return 0.0f;
	}
}

char* MidiRollPlugin::GetParameterDisplay( unsigned int index )
{
	if( index == PT_DENSITY )
	{
		std::snprintf( textBuffer_, sizeof( textBuffer_ ), "%d %%", static_cast< int >( std::lround( density_ * 100.0f ) ) );
		return textBuffer_;
	}

	return CFFGLPlugin::GetParameterDisplay( index );
}

FFResult MidiRollPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( value == nullptr )
		return FF_FAIL;

	if( index == PT_PATTERN )
	{
		ApplyEncodedPattern( value );
		return FF_SUCCESS;
	}

	if( index == PT_NOTES )
	{
		ParseNotes( value );
		return FF_SUCCESS;
	}

	if( index == PT_PROGRESSION )
	{
		ParseProgression( value );
		return FF_SUCCESS;
	}

	return FF_FAIL;
}

char* MidiRollPlugin::GetTextParameter( unsigned int index )
{
	const std::string* source = nullptr;

	if( index == PT_PATTERN )
		source = &patternText_;
	else if( index == PT_NOTES )
		source = &notesText_;
	else if( index == PT_PROGRESSION )
		source = &progressionText_;
	else
		return nullptr;

	const size_t length = std::min( source->size(), sizeof( textBuffer_ ) - 1 );
	std::memcpy( textBuffer_, source->data(), length );
	textBuffer_[ length ] = '\0';
	return textBuffer_;
}

// ---------------------------------------------------------------------------
// Render + clock
// ---------------------------------------------------------------------------

FFResult MidiRollPlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL == nullptr )
		return FF_FAIL;

	const auto now = std::chrono::steady_clock::now();

	float deltaSeconds = 0.0f;
	if( hasLastFrame_ )
		deltaSeconds = std::chrono::duration< float >( now - lastFrame_ ).count();

	lastFrame_    = now;
	hasLastFrame_ = true;

	// A stalled frame (project load, resize) must not fire a burst of notes.
	deltaSeconds = std::clamp( deltaSeconds, 0.0f, 0.1f );

	renderTime_ += deltaSeconds;

	// Visibility changes are raised from the render thread so a stateful host
	// (Resolume) applies them after its UI is fully initialized.
	if( lanesDirty_ )
	{
		lanesDirty_ = false;
		UpdateEnvelopeLanes( true );
	}

	DriveSequencer( deltaSeconds );
	FlushNoteOffs( deltaSeconds );
	envelopes_.Advance( deltaSeconds );
	UpdateCcTest( deltaSeconds );
	PushEnvelopeParams();
	PollBridge( deltaSeconds );

	// Publish an extrapolated phase so the editor's playhead is not stuck at the
	// host's render rate.
	const float phase = CurrentPhase();

	if( std::fabs( phase - lastPublishedPhase_ ) > 0.0005f )
	{
		lastPublishedPhase_ = phase;
		lastPhaseChange_    = renderTime_;
	}

	const float transportBpm = beatInfoSeen_ ? hostBpm_ : fallbackBpm_;

	const int transportBars       = std::max( 1, bars_ );
	const int transportTotal      = transportBars * kMaxSteps;
	const int transportLoopStart  = std::clamp( loopStart_, 0, transportTotal - 1 );
	const int transportLoopEnd    = std::clamp( loopEnd_, transportLoopStart + 1, transportTotal );

	midiroll::BridgeServer::Instance().SetTransport(
		phase,
		( renderTime_ - lastPhaseChange_ ) < 0.3,
		transportBpm,
		lastStep_ < 0 ? 0 : lastStep_,
		transportBars,
		transportLoopStart,
		transportLoopEnd );

	if( !beatInfoSeen_ && !noBeatWarned_ && renderTime_ > 3.0 )
	{
		noBeatWarned_ = true;
		FFGLLog::LogToHost( "No host beat info received; running free at fallback tempo" );
	}

	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	ScopedShaderBinding shaderBinding( shader_.GetGLID() );
	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( pGL->inputTextures[ 0 ]->Handle );

	shader_.Set( "InputTexture", 0 );

	const FFGLTexCoords maxCoords = GetMaxGLTexCoords( *pGL->inputTextures[ 0 ] );
	shader_.Set( "MaxUV", maxCoords.s, maxCoords.t );

	quad_.Draw();

	return FF_SUCCESS;
}

// ---------------------------------------------------------------------------
// Sequencer
// ---------------------------------------------------------------------------

void MidiRollPlugin::DriveSequencer( float deltaSeconds )
{
	long long barIndex = 0;
	float barPhase     = 0.0f;
	float bpm          = fallbackBpm_;
	(void)barIndex;

	if( beatInfoSeen_ && hostBpm_ > 1.0f )
	{
		// Host position is authoritative: the phase freezes when the host pauses,
		// so the sequencer pauses with it and never drifts against the timeline.
		barPhase = hostBarPhase_;
		bpm      = hostBpm_;
		fallbackBpm_ = hostBpm_;

		if( barPhase < lastBarPhase_ - 0.5f )
		{
			++barCount_;

			if( !wrapLogged_ )
			{
				wrapLogged_ = true;

				const double nowWall = std::chrono::duration< double >( std::chrono::steady_clock::now().time_since_epoch() ).count();

				char message[ 128 ] = {};
				std::snprintf( message, sizeof( message ), "Host bar wrapped after %.2f s (expected %.2f s)", nowWall - firstBeatWall_, ( 60.0f / hostBpm_ ) * kBeatsPerBar );
				FFGLLog::LogToHost( message );
			}
		}

		lastBarPhase_ = barPhase;
		barIndex      = barCount_;
	}
	else
	{
		// No host clock: run free so the plugin still makes sound on its own.
		fallbackBeats_ += static_cast< double >( deltaSeconds ) * ( fallbackBpm_ / 60.0 );
		barIndex = static_cast< long long >( fallbackBeats_ / kBeatsPerBar );
		barPhase = static_cast< float >( fallbackBeats_ / kBeatsPerBar - static_cast< double >( barIndex ) );
	}

	// The pattern spans `bars` bars at a fixed 1/32 resolution. Length is only
	// the editor's grid setting, so playback resolution never changes.
	const int bars = std::clamp( bars_, 1, 16 );
	const int stepsPerBar = kMaxSteps;
	const int totalSteps = bars * stepsPerBar;

	currentPhase_ = barPhase;

	// Shift the detection phase to compensate for latency after the plugin.
	const float barsPerSecond = bpm / 60.0f / kBeatsPerBar;
	const float offsetBars    = ( offsetMs_ / 1000.0f ) * barsPerSecond;

	float detectionPhase = barPhase + offsetBars;
	detectionPhase -= std::floor( detectionPhase );

	// Absolute host position keeps the loop mapping continuous across bars.
	const double hostBeats = static_cast< double >( barIndex ) * kBeatsPerBar
		+ static_cast< double >( detectionPhase ) * kBeatsPerBar;

	int loopStart = std::clamp( loopStart_, 0, totalSteps - 1 );
	int loopEnd   = std::clamp( loopEnd_, loopStart + 1, totalSteps );

	if( loopEnd <= loopStart )
	{
		loopStart = 0;
		loopEnd   = totalSteps;
	}

	const double loopStartBeats = static_cast< double >( loopStart ) / static_cast< double >( kStepsPerBeat );
	const double loopLength     = static_cast< double >( loopEnd - loopStart ) / static_cast< double >( kStepsPerBeat );

	double position = hostBeats;
	if( loopLength > 1e-6 )
	{
		double relative = std::fmod( hostBeats, loopLength );
		if( relative < 0.0 )
			relative += loopLength;

		position = loopStartBeats + relative;
	}

	const int step = std::clamp( static_cast< int >( position * static_cast< double >( kStepsPerBeat ) ), 0, totalSteps - 1 );

	if( step == lastStep_ )
		return;

	lastStep_ = step;

	const float stepSeconds = ( 60.0f / std::max( bpm, 1.0f ) ) * ( kBeatsPerBar / static_cast< float >( stepsPerBar ) );

	FireStep( step, stepSeconds );
}

void MidiRollPlugin::FireStep( int step, float stepSeconds )
{
	if( step < 0 || step >= kMaxSteps * std::max( 1, bars_ ) )
		return;

	// Free note list wins when the piano roll has written one.
	if( noteCount_ > 0 )
	{
		for( int i = 0; i < noteCount_; ++i )
		{
			const Note& note = notes_[ i ];
			if( note.s != step )
				continue;

			const int lane = LaneForPitch( note.p );

			midi_.NoteOn( midiChannel_, note.p, note.v );

			if( lane >= 0 )
			{
				envelopes_.Trigger( lane, static_cast< float >( note.v ) / 127.0f, decaySeconds_ );
				ScheduleNoteOff( note.p, lane, stepSeconds * static_cast< float >( note.l ) * 0.9f, midiChannel_ );
			}
			else
			{
				// More distinct pitches than envelope lanes: the note still
				// plays, it just has no envelope of its own.
				ScheduleNoteOff( note.p, -1, stepSeconds * static_cast< float >( note.l ) * 0.9f, midiChannel_ );
			}
		}

		return;
	}

	// Legacy 4 lane patterns were written on a 16 step grid, so they fire on
	// every second 1/32 step.
	if( step % 2 != 0 )
		return;

	const int laneStep = ( step / 2 ) % 16;
	const float gateDuration = stepSeconds;

	for( int lane = 0; lane < kLanes; ++lane )
	{
		const float velocity = pattern_[ lane ][ laneStep ];
		if( velocity <= 0.0f )
			continue;

		const int note = kBaseNote + lane;

		midi_.NoteOn( midiChannel_, note, static_cast< int >( std::lround( velocity * 127.0f ) ) );
		envelopes_.Trigger( lane, velocity, decaySeconds_ );
		ScheduleNoteOff( note, lane, gateDuration, midiChannel_ );
	}
}

void MidiRollPlugin::ScheduleNoteOff( int note, int lane, float delaySeconds, int channel )
{
	if( pendingCount_ >= kMaxPendingOffs )
	{
		// Queue is full: release the oldest note now instead of leaving it stuck.
		const PendingNoteOff dropped = pendingOffs_[ 0 ];
		midi_.NoteOff( dropped.channel, dropped.note );
		if( dropped.lane >= 0 )
			envelopes_.Release( dropped.lane );

		pendingOffs_[ 0 ] = pendingOffs_[ pendingCount_ - 1 ];
		--pendingCount_;
	}

	PendingNoteOff& entry = pendingOffs_[ pendingCount_++ ];
	entry.note            = note;
	entry.lane            = lane;
	entry.channel         = channel;
	entry.remaining       = delaySeconds;
}

void MidiRollPlugin::FlushNoteOffs( float deltaSeconds )
{
	for( int i = 0; i < pendingCount_; )
	{
		PendingNoteOff& entry = pendingOffs_[ i ];
		entry.remaining -= deltaSeconds;

		if( entry.remaining > 0.0f )
		{
			++i;
			continue;
		}

		midi_.NoteOff( entry.channel, entry.note );
		if( entry.lane >= 0 )
			envelopes_.Release( entry.lane );

		// Swap with the tail so removal stays O(1) and order doesn't matter.
		pendingOffs_[ i ] = pendingOffs_[ pendingCount_ - 1 ];
		--pendingCount_;
	}
}

void MidiRollPlugin::StopAll()
{
	lastStep_     = -1;
	pendingCount_ = 0;
	ccTestActive_ = false;
	ccTestElapsed_ = 0.0f;
	midi_.AllNotesOff();
	envelopes_.Reset();
	envCcSent_[ 0 ] = envCcSent_[ 1 ] = envCcSent_[ 2 ] = envCcSent_[ 3 ] = -1;
}

// ---------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------

void MidiRollPlugin::PushEnvelopeParams()
{
	static const unsigned int kParamIds[ kLanes ] = { PT_ENV1, PT_ENV2, PT_ENV3, PT_ENV4 };

	for( int lane = 0; lane < kLanes; ++lane )
	{
		const float value = envelopes_.Value( lane );

		// Only tell the host when the value moved enough to be visible. This
		// keeps event traffic low while still tracking the curve smoothly.
		if( std::fabs( value - envPushed_[ lane ] ) > 0.002f )
		{
			envPushed_[ lane ] = value;
			RaiseParamEvent( kParamIds[ lane ], FF_EVENT_FLAG_VALUE );
		}

		if( !midi_.IsOpen() || ccTestActive_ )
			continue;

		const int ccValue = static_cast< int >( std::lround( value * 127.0f ) );
		if( ccValue != envCcSent_[ lane ] )
		{
			envCcSent_[ lane ] = ccValue;
			midi_.ControlChange( CcChannel(), kCcBase + lane, ccValue );
		}
	}
}

int MidiRollPlugin::CcChannel() const
{
	return ccChannelElement_ > 0 ? ccChannelElement_ - 1 : midiChannel_;
}

void MidiRollPlugin::UpdateCcTest( float deltaSeconds )
{
	if( !ccTestActive_ )
		return;

	ccTestElapsed_ += deltaSeconds;

	if( midi_.IsOpen() )
	{
		// 0..127..0 triangle over 1.6 s, each lane a quarter cycle apart. The
		// first CC (21) is sent first, so MIDI learn grabs Env 1; the other
		// lanes can be selected by editing the learned CC number.
		constexpr float kCycle = 1.6f;
		const float phase   = std::fmod( ccTestElapsed_ / kCycle, 1.0f );
		const int channel   = CcChannel();

		for( int lane = 0; lane < kLanes; ++lane )
		{
			const float shifted  = std::fmod( phase + 0.25f * static_cast< float >( lane ), 1.0f );
			const float triangle = 1.0f - std::fabs( 2.0f * shifted - 1.0f );
			const int ccValue    = static_cast< int >( std::lround( triangle * 127.0f ) );
			midi_.ControlChange( channel, kCcBase + lane, ccValue );
		}
	}
}

void MidiRollPlugin::OpenConfiguredPort()
{
	midi_.AllNotesOff();

	if( portElement_ <= 0 )
	{
		midi_.Close();
		return;
	}

	midi_.Open( portElement_ - 1 );
	// A fresh port must receive the current envelope values even if they have
	// not changed since the last frame.
	envCcSent_[ 0 ] = envCcSent_[ 1 ] = envCcSent_[ 2 ] = envCcSent_[ 3 ] = -1;
}

// ---------------------------------------------------------------------------
// Pattern bridge
// ---------------------------------------------------------------------------

namespace
{
	// Four velocity levels. The whole pattern is 4 x 16 characters, one digit per
	// step, so it survives a text parameter and a URL body without escaping.
	const float kVelocitySteps[ 4 ] = { 0.0f, 0.4f, 0.7f, 1.0f };

	// Reads `"key": number` out of the flat Notes JSON. The editor writes that
	// object itself, so a full parser would be overkill.
	int ReadIntField( const std::string& text, const char* key, int fallback )
	{
		const size_t pos = text.find( key );
		if( pos == std::string::npos )
			return fallback;

		const char* c = text.c_str() + pos + std::strlen( key );

		while( *c && *c != ':' )
			++c;
		if( *c == ':' )
			++c;
		while( *c == ' ' )
			++c;

		if( *c < '0' || *c > '9' )
			return fallback;

		int value = 0;
		while( *c >= '0' && *c <= '9' )
		{
			value = value * 10 + ( *c - '0' );
			++c;
		}

		return value;
	}
}

bool MidiRollPlugin::ParseNotes( const std::string& text )
{
	Note parsed[ kMaxNotes ] = {};
	int count = 0;

	// Empty text means "no free notes": fall back to the legacy pattern and
	// reset the clip length so nothing stale survives.
	if( text.find( '[' ) == std::string::npos )
	{
		noteCount_ = 0;
		notesText_.clear();
		bars_      = 1;
		lengthModeManual_ = false;
		loopStart_ = 0;
		loopEnd_   = kMaxSteps;
		UpdateEnvelopeLanes( false );
		lanesDirty_ = true;
		return true;
	}

	// Old lists were written in grid steps. Scale them into the fixed 1/32 grid
	// so a grid change never alters the music. New lists carry "res":32.
	const bool legacy = text.find( "\"res\"" ) == std::string::npos;
	const float scale = legacy
		? static_cast< float >( kMaxSteps ) / static_cast< float >( std::max( division_, 4 ) )
		: 1.0f;

	// Clip length and loop points come from the editor, which reads the length
	// from Resolume's transport setting.
	bars_ = std::clamp( ReadIntField( text, "\"bars\"", 1 ), 1, 16 );

	// "lengthMode":"manual" pins the length. Missing field = auto.
	const size_t modePos = text.find( "\"lengthMode\"" );
	if( modePos != std::string::npos )
	{
		const size_t manualPos = text.find( "manual", modePos );
		const size_t nextComma = text.find( ',', modePos );
		lengthModeManual_ = manualPos != std::string::npos
			&& ( nextComma == std::string::npos || manualPos < nextComma );
	}
	else
	{
		lengthModeManual_ = false;
	}

	const int totalSteps = bars_ * kMaxSteps;
	const int maxStored  = kMaxSteps * 16;

	const int parsedLoopStart = ReadIntField( text, "\"loopStart\"", 0 );
	const int parsedLoopEnd   = ReadIntField( text, "\"loopEnd\"", totalSteps );

	loopStart_ = std::clamp( parsedLoopStart, 0, totalSteps - 1 );
	loopEnd_   = std::clamp( parsedLoopEnd, loopStart_ + 1, totalSteps );

	if( loopEnd_ <= loopStart_ )
	{
		loopStart_ = 0;
		loopEnd_   = totalSteps;
	}

	const size_t notesPos = text.find( "\"notes\"" );
	const char* cursor = text.c_str();
	if( notesPos != std::string::npos )
		cursor += notesPos;

	while( *cursor && *cursor != '[' )
		++cursor;

	if( *cursor != '[' )
		return false;

	++cursor;

	while( *cursor )
	{
		while( *cursor == ' ' || *cursor == ',' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t' )
			++cursor;

		if( *cursor == ']' )
			break;

		if( *cursor != '[' )
			return false;

		++cursor;

		int values[ 4 ] = { 0, 0, 0, 0 };

		for( int field = 0; field < 4; ++field )
		{
			while( *cursor == ' ' )
				++cursor;

			if( *cursor < '0' || *cursor > '9' )
				return false;

			int number = 0;
			while( *cursor >= '0' && *cursor <= '9' )
			{
				number = number * 10 + ( *cursor - '0' );
				++cursor;
			}

			values[ field ] = number;

			while( *cursor == ' ' )
				++cursor;

			if( field < 3 )
			{
				if( *cursor != ',' )
					return false;

				++cursor;
			}
		}

		if( *cursor != ']' )
			return false;

		++cursor;

		if( count < kMaxNotes )
		{
			Note& note = parsed[ count++ ];
			note.p     = std::clamp( values[ 0 ], 0, 127 );
			note.s     = std::clamp( static_cast< int >( std::lround( values[ 1 ] * scale ) ), 0, maxStored - 1 );
			note.l     = std::clamp( static_cast< int >( std::lround( values[ 2 ] * scale ) ), 1, 64 );
			note.v     = std::clamp( values[ 3 ], 1, 127 );
		}
	}

	for( int i = 0; i < count; ++i )
		notes_[ i ] = parsed[ i ];

	noteCount_ = count;
	// Keep the JSON even when it has no notes: it still carries the clip length
	// and loop points, and the plugin mirrors those back into the host.
	notesText_ = text.find( '{' ) != std::string::npos ? text : std::string();

	UpdateEnvelopeLanes( false );
	lanesDirty_ = true;

	return true;
}

void MidiRollPlugin::UpdateEnvelopeLanes( bool raiseEvent )
{
	for( int pitch = 0; pitch < 128; ++pitch )
		pitchLane_[ pitch ] = -1;

	if( noteCount_ <= 0 )
	{
		activeLanes_ = kLanes;
	}
	else
	{
		bool used[ 128 ] = {};
		int unique[ 128 ] = {};
		int uniqueCount = 0;

		for( int i = 0; i < noteCount_; ++i )
		{
			const int pitch = notes_[ i ].p;
			if( pitch >= 0 && pitch < 128 && !used[ pitch ] )
			{
				used[ pitch ] = true;
				unique[ uniqueCount++ ] = pitch;
			}
		}

		// Fold lists the highest note first; Env 1 follows that order.
		std::sort( unique, unique + uniqueCount, []( int a, int b ) { return a > b; } );

		activeLanes_ = std::min( kLanes, uniqueCount );
		for( int lane = 0; lane < activeLanes_; ++lane )
			pitchLane_[ unique[ lane ] ] = lane;
	}

	for( int lane = 0; lane < kLanes; ++lane )
		SetParamVisibility( PT_ENV1 + lane, lane < activeLanes_, raiseEvent );
}

int MidiRollPlugin::LaneForPitch( int pitch ) const
{
	if( pitch < 0 || pitch >= 128 )
		return -1;

	return pitchLane_[ pitch ];
}

int MidiRollPlugin::ProgressionRoot( int bar ) const
{
	if( progressionCount_ <= 0 )
		return rootIndex_;

	const int index = ( ( bar % progressionCount_ ) + progressionCount_ ) % progressionCount_;
	return progressionRoots_[ index ];
}

void MidiRollPlugin::ParseProgression( const std::string& text )
{
	progressionText_ = text;
	progressionCount_ = 0;

	static const int kSemitone[ 7 ] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G

	for( size_t i = 0; i < text.size() && progressionCount_ < 8; )
	{
		while( i < text.size() && ( text[ i ] == ' ' || text[ i ] == ',' || text[ i ] == '\t' ) )
			++i;

		if( i >= text.size() )
			break;

		const char letter = static_cast< char >( std::toupper( static_cast< unsigned char >( text[ i ] ) ) );
		if( letter < 'A' || letter > 'G' )
		{
			++i;
			continue;
		}

		int root = kSemitone[ letter - 'A' ];
		++i;

		if( i < text.size() && ( text[ i ] == '#' || text[ i ] == 'b' ) )
		{
			root += ( text[ i ] == '#' ) ? 1 : -1;
			++i;
		}

		const int slot = progressionCount_;
		progressionRoots_[ slot ] = ( ( root % 12 ) + 12 ) % 12;

		// Parse the chord suffix: m = minor, maj = major, 7 = dominant,
		// dim = diminished. Empty means "use the global Scale".
		std::string suffix;
		while( i < text.size() && text[ i ] != ' ' && text[ i ] != ',' && text[ i ] != '\t' )
		{
			suffix.push_back( static_cast< char >( std::tolower( static_cast< unsigned char >( text[ i ] ) ) ) );
			++i;
		}

		int quality = 1; // bare chord = major
		if( suffix.find( "*" ) != std::string::npos )
			quality = 0; // explicit global scale
		else if( suffix.find( "dim" ) != std::string::npos )
			quality = 4;
		else if( suffix.find( "maj" ) != std::string::npos )
			quality = 1;
		else if( suffix.find( "m" ) != std::string::npos )
			quality = 2;
		else if( suffix.find( "7" ) != std::string::npos )
			quality = 3;

		progressionQualities_[ slot ] = quality;
		++progressionCount_;
	}
}

int MidiRollPlugin::ProgressionQuality( int bar ) const
{
	if( progressionCount_ <= 0 )
		return 0;

	const int index = ( ( bar % progressionCount_ ) + progressionCount_ ) % progressionCount_;
	return progressionQualities_[ index ];
}

int MidiRollPlugin::DensityPercent( int minPercent, int maxPercent ) const
{
	const float d = std::clamp( density_, 0.0f, 1.0f );
	return minPercent + static_cast< int >( std::lround( d * static_cast< float >( maxPercent - minPercent ) ) );
}

int MidiRollPlugin::ScalePitch( int degree, int octave ) const
{
	const int root = templateRoot_ >= 0 ? templateRoot_ : rootIndex_;
	return ScalePitch( degree, octave, root, templateQuality_ );
}

int MidiRollPlugin::ScalePitch( int degree, int octave, int root, int quality ) const
{
	static const int kScales[ 6 ][ 7 ] = {
		{ 0, 2, 4, 5, 7, 9, 11 },   // Major
		{ 0, 2, 3, 5, 7, 8, 10 },   // Natural Minor
		{ 0, 2, 3, 5, 7, 9, 10 },   // Dorian
		{ 0, 2, 4, 5, 7, 9, 10 },   // Mixolydian
		{ 0, 3, 5, 7, 10, 12, 15 }, // Pentatonic Minor
		{ 0, 2, 4, 7, 9, 12, 14 },  // Pentatonic Major
	};
	static const int kQualityScales[ 5 ][ 7 ] = {
		{ 0, 2, 4, 5, 7, 9, 11 }, // 0: global (unused)
		{ 0, 2, 4, 5, 7, 9, 11 }, // 1: Major
		{ 0, 2, 3, 5, 7, 8, 10 }, // 2: Minor
		{ 0, 2, 4, 5, 7, 9, 10 }, // 3: Dominant 7
		{ 0, 2, 3, 5, 6, 8, 9 },  // 4: Diminished
	};

	const int scale = std::clamp( scaleIndex_, 0, 5 );
	const int* intervals = ( quality >= 1 && quality <= 4 )
		? kQualityScales[ quality ]
		: kScales[ scale ];

	int shift = 0;

	while( degree < 0 )
	{
		degree += 7;
		--shift;
	}

	while( degree >= 7 )
	{
		degree -= 7;
		++shift;
	}

	const int pitch = root + 12 * ( octave + 1 + shift ) + intervals[ degree ];
	return std::clamp( pitch, 0, 127 );
}

int MidiRollPlugin::BassPitch( int interval, int octave ) const
{
	const int root = templateRoot_ >= 0 ? templateRoot_ : rootIndex_;
	return std::clamp( root + 12 * ( octave + 1 ) + interval, 0, 127 );
}

void MidiRollPlugin::AddTemplateNote( int pitch, int step, int length, int velocity )
{
	if( noteCount_ >= kMaxNotes )
		return;

	Note& note = notes_[ noteCount_++ ];
	note.p = std::clamp( pitch, 0, 127 );
	note.s = std::clamp( step + templateBarOffset_, 0, kMaxSteps * 16 - 1 );
	note.l = std::clamp( length, 1, 64 );
	note.v = std::clamp( velocity, 1, 127 );
}

void MidiRollPlugin::AddTemplateDrums( int genre )
{
	switch( genre )
	{
	case 0: // Techno
	{
		const int kicks[] = { 0, 8, 16, 24 };
		const int hats[]  = { 4, 12, 20, 28 };
		for( int step : kicks ) AddTemplateNote( 36, step, 2, 122 );
		AddTemplateNote( 38, 8, 2, 108 );
		AddTemplateNote( 38, 24, 2, 108 );
		for( int step : hats ) AddTemplateNote( 42, step, 1, 70 );
		break;
	}
	case 1: // House
	{
		const int kicks[] = { 0, 8, 16, 24 };
		const int hats[]  = { 4, 12, 20, 28 };
		const int opens[] = { 2, 10, 18, 26 };
		for( int step : kicks ) AddTemplateNote( 36, step, 2, 120 );
		AddTemplateNote( 38, 8, 2, 115 );
		AddTemplateNote( 38, 24, 2, 115 );
		for( int step : hats ) AddTemplateNote( 42, step, 1, 80 );
		for( int step : opens ) AddTemplateNote( 46, step, 2, 58 );
		break;
	}
	case 2: // Trance
	{
		const int kicks[] = { 0, 8, 16, 24 };
		const int hats[]  = { 4, 12, 20, 28 };
		const int opens[] = { 2, 6, 10, 14, 18, 22, 26, 30 };
		for( int step : kicks ) AddTemplateNote( 36, step, 2, 124 );
		AddTemplateNote( 38, 8, 2, 110 );
		AddTemplateNote( 38, 24, 2, 110 );
		for( int step : hats ) AddTemplateNote( 42, step, 1, 75 );
		for( int step : opens ) AddTemplateNote( 46, step, 1, 52 );
		break;
	}
	case 3: // Drum & Bass
	{
		AddTemplateNote( 36, 0, 2, 125 );
		AddTemplateNote( 36, 10, 2, 110 );
		AddTemplateNote( 36, 16, 2, 125 );
		AddTemplateNote( 36, 26, 2, 110 );
		AddTemplateNote( 38, 8, 2, 118 );
		AddTemplateNote( 38, 24, 2, 118 );
		const int hats[] = { 2, 6, 10, 14, 18, 22, 26, 30 };
		for( int step : hats ) AddTemplateNote( 42, step, 1, 78 );
		break;
	}
	default: // Hip-Hop
	{
		AddTemplateNote( 36, 0, 3, 120 );
		AddTemplateNote( 36, 6, 2, 105 );
		AddTemplateNote( 36, 16, 3, 120 );
		AddTemplateNote( 36, 22, 2, 105 );
		AddTemplateNote( 38, 8, 3, 115 );
		AddTemplateNote( 38, 24, 3, 115 );
		const int hats[] = { 2, 4, 6, 10, 12, 14, 18, 20, 22, 26, 28, 30 };
		for( int step : hats ) AddTemplateNote( 42, step, 1, 68 );
		break;
	}
	}
}

void MidiRollPlugin::AddTemplateBass( int genre )
{
	// Bass is root-centric: root, fifth, octave. It should anchor the bar, not
	// wander like a melody.
	switch( genre )
	{
	case 0: // Techno
		AddTemplateNote( BassPitch( 0, 2 ), 0, 4, 110 );
		AddTemplateNote( BassPitch( 0, 2 ), 8, 2, 100 );
		AddTemplateNote( BassPitch( 0, 2 ), 12, 2, 95 );
		AddTemplateNote( BassPitch( 0, 2 ), 16, 4, 110 );
		AddTemplateNote( BassPitch( 7, 2 ), 24, 2, 95 );
		AddTemplateNote( BassPitch( 0, 2 ), 28, 2, 100 );
		break;

	case 1: // House
		AddTemplateNote( BassPitch( 0, 2 ), 0, 3, 110 );
		AddTemplateNote( BassPitch( 0, 2 ), 6, 2, 95 );
		AddTemplateNote( BassPitch( 7, 2 ), 10, 2, 100 );
		AddTemplateNote( BassPitch( 0, 2 ), 16, 3, 110 );
		AddTemplateNote( BassPitch( 12, 2 ), 22, 2, 95 );
		AddTemplateNote( BassPitch( 7, 2 ), 26, 2, 100 );
		break;

	case 2: // Trance, rolling eighth notes on the root with fifth accents
	{
		const int eighths[] = { 0, 4, 8, 12, 16, 20, 24, 28 };
		for( int step : eighths ) AddTemplateNote( BassPitch( 0, 2 ), step, 2, 105 );
		AddTemplateNote( BassPitch( 7, 2 ), 6, 2, 90 );
		AddTemplateNote( BassPitch( 7, 2 ), 14, 2, 90 );
		AddTemplateNote( BassPitch( 7, 2 ), 22, 2, 90 );
		AddTemplateNote( BassPitch( 7, 2 ), 30, 2, 90 );
		break;
	}

	case 3: // Drum & Bass, sub root with a low fifth
		AddTemplateNote( BassPitch( 0, 2 ), 0, 6, 115 );
		AddTemplateNote( BassPitch( 7, 1 ), 10, 4, 100 );
		AddTemplateNote( BassPitch( 0, 2 ), 16, 6, 115 );
		AddTemplateNote( BassPitch( 0, 1 ), 26, 4, 100 );
		break;

	default: // Hip-Hop
		AddTemplateNote( BassPitch( 0, 2 ), 0, 4, 110 );
		AddTemplateNote( BassPitch( 0, 2 ), 6, 2, 95 );
		AddTemplateNote( BassPitch( 7, 2 ), 12, 4, 100 );
		AddTemplateNote( BassPitch( 0, 2 ), 20, 4, 100 );
		AddTemplateNote( BassPitch( 0, 2 ), 26, 2, 95 );
		break;
	}
}

void MidiRollPlugin::AddTemplateMelody( int genre )
{
	// Melody uses scale degrees, octave 4.
	switch( genre )
	{
	case 0: // Techno, minor
		AddTemplateNote( ScalePitch( 0, 4 ), 0, 4, 95 );
		AddTemplateNote( ScalePitch( 2, 4 ), 4, 4, 90 );
		AddTemplateNote( ScalePitch( 4, 4 ), 8, 4, 95 );
		AddTemplateNote( ScalePitch( 2, 4 ), 12, 4, 90 );
		AddTemplateNote( ScalePitch( 0, 4 ), 16, 4, 95 );
		AddTemplateNote( ScalePitch( 6, 4 ), 20, 4, 90 );
		AddTemplateNote( ScalePitch( 4, 4 ), 24, 4, 95 );
		AddTemplateNote( ScalePitch( 2, 4 ), 28, 4, 90 );
		break;

	case 1: // House, major
		AddTemplateNote( ScalePitch( 0, 4 ), 0, 4, 100 );
		AddTemplateNote( ScalePitch( 4, 4 ), 4, 4, 95 );
		AddTemplateNote( ScalePitch( 7, 4 ), 8, 4, 100 );
		AddTemplateNote( ScalePitch( 4, 4 ), 12, 4, 95 );
		AddTemplateNote( ScalePitch( 2, 4 ), 16, 4, 100 );
		AddTemplateNote( ScalePitch( 7, 4 ), 20, 4, 95 );
		AddTemplateNote( ScalePitch( 4, 4 ), 24, 4, 100 );
		AddTemplateNote( ScalePitch( 2, 4 ), 28, 4, 95 );
		break;

	case 2: // Trance, uplifting
		AddTemplateNote( ScalePitch( 0, 4 ), 0, 4, 100 );
		AddTemplateNote( ScalePitch( 4, 4 ), 4, 4, 100 );
		AddTemplateNote( ScalePitch( 7, 4 ), 8, 4, 105 );
		AddTemplateNote( ScalePitch( 5, 4 ), 12, 4, 100 );
		AddTemplateNote( ScalePitch( 4, 4 ), 16, 4, 100 );
		AddTemplateNote( ScalePitch( 7, 4 ), 20, 4, 105 );
		AddTemplateNote( ScalePitch( 5, 4 ), 24, 4, 100 );
		AddTemplateNote( ScalePitch( 4, 4 ), 28, 4, 100 );
		break;

	case 3: // Drum & Bass, dark
		AddTemplateNote( ScalePitch( 0, 4 ), 0, 4, 95 );
		AddTemplateNote( ScalePitch( 3, 4 ), 4, 4, 90 );
		AddTemplateNote( ScalePitch( 4, 4 ), 8, 4, 95 );
		AddTemplateNote( ScalePitch( 2, 4 ), 12, 4, 90 );
		AddTemplateNote( ScalePitch( 0, 4 ), 16, 4, 95 );
		AddTemplateNote( ScalePitch( 4, 4 ), 20, 4, 90 );
		AddTemplateNote( ScalePitch( 3, 4 ), 24, 4, 95 );
		AddTemplateNote( ScalePitch( 2, 4 ), 28, 4, 90 );
		break;

	default: // Hip-Hop, minor and syncopated
		AddTemplateNote( ScalePitch( 0, 4 ), 0, 4, 100 );
		AddTemplateNote( ScalePitch( 2, 4 ), 6, 2, 90 );
		AddTemplateNote( ScalePitch( 4, 4 ), 12, 4, 95 );
		AddTemplateNote( ScalePitch( 1, 4 ), 18, 2, 90 );
		AddTemplateNote( ScalePitch( 0, 4 ), 24, 4, 100 );
		AddTemplateNote( ScalePitch( 2, 4 ), 28, 2, 90 );
		break;
	}
}

void MidiRollPlugin::PushHistory()
{
	const std::string snapshot = BuildNotesJson();

	if( historyCount_ > 0 && history_[ historyCount_ - 1 ] == snapshot )
		return;

	if( historyCount_ >= 8 )
	{
		for( int i = 1; i < 8; ++i )
			history_[ i - 1 ] = history_[ i ];

		--historyCount_;
	}

	history_[ historyCount_++ ] = snapshot;
}

void MidiRollPlugin::RestorePrevious()
{
	if( historyCount_ <= 0 )
		return;

	const std::string snapshot = history_[ --historyCount_ ];
	ParseNotes( snapshot );
	RaiseParamEvent( PT_NOTES, FF_EVENT_FLAG_VALUE );
}

void MidiRollPlugin::ApplyTemplate( int genre, int part )
{
	noteCount_ = 0;
	genreIndex_ = std::clamp( genre, 0, 4 );
	templateIndex_ = std::clamp( part, 0, 4 );
	generatorBars_ = std::clamp( generatorBars_, 1, 8 );

	const int which = templateIndex_;
	const int bars = generatorBars_;

	bars_ = bars;
	lengthModeManual_ = true;
	loopStart_ = 0;
	loopEnd_ = bars * kMaxSteps;

	for( int bar = 0; bar < bars; ++bar )
	{
		templateBarOffset_ = bar * kMaxSteps;
		templateRoot_ = ProgressionRoot( bar );
		templateQuality_ = ProgressionQuality( bar );

		if( which == 1 || which == 4 )
			AddTemplateDrums( genreIndex_ );

		if( which == 2 || which == 4 )
			AddTemplateBass( genreIndex_ );

		if( which == 3 || which == 4 )
			AddTemplateMelody( genreIndex_ );
	}

	templateBarOffset_ = 0;
	templateRoot_ = -1;
	templateQuality_ = 0;

	notesText_ = BuildNotesJson();
	RaiseParamEvent( PT_NOTES, FF_EVENT_FLAG_VALUE );
	UpdateEnvelopeLanes( false );
	lanesDirty_ = true;
}

unsigned int MidiRollPlugin::NextRandom()
{
	unsigned int x = rngState_;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rngState_ = x;
	return x;
}

int MidiRollPlugin::RandomInt( int min, int max )
{
	if( max <= min )
		return min;

	return min + static_cast< int >( NextRandom() % static_cast< unsigned int >( max - min + 1 ) );
}

bool MidiRollPlugin::Chance( int percent )
{
	return RandomInt( 1, 100 ) <= percent;
}

void MidiRollPlugin::AddRandomDrums( int genre )
{
	switch( genre )
	{
	case 0: // Techno: kick stays four on the floor, hats vary
		for( int step : { 0, 8, 16, 24 } )
			AddTemplateNote( 36, step, 2, 118 + RandomInt( 0, 8 ) );

		AddTemplateNote( 38, 8, 2, 104 + RandomInt( 0, 8 ) );
		AddTemplateNote( 38, 24, 2, 104 + RandomInt( 0, 8 ) );

		if( density_ < 0.5f )
		{
			for( int step : { 4, 12, 20, 28 } )
				if( Chance( DensityPercent( 20, 95 ) ) )
					AddTemplateNote( 42, step, 1, 62 + RandomInt( 0, 12 ) );
		}
		else
		{
			for( int step = 2; step < kMaxSteps; step += 4 )
				if( Chance( DensityPercent( 20, 95 ) ) )
					AddTemplateNote( 42, step, 1, 55 + RandomInt( 0, 15 ) );
		}
		break;

	case 1: // House: four on the floor, clap, offbeat hats, sparse open hats
		for( int step : { 0, 8, 16, 24 } )
			AddTemplateNote( 36, step, 2, 116 + RandomInt( 0, 8 ) );

		AddTemplateNote( 38, 8, 2, 110 + RandomInt( 0, 8 ) );
		AddTemplateNote( 38, 24, 2, 110 + RandomInt( 0, 8 ) );

		for( int step : { 4, 12, 20, 28 } )
			if( Chance( DensityPercent( 20, 95 ) ) )
				AddTemplateNote( 42, step, 1, 75 + RandomInt( 0, 10 ) );

		for( int step = 2; step < kMaxSteps; step += 4 )
			if( Chance( DensityPercent( 0, 50 ) ) )
				AddTemplateNote( 46, step, 2, 48 + RandomInt( 0, 12 ) );
		break;

	case 2: // Trance: driving kick, denser open hats
		for( int step : { 0, 8, 16, 24 } )
			AddTemplateNote( 36, step, 2, 120 + RandomInt( 0, 6 ) );

		AddTemplateNote( 38, 8, 2, 108 + RandomInt( 0, 6 ) );
		AddTemplateNote( 38, 24, 2, 108 + RandomInt( 0, 6 ) );

		for( int step : { 4, 12, 20, 28 } )
			if( Chance( DensityPercent( 30, 95 ) ) )
				AddTemplateNote( 42, step, 1, 70 + RandomInt( 0, 10 ) );

		for( int step = 2; step < kMaxSteps; step += 4 )
			if( Chance( DensityPercent( 0, 55 ) ) )
				AddTemplateNote( 46, step, 1, 46 + RandomInt( 0, 10 ) );
		break;

	case 3: // Drum & Bass: pick one of three broken kick patterns
	{
		static const int kicks[ 3 ][ 4 ] = {
			{ 0, 10, 16, 26 },
			{ 0, 10, 16, 22 },
			{ 0, 6, 16, 26 },
		};
		const int* pattern = kicks[ RandomInt( 0, 2 ) ];

		for( int i = 0; i < 4; ++i )
			AddTemplateNote( 36, pattern[ i ], 2, 116 + RandomInt( 0, 10 ) );

		AddTemplateNote( 38, 8, 2, 112 + RandomInt( 0, 6 ) );
		AddTemplateNote( 38, 24, 2, 112 + RandomInt( 0, 6 ) );

		for( int step = 2; step < kMaxSteps; step += 4 )
			if( Chance( DensityPercent( 20, 90 ) ) )
				AddTemplateNote( 42, step, 1, 70 + RandomInt( 0, 12 ) );
		break;
	}

	default: // Hip-Hop: boom-bap kick variants and swung hats
	{
		static const int kicks[ 3 ][ 4 ] = {
			{ 0, 6, 16, 22 },
			{ 0, 6, 14, 22 },
			{ 0, 10, 16, 22 },
		};
		const int* pattern = kicks[ RandomInt( 0, 2 ) ];

		for( int i = 0; i < 4; ++i )
			AddTemplateNote( 36, pattern[ i ], 3, 114 + RandomInt( 0, 10 ) );

		AddTemplateNote( 38, 8, 3, 110 + RandomInt( 0, 8 ) );
		AddTemplateNote( 38, 24, 3, 110 + RandomInt( 0, 8 ) );

		static const int hats[] = { 2, 4, 6, 10, 12, 14, 18, 20, 22, 26, 28, 30 };
		for( int step : hats )
			if( Chance( DensityPercent( 20, 90 ) ) )
				AddTemplateNote( 42, step, 1, 58 + RandomInt( 0, 14 ) );
		break;
	}
	}

	// Ghost notes appear as density rises; the core groove stays intact.
	if( Chance( DensityPercent( 0, 30 ) ) )
		AddTemplateNote( 36, 4, 1, 40 + RandomInt( 0, 20 ) );

	if( Chance( DensityPercent( 0, 25 ) ) )
		AddTemplateNote( 38, 30, 1, 40 + RandomInt( 0, 20 ) );
}

void MidiRollPlugin::AddRandomBass( int genre )
{
	// Bass stays on the bar root with occasional fifth/octave accents. It never
	// walks through the scale like the melody.
	int onsets[ 8 ] = {};
	int count = 0;
	int length = 2;
	int octave = 2;

	switch( genre )
	{
	case 0: // Techno
		for( int step : { 0, 4, 8, 12, 16, 20, 24, 28 } ) onsets[ count++ ] = step;
		length = 2;
		break;

	case 1: // House
		for( int step : { 0, 6, 10, 16, 22, 26 } ) onsets[ count++ ] = step;
		length = 3;
		break;

	case 2: // Trance, rolling eighth notes
		for( int step : { 0, 4, 8, 12, 16, 20, 24, 28 } ) onsets[ count++ ] = step;
		length = 2;
		break;

	case 3: // Drum & Bass
		for( int step : { 0, 10, 16, 26 } ) onsets[ count++ ] = step;
		length = 6;
		break;

	default: // Hip-Hop
		for( int step : { 0, 6, 12, 20, 26 } ) onsets[ count++ ] = step;
		length = 3;
		break;
	}

	const int keep = DensityPercent( 30, 100 );
	bool previousColor = false;

	for( int i = 0; i < count; ++i )
	{
		if( i > 0 && !Chance( keep ) )
			continue;

		int interval = 0;

		// First and last note of the bar stay on the root.
		if( i > 0 && i < count - 1 )
		{
			const int roll = RandomInt( 1, 100 );

			if( roll > 92 )
				interval = 12;      // octave
			else if( roll > 70 )
				interval = 7;       // fifth
			else
				interval = 0;       // root

			// Never two color notes in a row.
			if( previousColor && interval != 0 )
				interval = 0;
		}

		previousColor = interval != 0;
		AddTemplateNote( BassPitch( interval, octave ), onsets[ i ], length, 95 + RandomInt( 0, 15 ) );
	}
}

void MidiRollPlugin::AddRandomMelody( int genre )
{
	// Degrees inside the selected scale. The random walk never leaves this set.
	int set[ 4 ] = {};
	int onsets[ 8 ] = {};
	int count = 0;
	int length = 4;

	switch( genre )
	{
	case 0: // Techno
		set[ 0 ] = 0; set[ 1 ] = 2; set[ 2 ] = 4; set[ 3 ] = 6;
		for( int step : { 0, 4, 8, 12, 16, 20, 24, 28 } ) onsets[ count++ ] = step;
		break;

	case 1: // House
		set[ 0 ] = 0; set[ 1 ] = 2; set[ 2 ] = 4; set[ 3 ] = 7;
		for( int step : { 0, 4, 8, 12, 16, 20, 24, 28 } ) onsets[ count++ ] = step;
		break;

	case 2: // Trance
		set[ 0 ] = 0; set[ 1 ] = 4; set[ 2 ] = 5; set[ 3 ] = 7;
		for( int step : { 0, 4, 8, 12, 16, 20, 24, 28 } ) onsets[ count++ ] = step;
		break;

	case 3: // Drum & Bass
		set[ 0 ] = 0; set[ 1 ] = 2; set[ 2 ] = 3; set[ 3 ] = 4;
		for( int step : { 0, 4, 8, 12, 16, 20, 24, 28 } ) onsets[ count++ ] = step;
		break;

	default: // Hip-Hop
		set[ 0 ] = 0; set[ 1 ] = 1; set[ 2 ] = 2; set[ 3 ] = 4;
		for( int step : { 0, 6, 12, 18, 24, 28 } ) onsets[ count++ ] = step;
		break;
	}

	const int keep = DensityPercent( 30, 100 );
	int index = 0;
	int previous = -1;

	for( int i = 0; i < count; ++i )
	{
		if( i > 0 )
		{
			int move = RandomInt( -1, 1 );
			if( move == 0 && Chance( 30 ) )
				move = Chance( 50 ) ? 1 : -1;

			index = std::clamp( index + move, 0, 3 );

			// Avoid three identical notes in a row.
			if( index == previous && Chance( 40 ) )
				index = std::clamp( index + ( Chance( 50 ) ? 1 : -1 ), 0, 3 );

			// Occasional jump inside the genre's four-note set adds contour
			// without leaving the scale.
			if( Chance( 25 ) )
				index = RandomInt( 0, 3 );

			if( i < count - 1 && !Chance( keep ) )
				continue;
		}

		// Resolve to the root at the end of the bar.
		if( i == count - 1 )
			index = 0;

		AddTemplateNote( ScalePitch( set[ index ], 4 ), onsets[ i ], length, 85 + RandomInt( 0, 15 ) );
		previous = index;
	}
}

void MidiRollPlugin::ApplyRandomTemplate( int genre, int part )
{
	noteCount_ = 0;
	genreIndex_ = std::clamp( genre, 0, 4 );
	templateIndex_ = std::clamp( part, 0, 4 );
	generatorBars_ = std::clamp( generatorBars_, 1, 8 );

	const int which = templateIndex_;
	const int bars = generatorBars_;

	bars_ = bars;
	lengthModeManual_ = true;
	loopStart_ = 0;
	loopEnd_ = bars * kMaxSteps;

	for( int bar = 0; bar < bars; ++bar )
	{
		templateBarOffset_ = bar * kMaxSteps;
		templateRoot_ = ProgressionRoot( bar );
		templateQuality_ = ProgressionQuality( bar );

		if( which == 1 || which == 4 )
			AddRandomDrums( genreIndex_ );

		if( which == 2 || which == 4 )
			AddRandomBass( genreIndex_ );

		if( which == 3 || which == 4 )
			AddRandomMelody( genreIndex_ );
	}

	templateBarOffset_ = 0;
	templateRoot_ = -1;
	templateQuality_ = 0;

	notesText_ = BuildNotesJson();
	RaiseParamEvent( PT_NOTES, FF_EVENT_FLAG_VALUE );
	UpdateEnvelopeLanes( false );
	lanesDirty_ = true;
}

void MidiRollPlugin::ApplyEncodedPattern( const std::string& encoded )
{
	// Presets saved before the 32 step grid hold 64 characters: 4 lanes x 16
	// steps. Decode those with the old stride, otherwise the lanes get mixed.
	const bool legacy       = encoded.size() == 64;
	const std::string clean = legacy ? encoded : midiroll::BridgeServer::Sanitize( encoded );
	const int stride        = legacy ? 16 : kMaxSteps;

	for( int lane = 0; lane < kLanes; ++lane )
	{
		for( int step = 0; step < kMaxSteps; ++step )
		{
			const size_t index = static_cast< size_t >( lane * stride + step );
			const char digit   = index < clean.size() ? clean[ index ] : '0';
			const int level    = ( digit >= '0' && digit <= '3' ) ? digit - '0' : 0;

			pattern_[ lane ][ step ] = kVelocitySteps[ level ];
		}
	}

	patternText_ = EncodePattern();
}

std::string MidiRollPlugin::EncodePattern() const
{
	std::string out;
	out.reserve( static_cast< size_t >( kLanes * kMaxSteps ) );

	for( int lane = 0; lane < kLanes; ++lane )
	{
		for( int step = 0; step < kMaxSteps; ++step )
		{
			const float value = pattern_[ lane ][ step ];

			int bestLevel   = 0;
			float bestDelta = 2.0f;
			for( int level = 0; level < 4; ++level )
			{
				const float delta = std::fabs( value - kVelocitySteps[ level ] );
				if( delta < bestDelta )
				{
					bestDelta = delta;
					bestLevel = level;
				}
			}

			out.push_back( static_cast< char >( '0' + bestLevel ) );
		}
	}

	return out;
}

void MidiRollPlugin::PublishCurrentPattern()
{
	midiroll::BridgeServer::Instance().Publish( slot_, EncodePattern() );
}

void MidiRollPlugin::PublishDivision()
{
	char label[ 8 ] = {};
	std::snprintf( label, sizeof( label ), "1/%d", division_ );
	midiroll::BridgeServer::Instance().SetDivision( label );
}

std::string MidiRollPlugin::BuildNotesJson() const
{
	char head[ 192 ] = {};
	std::snprintf( head, sizeof( head ),
		"{\"res\":32,\"bars\":%d,\"lengthMode\":\"%s\",\"loopStart\":%d,\"loopEnd\":%d,\"notes\":[",
		bars_,
		lengthModeManual_ ? "manual" : "auto",
		loopStart_,
		loopEnd_ );

	std::string out = head;

	for( int i = 0; i < noteCount_; ++i )
	{
		if( i > 0 )
			out += ',';

		out += '[';
		out += std::to_string( notes_[ i ].p );
		out += ',';
		out += std::to_string( notes_[ i ].s );
		out += ',';
		out += std::to_string( notes_[ i ].l );
		out += ',';
		out += std::to_string( notes_[ i ].v );
		out += ']';
	}

	out += "]}";
	return out;
}

void MidiRollPlugin::PublishNotes()
{
	// A clip that only uses the legacy Pattern has no JSON to update.
	if( noteCount_ == 0 && notesText_.find( '{' ) == std::string::npos )
		return;

	notesText_ = BuildNotesJson();
	RaiseParamEvent( PT_NOTES, FF_EVENT_FLAG_VALUE );
}

void MidiRollPlugin::PollBridge( float deltaSeconds )
{
	bridgeTimer_ += deltaSeconds;
	if( bridgeTimer_ < 0.15f )
		return;

	bridgeTimer_ = 0.0f;

	std::string encoded;
	if( !midiroll::BridgeServer::Instance().Consume( slot_, encoded ) )
		return;

	ApplyEncodedPattern( encoded );

	// Tell the host so the new pattern lands in the composition and in presets.
	RaiseParamEvent( PT_PATTERN, FF_EVENT_FLAG_VALUE );
}

