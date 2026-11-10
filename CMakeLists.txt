cmake_minimum_required(VERSION 3.20)
project(MidiRoll VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
	message(FATAL_ERROR "Midi Roll targets MSVC on Windows (Resolume loads 64-bit DLLs).")
endif()

set(FFGL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ffgl")
set(GLEW_ROOT "${FFGL_ROOT}/deps/glew-2.1.0")

# The vendored FFGL SDK links GLEW statically. We declare the imported target
# ourselves so the build never depends on a system-wide GLEW install.
add_library(glew_static STATIC IMPORTED)
set_target_properties(glew_static PROPERTIES
	IMPORTED_LOCATION "${GLEW_ROOT}/lib/Release/x64/glew32s.lib"
	INTERFACE_INCLUDE_DIRECTORIES "${GLEW_ROOT}/include"
	INTERFACE_COMPILE_DEFINITIONS "GLEW_STATIC")

add_library(MidiRoll SHARED
	src/PluginMain.cpp
	src/MidiRollPlugin.cpp
	src/EnvelopeEngine.cpp
	src/MidiOut.cpp
	src/BridgeServer.cpp
	src/BrowserLauncher.cpp
	"${FFGL_ROOT}/source/lib/FFGLSDK.cpp"
)

target_include_directories(MidiRoll PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${FFGL_ROOT}/source/lib")

target_link_libraries(MidiRoll PRIVATE glew_static opengl32 winmm ws2_32 shell32 ole32)

# The vendored GLEW static lib was built against the static CRT. We build with
# the DLL CRT (required for host plugins), so drop the static CRT default lib
# and let GLEW's symbols resolve against the shared one.
if(MSVC)
	target_link_options(MidiRoll PRIVATE "/NODEFAULTLIB:LIBCMT" "/NODEFAULTLIB:LIBCMTD")
endif()

target_compile_definitions(MidiRoll PRIVATE
	GLEW_STATIC
	NOMINMAX
	WIN32_LEAN_AND_MEAN
	_CRT_SECURE_NO_WARNINGS)

set_target_properties(MidiRoll PROPERTIES
	OUTPUT_NAME "MidiRoll"
	PREFIX ""
	SUFFIX ".dll")

# Metadata probe: loads the built DLL and dumps every registered parameter.
# Handy as a smoke test when Resolume is not running.
add_executable(MidiRollProbe tools/probe.cpp)
target_include_directories(MidiRollProbe PRIVATE "${FFGL_ROOT}/source/lib")
target_link_libraries(MidiRollProbe PRIVATE glew_static)
target_compile_definitions(MidiRollProbe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN GLEW_STATIC)
add_dependencies(MidiRollProbe MidiRoll)

# Full harness: hidden GL context + real plugin instance + MIDI capture.
add_executable(MidiRollHarness tools/harness.cpp)
target_include_directories(MidiRollHarness PRIVATE "${FFGL_ROOT}/source/lib")
target_link_libraries(MidiRollHarness PRIVATE glew_static opengl32 winmm)
target_compile_definitions(MidiRollHarness PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN GLEW_STATIC)
target_link_options(MidiRollHarness PRIVATE "/NODEFAULTLIB:LIBCMT" "/NODEFAULTLIB:LIBCMTD")
add_dependencies(MidiRollHarness MidiRoll)

# Post-build: drop the DLL straight into Resolume's user plugin folder so a
# rebuild is immediately testable. Set MIDIROLL_SKIP_INSTALL to disable.
if(NOT DEFINED MIDIROLL_SKIP_INSTALL)
	# Resolume uses the shell's Documents folder, which is commonly redirected to
	# OneDrive. build.ps1 passes the authoritative path; this is the fallback.
	if(DEFINED ENV{OneDrive} AND EXISTS "$ENV{OneDrive}/Documents")
		set(_midiroll_docs "$ENV{OneDrive}/Documents")
	else()
		set(_midiroll_docs "$ENV{USERPROFILE}/Documents")
	endif()

	set(MIDIROLL_INSTALL_DIR "${_midiroll_docs}/Resolume Arena/Extra Effects" CACHE PATH "Resolume extra effects folder")
	add_custom_command(TARGET MidiRoll POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${MIDIROLL_INSTALL_DIR}"
		# Resolume keeps the DLL mapped while it runs, so the copy can fail. A
		# build must never break because the host is open: warn and move on.
		COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
			"try { Copy-Item -LiteralPath '$<TARGET_FILE:MidiRoll>' -Destination '${MIDIROLL_INSTALL_DIR}/MidiRoll.dll' -Force -ErrorAction Stop; Write-Host 'MidiRoll.dll installed' } catch { Write-Host 'MidiRoll.dll is locked (Resolume running) - install skipped' }"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/gui"
			"${MIDIROLL_INSTALL_DIR}/gui"
		COMMENT "Installing MidiRoll.dll + gui to ${MIDIROLL_INSTALL_DIR}")
endif()
