#include "MidiOut.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>

namespace midiroll
{
	namespace
	{
		std::string Narrow( const wchar_t* wide )
		{
			if( wide == nullptr || wide[ 0 ] == L'\0' )
				return std::string();

			const int size = WideCharToMultiByte( CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr );
			if( size <= 1 )
				return std::string();

			std::string out( static_cast< size_t >( size - 1 ), '\0' );
			WideCharToMultiByte( CP_UTF8, 0, wide, -1, out.data(), size, nullptr, nullptr );
			return out;
		}
	}

	MidiOut::MidiOut()
	{
		RefreshDevices();
	}

	MidiOut::~MidiOut()
	{
		AllNotesOff();
		Close();
	}

	void MidiOut::RefreshDevices()
	{
		devices_.clear();

		const UINT count = midiOutGetNumDevs();
		for( UINT i = 0; i < count; ++i )
		{
			MIDIOUTCAPSW caps = {};
			if( midiOutGetDevCapsW( i, &caps, sizeof( caps ) ) != MMSYSERR_NOERROR )
				continue;

			Device device;
			device.name = Narrow( caps.szPname );
			if( device.name.empty() )
				device.name = "Midi Device " + std::to_string( i + 1 );

			devices_.push_back( device );
		}
	}

	int MidiOut::DeviceCount() const
	{
		return static_cast< int >( devices_.size() );
	}

	const std::string& MidiOut::DeviceName( int index ) const
	{
		static const std::string empty;
		if( index < 0 || index >= DeviceCount() )
			return empty;

		return devices_[ static_cast< size_t >( index ) ].name;
	}

	bool MidiOut::Open( int deviceIndex )
	{
		Close();

		if( deviceIndex < 0 || deviceIndex >= DeviceCount() )
			return false;

		HMIDIOUT handle = nullptr;
		const MMRESULT result = midiOutOpen( &handle, static_cast< UINT >( deviceIndex ), 0, 0, CALLBACK_NULL );
		if( result != MMSYSERR_NOERROR || handle == nullptr )
			return false;

		handle_    = handle;
		openIndex_ = deviceIndex;
		return true;
	}

	void MidiOut::Close()
	{
		if( handle_ == nullptr )
			return;

		AllNotesOff();
		midiOutReset( static_cast< HMIDIOUT >( handle_ ) );
		midiOutClose( static_cast< HMIDIOUT >( handle_ ) );

		handle_    = nullptr;
		openIndex_ = -1;
	}

	bool MidiOut::IsOpen() const
	{
		return handle_ != nullptr;
	}

	int MidiOut::OpenIndex() const
	{
		return openIndex_;
	}

	void MidiOut::SendShort( int status, int data1, int data2 )
	{
		if( handle_ == nullptr )
			return;

		const DWORD message = static_cast< DWORD >( status & 0xFF )
		                    | ( static_cast< DWORD >( data1 & 0x7F ) << 8 )
		                    | ( static_cast< DWORD >( data2 & 0x7F ) << 16 );

		midiOutShortMsg( static_cast< HMIDIOUT >( handle_ ), message );
	}

	void MidiOut::NoteOn( int channel, int note, int velocity )
	{
		if( note < 0 || note > 127 )
			return;

		channel  = std::clamp( channel, 0, 15 );
		velocity = std::clamp( velocity, 1, 127 );

		SendShort( 0x90 | channel, note, velocity );
		activeNotes_[ channel ][ note ] = true;
	}

	void MidiOut::NoteOff( int channel, int note )
	{
		if( note < 0 || note > 127 )
			return;

		channel = std::clamp( channel, 0, 15 );

		SendShort( 0x80 | channel, note, 0 );
		activeNotes_[ channel ][ note ] = false;
	}

	void MidiOut::ControlChange( int channel, int controller, int value )
	{
		channel    = std::clamp( channel, 0, 15 );
		controller = std::clamp( controller, 0, 127 );
		value      = std::clamp( value, 0, 127 );

		SendShort( 0xB0 | channel, controller, value );
	}

	void MidiOut::AllNotesOff()
	{
		if( handle_ == nullptr )
			return;

		for( int channel = 0; channel < 16; ++channel )
		{
			for( int note = 0; note < 128; ++note )
			{
				if( activeNotes_[ channel ][ note ] )
					SendShort( 0x80 | channel, note, 0 );

				activeNotes_[ channel ][ note ] = false;
			}

			// Belt and braces: the channel-wide panic message.
			SendShort( 0xB0 | channel, 123, 0 );
		}
	}
}
