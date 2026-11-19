#pragma once

namespace midiroll
{
	// Four independent envelope lanes. Each lane integrates every note that maps
	// to it, so overlapping triggers never orphan a decaying value.
	class EnvelopeEngine
	{
	public:
		static constexpr int kLanes = 4;

		EnvelopeEngine();

		void Trigger( int lane, float velocity, float decaySeconds );
		void Advance( float deltaSeconds );
		void Reset();

		float Value( int lane ) const;

	private:
		struct Lane
		{
			float value     = 0.0f;
			float decayTau  = 0.2f;
		};

		Lane lanes_[ kLanes ];
	};
}
