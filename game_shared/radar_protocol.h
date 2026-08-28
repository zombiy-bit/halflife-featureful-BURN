#pragma once

// Shared client/server protocol for the radar HUD.
// Keep this header free of server/client-only includes.

enum RadarMessageOpcode
{
	RADAR_MSG_SNAPSHOT_BEGIN = 1,
	RADAR_MSG_NPC_INFO = 2,
	RADAR_MSG_NPC_DAMAGE_REVEAL = 3,
	RADAR_MSG_NPC_REMOVE = 4,
	RADAR_MSG_HINT_SYNC = 5,
	RADAR_MSG_HINT_EVENT = 6,
	RADAR_MSG_SNAPSHOT_END = 7
};

enum RadarMarkerKind
{
	RADAR_MARKER_HOSTILE = 0,
	RADAR_MARKER_NEUTRAL = 1,
	RADAR_MARKER_HINT_MAIN = 2,
	RADAR_MARKER_HINT_SECONDARY = 3
};

enum RadarSnapshotFlags
{
	RADAR_SNAPSHOT_SILENT = 1
};
