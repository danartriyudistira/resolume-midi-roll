#pragma once

#include "FFGLSDK.h"

#include "EnvelopeEngine.h"
#include "MidiOut.h"

#include <chrono>
#include <string>

// Midi Roll
//
// An FFGL effect that doubles as a MIDI source and an envelope modulator.
//   * Plays a 16 step pattern out of a user selected MIDI port/channel.
//   * Takes tempo and play position from the host: no transport buttons, no BPM
//     slider. When Resolume pauses, the sequencer pauses with it.
//   * Converts every trigger into a decaying float the host can read through
//     the Env 1..4 parameters.
//   * Mirrors those envelopes to CC 21..24 so Resolume can MIDI-map them onto
//     any other parameter.
//   * Opens the piano roll editor in the default browser on demand.
class MidiRollPlugin : public CFFGLPlugin
{
public:
	MidiRollPlugin();
	~MidiRollPlugin() override;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult DeInitGL() override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;

	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	unsigned int Connect() override;
	unsigned int Disconnect() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;

private:
	enum ParamId : unsigned int
	{
		PT_PORT = 0,
		PT_CHANNEL,
		PT_LENGTH,
		PT_DECAY,
		PT_EDIT_MODE,
		PT_ENV1,
		PT_ENV2,
		PT_ENV3,
		PT_ENV4,
		PT_PATTERN,
		PT_NOTES,
		PT_OFFSET,
		// Appended after the original parameters so existing presets keep their
		// parameter indices.
		PT_ROOT,
		PT_SCALE,
		PT_GENRE,
		PT_TEMPLATE,
		PT_LOAD_TEMPLATE,
		PT_RANDOM_TEMPLATE,
		PT_BARS_GEN,
		PT_PROGRESSION,
		PT_DENSITY,
		PT_LOAD_V2,
		PT_RANDOM_V2,
		PT_PREVIOUS,
		PT_CC_CHANNEL,
		PT_TEST_CC,
		PT_TEST_LANE,
		PT_COUNT
	};

	static constexpr int kMaxSteps       = 32; // storage width, one bar at 1/32
	static constexpr int kStepsPerBeat   = 8;  // 1/32 steps in one beat
	static constexpr int kLanes          = midiroll::EnvelopeEngine::kLanes;
	static constexpr int kBaseNote       = 36;
	static constexpr int kCcBase         = 21;
	static constexpr int kMaxPendingOffs = 64;
	static constexpr int kMaxNotes       = 128;
	static constexpr float kBeatsPerBar  = 4.0f;

	struct Note
	{
		int p;
		int s;
		int l;
		int v;
	};

	struct PendingNoteOff
	{
		int note;
		int lane;
		int channel;
		float remaining;
	};

	void DriveSequencer( float deltaSeconds );
	float CurrentPhase() const;
	void FireStep( int step, float stepSeconds );
	void ScheduleNoteOff( int note, int lane, float delaySeconds, int channel );
	void FlushNoteOffs( float deltaSeconds );
	void PushEnvelopeParams();
	// Toggles a continuous CC 21..24 sweep so the user can MIDI-learn a
	// dashboard dial without waiting for a note. Press again to stop.
	void UpdateCcTest( float deltaSeconds );
	int CcChannel() const;
	void OpenConfiguredPort();
	void StopAll();

	// Envelope lanes follow the folded note rows: the highest active pitch is
	// Env 1, the next is Env 2, and unused lanes are hidden from the host UI.
	void UpdateEnvelopeLanes( bool raiseEvent );
	int LaneForPitch( int pitch ) const;

	void ApplyEncodedPattern( const std::string& encoded );
	std::string EncodePattern() const;
	bool ParseNotes( const std::string& text );
	void PublishCurrentPattern();
	void PollBridge( float deltaSeconds );
	void PublishDivision();

	// Piano roll state mirrored into Resolume parameters.
	std::string BuildNotesJson() const;
	void PublishNotes();

	// Scale degree -> MIDI pitch. Octave 4 is the melody range, 2 the bass.
	int ScalePitch( int degree, int octave ) const;
	int ScalePitch( int degree, int octave, int root, int quality ) const;
	// Root/fifth/octave for bass, independent of the scale degree layout.
	int BassPitch( int interval, int octave ) const;
	int ProgressionRoot( int bar ) const;
	int ProgressionQuality( int bar ) const;
	void ParseProgression( const std::string& text );
	int DensityPercent( int minPercent, int maxPercent ) const;

	// Pattern history for the Previous button (8 levels).
	void PushHistory();
	void RestorePrevious();

