#pragma once

#include <string>
#include <vector>

namespace midiroll
{
	// Thin wrapper around the Win32 multimedia MIDI output API.
	// One instance per plugin instance, so every Resolume clip can drive its own
	// hardware channel without stepping on another instance's notes.
	class MidiOut
	{
	public:
		MidiOut();
		~MidiOut();

		MidiOut( const MidiOut& )            = delete;
		MidiOut& operator=( const MidiOut& ) = delete;

		void RefreshDevices();

		int DeviceCount() const;
		const std::string& DeviceName( int index ) const;

		bool Open( int deviceIndex );
		void Close();

		bool IsOpen() const;
		int OpenIndex() const;

		void NoteOn( int channel, int note, int velocity );
		void NoteOff( int channel, int note );
		void ControlChange( int channel, int controller, int value );
		void AllNotesOff();

	private:
		void SendShort( int status, int data1, int data2 );

		struct Device
		{
			std::string name;
		};

		std::vector< Device > devices_;
		void* handle_          = nullptr;
		int openIndex_         = -1;
		bool activeNotes_[ 16 ][ 128 ] = {};
	};
}
