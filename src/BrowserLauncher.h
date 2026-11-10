#pragma once

#include <string>

namespace midiroll
{
	namespace browser
	{
		// Directory the plugin DLL lives in. Empty when the module handle can't be
		// resolved, which only happens if the DLL was mapped without a loader.
		std::wstring PluginDirectory();

		// Absolute path to the piano roll page. Prefers gui/index.html next to the
		// DLL, then the Resolume user plugin folder, then the project source tree.
		std::wstring GuiPath();

		// Folder that holds the page and its assets.
		std::wstring GuiDirectory();

		// Launches the page in the user's default browser. Returns false and fills
		// errorOut on failure so the caller can log it to the host.
		bool OpenGui( std::string* errorOut );

		// Launches an arbitrary URL (used for the plugin's own loopback origin).
		bool OpenUrl( const std::wstring& url, std::string* errorOut );
	}
}