	// Built-in patterns for the effect's Genre/Template selector.
	void ApplyTemplate( int genre, int part );
	void ApplyRandomTemplate( int genre, int part );
	void AddTemplateDrums( int genre );
	void AddTemplateBass( int genre );
	void AddTemplateMelody( int genre );
	void AddRandomDrums( int genre );
	void AddRandomBass( int genre );
	void AddRandomMelody( int genre );
	void AddTemplateNote( int pitch, int step, int length, int velocity );

	unsigned int NextRandom();
	int RandomInt( int min, int max );
	bool Chance( int percent );

	ffglex::FFGLShader shader_;
	ffglex::FFGLScreenQuad quad_;

	midiroll::EnvelopeEngine envelopes_;
	midiroll::MidiOut midi_;

	// --- Host transport ----------------------------------------------------
	double hostTime_         = 0.0;
	double lastHostTime_     = 0.0;
	bool hostTimeSeen_       = false;
	bool hostTimeAdvancing_  = false;

	float hostBpm_           = 0.0f;
	float hostBarPhase_      = 0.0f;
	bool beatInfoSeen_       = false;
	bool beatInfoLogged_     = false;
	long long barCount_      = 0;
	float lastBarPhase_      = 0.0f;
	double firstBeatWall_    = 0.0;
	bool wrapLogged_         = false;

	double renderTime_       = 0.0;
	bool noBeatWarned_       = false;

	float currentPhase_       = 0.0f;
	float lastPublishedPhase_ = -1.0f;
	double lastPhaseChange_   = 0.0;

	// Phase extrapolation: the host only updates the phase once per rendered
	// frame, so we anchor it to a steady clock and advance it ourselves.
	float phaseAnchor_            = 0.0f;
	double phaseAnchorTime_       = 0.0;
	double lastHostPhaseChange_   = 0.0;

	// Fires notes early/late to compensate for downstream latency (loopMIDI,
	// DAW buffer, synth). 0.5 = no shift.
	float offsetNormalized_       = 0.5f;
	float offsetMs_               = 0.0f;

	// Fallback when the host provides no beat info: free running at the last
	// known tempo, or 120 if we never heard one.
	double fallbackBeats_    = 0.0;
	float fallbackBpm_       = 120.0f;

	int lastStep_            = -1;

	// --- Pattern / playback ------------------------------------------------
	float decayNormalized_   = 0.186441f; // 0.6 s
	float decaySeconds_      = 0.6f;
	int division_            = 16; // 4, 8, 16 or 32: note value of one step
	int midiChannel_         = 0;  // 0 based
	int portElement_         = 0;  // 0 = Off, N = device N-1

	float envPushed_[ kLanes ] = {};
	int envCcSent_[ kLanes ]   = { -1, -1, -1, -1 };

	// 0 = same channel as the notes, 1..16 = explicit CC channel.
	int ccChannelElement_      = 0;
	int testLane_              = 0; // 0..3: which Env the Test CC sweep drives
	bool ccTestActive_         = false;
	float ccTestElapsed_       = 0.0f;

	float pattern_[ kLanes ][ kMaxSteps ] = {};

	int slot_                  = 0;
	std::string patternText_;
	float bridgeTimer_         = 0.0f;
	char textBuffer_[ 4096 ]   = {};

	Note notes_[ kMaxNotes ]   = {};
	int noteCount_             = 0;
	int bars_                  = 1;
	int loopStart_             = 0;         // 1/32 steps, inclusive
	int loopEnd_               = kMaxSteps; // 1/32 steps, exclusive
	bool lengthModeManual_     = false;
	int templateIndex_         = 0;
	int genreIndex_            = 0;
	int rootIndex_             = 0;  // 0 = C
	int scaleIndex_            = 1;  // 1 = Natural Minor
	int generatorBars_         = 1;  // 1..8, used by Load/Random
	int progressionRoots_[ 8 ] = {};
	int progressionQualities_[ 8 ] = {};
	int progressionCount_      = 0;
	std::string progressionText_;
	float density_             = 0.5f;
	int templateBarOffset_     = 0;  // bar * 32 while generating
	int templateRoot_          = -1; // per-bar root, -1 = global Root
	int templateQuality_       = 0;  // 0 = global scale, 1 major, 2 minor, 3 dom7, 4 dim
	std::string history_[ 8 ];
	int historyCount_          = 0;
	unsigned int rngState_     = 0x9E3779B9u;
	std::string notesText_;

	// Pitch -> envelope lane for the folded note rows. -1 means the note still
	// plays but has no envelope (more than four distinct pitches).
	int pitchLane_[ 128 ]      = {};
	int activeLanes_           = kLanes;
	bool lanesDirty_           = false;

	PendingNoteOff pendingOffs_[ kMaxPendingOffs ] = {};
	int pendingCount_          = 0;

	std::chrono::steady_clock::time_point lastFrame_;
	bool hasLastFrame_         = false;
};
