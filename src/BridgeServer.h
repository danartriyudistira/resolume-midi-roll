#pragma once

#include <array>
#include <mutex>
#include <string>
#include <thread>

namespace midiroll
{
	// Loopback bridge between the plugin and the piano roll page.
	//
	// The plugin owns a tiny HTTP listener on 127.0.0.1 so the browser page can
	// be served from the same origin. That buys three things at once: no CORS,
	// a secure context for Web MIDI, and a place to POST pattern edits back.
	//
	// One listener per process. Instances acquire and release it; the first
	// acquire starts the thread, the last release stops it.
	class BridgeServer
	{
	public:
		static constexpr int kSlots          = 4;
		static constexpr unsigned short kPort = 47823;

		static BridgeServer& Instance();

		void Acquire();
		void Release();

		bool IsListening() const;
		std::string Origin() const;

		// Plugin -> GUI. Publishes the pattern the plugin currently holds so a
		// freshly opened page hydrates from the source of truth.
		void Publish( int slot, const std::string& pattern );

		// GUI -> Plugin. Returns true once per accepted change.
		bool Consume( int slot, std::string& pattern );

		// Current contents of a slot without touching its dirty flag.
		std::string Peek( int slot ) const;

		// Note value of one step, so the editor can label the grid.
		void SetDivision( const std::string& label );

		// Host transport, for the playhead in the editor.
		void SetTransport( float phase, bool playing, float bpm, int step, int bars, int loopStart, int loopEnd );

		static std::string Sanitize( const std::string& candidate );

	private:
		BridgeServer();
		~BridgeServer();

		BridgeServer( const BridgeServer& )            = delete;
		BridgeServer& operator=( const BridgeServer& ) = delete;

		void Start();
		void Stop();
		void Run();
		void HandleClient( unsigned long long clientHandle );
		std::string Route( const std::string& method, const std::string& target, const std::string& body );

		std::array< std::string, kSlots > slots_;
		std::array< bool, kSlots > dirty_ = {};
		std::string division_             = "1/16";
		float phase_                      = 0.0f;
		bool playing_                     = false;
		float bpm_                        = 120.0f;
		int step_                         = 0;
		int bars_                         = 1;
		int loopStart_                    = 0;
		int loopEnd_                      = 32;
		mutable std::mutex mutex_;

		std::thread thread_;
		bool running_        = false;
		bool listening_      = false;
		int refCount_        = 0;
		unsigned long long listenSocket_ = 0;
	};
}
