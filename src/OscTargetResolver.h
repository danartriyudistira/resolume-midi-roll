#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace midiroll
{
	// Resolves the selected clip/layer in Resolume and reports its grid position.
	//
	// Runs on a worker thread because the REST calls must never block the render
	// thread. The plugin consumes the result once and builds the OSC addresses.
	class OscTargetResolver
	{
	public:
		enum class Task
		{
			None,
			Clip,
			Layer,
		};

		struct Result
		{
			bool valid      = false;
			bool layerOnly  = false;
			int layer       = 0; // 1-based
			int column      = 0; // 1-based, 0 for layer-only
			std::string reason;
		};

		OscTargetResolver();
		~OscTargetResolver();

		void Start();
		void Stop();

		void RequestClip();
		void RequestLayer();

		// Returns true once when a result is available; clears the result.
		bool Consume( Result& result );

	private:
		void Run();
		bool ResolveClip( Result& result );
		bool ResolveLayer( Result& result );

		std::atomic< int > task_ = 0;
		std::atomic< bool > running_ = false;
		std::thread thread_;

		std::mutex mutex_;
		Result result_;
		bool hasResult_ = false;
	};
}
