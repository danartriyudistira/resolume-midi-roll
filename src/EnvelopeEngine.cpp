#include "EnvelopeEngine.h"

#include <algorithm>
#include <cmath>

namespace midiroll
{
	namespace
	{
		// A decay "time" of T seconds should read as ~95% gone after T seconds.
		// exp(-T / tau) = 0.05  =>  tau = T / 3.
		constexpr float kDecayShape = 3.0f;
		constexpr float kMinTau     = 0.005f;
		constexpr float kEpsilon    = 0.0005f;
		constexpr int kMaxStack     = 16;
	}

	EnvelopeEngine::EnvelopeEngine()
	{
		Reset();
	}

	void EnvelopeEngine::Trigger( int lane, float velocity, float decaySeconds )
	{
		if( lane < 0 || lane >= kLanes )
			return;

		Lane& l = lanes_[ lane ];

		velocity = std::clamp( velocity, 0.0f, 1.0f );
		l.decayTau = std::max( decaySeconds / kDecayShape, kMinTau );

		// Never pull an envelope downwards on retrigger, only raise it. This keeps
		// rapid note repeats from stuttering the value the host is reading.
		if( velocity > l.value )
			l.value = velocity;

		if( l.activeNotes < kMaxStack )
			++l.activeNotes;
	}

	void EnvelopeEngine::Release( int lane )
	{
		if( lane < 0 || lane >= kLanes )
			return;

		Lane& l = lanes_[ lane ];
		if( l.activeNotes > 0 )
			--l.activeNotes;
	}

	void EnvelopeEngine::Advance( float deltaSeconds )
	{
		if( deltaSeconds <= 0.0f )
			return;

		for( Lane& l : lanes_ )
		{
			if( l.value <= 0.0f )
				continue;

			// One time constant governs the whole tail. Releasing a note must not
			// short-circuit the decay the user dialled in, otherwise a 1 s decay
			// collapses the moment the gate closes.
			l.value *= std::exp( -deltaSeconds / l.decayTau );

			if( l.value < kEpsilon )
				l.value = 0.0f;
		}
	}

	void EnvelopeEngine::Reset()
	{
		for( Lane& l : lanes_ )
		{
			l.value       = 0.0f;
			l.decayTau    = 0.2f;
			l.activeNotes = 0;
		}
	}

	float EnvelopeEngine::Value( int lane ) const
	{
		if( lane < 0 || lane >= kLanes )
			return 0.0f;

		return lanes_[ lane ].value;
	}

	bool EnvelopeEngine::IsActive( int lane ) const
	{
		if( lane < 0 || lane >= kLanes )
			return false;

		return lanes_[ lane ].activeNotes > 0;
	}
}
