#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <thread>

namespace midiroll
{
	// Drives Resolume's composition dashboard dials through the local REST API.
	//
	// The REST API is already used by the web editor, so no OSC input or MIDI
	// learn is required. SetDial is non-blocking: it stores the latest value and
	// a background thread coalesces the writes.
	class DashboardRest
	{
	public:
		DashboardRest();
		~DashboardRest();

		void Start();
		void Stop();

		// dial 0..7, value 0..1.
		void SetDial( int dial, float value );

	private:
		void Run();
		bool RefreshIds();
		bool PutValue( int dial, float value );
		bool EnsureConnection();
		void CloseConnection();

		std::array< long long, 8 > ids_ = { -1, -1, -1, -1, -1, -1, -1, -1 };
		std::array< float, 8 > pending_ = {};
		std::array< float, 8 > sent_    = { -1, -1, -1, -1, -1, -1, -1, -1 };
		std::array< bool, 8 > dirty_    = {};
		std::array< unsigned long long, 8 > lastSent_ = {};

		std::mutex mutex_;
		std::thread thread_;
		std::atomic< bool > running_ = false;

		void* session_ = nullptr;
		void* connect_ = nullptr;
	};
}
