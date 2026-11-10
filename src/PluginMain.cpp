#include "FFGLSDK.h"

#include "MidiRollPlugin.h"

static CFFGLPluginInfo PluginInfo(
	PluginFactory< MidiRollPlugin >,   // Create method
	"MR01",                            // Plugin unique ID (4 chars)
	"Midi Roll",                       // Plugin name
	2,                                 // API major version
	1,                                 // API minor version
	0,                                 // Plugin major version
	1,                                 // Plugin minor version
	FF_EFFECT,                         // Plugin type
	"16 step MIDI sequencer with envelope outputs and a browser piano roll.", // Description
	"Resolume Midi Roll"               // About
);
