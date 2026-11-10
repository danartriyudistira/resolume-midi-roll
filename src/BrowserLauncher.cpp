#include "BrowserLauncher.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <iterator>
#include <vector>

namespace midiroll
{
	namespace browser
	{
		namespace
		{
			bool FileExists( const std::wstring& path )
			{
				const DWORD attributes = GetFileAttributesW( path.c_str() );
				return attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_DIRECTORY ) == 0;
			}

			std::wstring Join( const std::wstring& a, const std::wstring& b )
			{
				if( a.empty() )
					return b;

				if( a.back() == L'\\' || a.back() == L'/' )
					return a + b;

				return a + L"\\" + b;
			}

			std::wstring UserDocuments()
			{
				// The shell's Documents folder is the only reliable answer: it may
				// be redirected to OneDrive, in which case USERPROFILE\Documents is
				// a different (and wrong) directory.
				PWSTR known = nullptr;
				if( SUCCEEDED( SHGetKnownFolderPath( FOLDERID_Documents, 0, nullptr, &known ) ) && known != nullptr )
				{
					std::wstring result( known );
					CoTaskMemFree( known );
					return result;
				}

				wchar_t buffer[ MAX_PATH * 2 ] = {};
				const DWORD length = GetEnvironmentVariableW( L"USERPROFILE", buffer, static_cast< DWORD >( std::size( buffer ) ) );
				if( length == 0 || length >= std::size( buffer ) )
					return std::wstring();

				return Join( std::wstring( buffer, length ), L"Documents" );
			}

			std::string Narrow( const std::wstring& wide )
			{
				if( wide.empty() )
					return std::string();

				const int size = WideCharToMultiByte( CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr );
				if( size <= 1 )
					return std::string();

				std::string out( static_cast< size_t >( size - 1 ), '\0' );
				WideCharToMultiByte( CP_UTF8, 0, wide.c_str(), -1, out.data(), size, nullptr, nullptr );
				return out;
			}
		}

		std::wstring PluginDirectory()
		{
			HMODULE module = nullptr;
			if( !GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast< LPCWSTR >( &PluginDirectory ),
					&module ) )
			{
				return std::wstring();
			}

			wchar_t path[ MAX_PATH * 2 ] = {};
			const DWORD length = GetModuleFileNameW( module, path, static_cast< DWORD >( std::size( path ) ) );
			if( length == 0 || length >= std::size( path ) )
				return std::wstring();

			std::wstring full( path, length );
			const size_t slash = full.find_last_of( L"\\/" );
			if( slash == std::wstring::npos )
				return std::wstring();

			return full.substr( 0, slash );
		}

		std::wstring GuiPath()
		{
			std::vector< std::wstring > candidates;

			const std::wstring pluginDir = PluginDirectory();
			if( !pluginDir.empty() )
				candidates.push_back( Join( pluginDir, L"gui\\index.html" ) );

			const std::wstring documents = UserDocuments();
			if( !documents.empty() )
			{
				candidates.push_back( Join( documents, L"Resolume Arena\\Extra Effects\\gui\\index.html" ) );
				candidates.push_back( Join( documents, L"Resolume\\Extra Effects\\gui\\index.html" ) );
			}

			for( const std::wstring& candidate : candidates )
			{
				if( FileExists( candidate ) )
					return candidate;
			}

			// Nothing found: hand back the preferred location so the error message
			// tells the user exactly where the page is expected to live.
			return candidates.empty() ? std::wstring() : candidates.front();
		}

		std::wstring GuiDirectory()
		{
			const std::wstring path = GuiPath();
			if( path.empty() )
				return std::wstring();

			const size_t slash = path.find_last_of( L"\\/" );
			if( slash == std::wstring::npos )
				return std::wstring();

			return path.substr( 0, slash );
		}

		bool OpenGui( std::string* errorOut )
		{
			const std::wstring path = GuiPath();

			if( path.empty() || !FileExists( path ) )
			{
				if( errorOut )
					*errorOut = "Piano roll not found. Expected at: " + Narrow( path );

				return false;
			}

			return OpenUrl( path, errorOut );
		}

		bool OpenUrl( const std::wstring& url, std::string* errorOut )
		{
			const HINSTANCE result = ShellExecuteW(
				nullptr,
				L"open",
				url.c_str(),
				nullptr,
				nullptr,
				SW_SHOWNORMAL );

			// ShellExecute returns a value > 32 on success; anything else is an
			// error code from the shell.
			if( reinterpret_cast< INT_PTR >( result ) <= 32 )
			{
				if( errorOut )
					*errorOut = "ShellExecute failed for: " + Narrow( url );

				return false;
			}

			return true;
		}
	}
}
