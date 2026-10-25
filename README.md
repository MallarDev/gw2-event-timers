# Event-Timers

## Event click action — version 1.2.0

Settings → Other → **Left-click on an event** has three choices:

- **Teleport directly**: requests native waypoint travel without opening the map or displaying its confirmation. Uses normal waypoint costs and game/server travel rules. Independent of Pie UI.
- **Open map at waypoint (requires Pie UI)**: opens the game's map at that waypoint through the optional Pie UI addon. Travel is confirmed in GW2.
- **Copy waypoint**: copies the link, including the event name when that setting is enabled.

The choice applies to timeline events, actual event notification toasts and the Upcoming Events panel. Existing saved choices are preserved; the default remains Open map. A toast preview in settings only demonstrates copying and cannot initiate travel. If the selected action is unavailable, an alert explains why; it does not silently perform a different action.

### Install

1. Close GW2 and remove **WaypointProbe.dll**. The diagnostic addon must be removed because its hooks modify the game function that the integrated build checks.
2. Replace the existing Event Timers DLL with **EventTimers.dll** in GW2's `addons` directory. Keep only one Event Timers build (the addon signature is unchanged).
3. Open GW2, then select the action in Settings. Direct travel is ready without diagnostic captures or manual A/B warm-up.

Direct travel uses the native request routine validated with Waypoint Probe v0.8 and dispatches it through the game window's thread. The native adapter does not install game hooks or collect message contents. It validates the exact executable version and function bytes, a connected gameplay state, target window/process, queued request lifetime and connection identity. Pending requests expire after 5 seconds; unloading cancels pending work. Repeat clicks are limited to one native invocation per 2 seconds. Wait for loading to finish before travelling again.

### Compatibility

The direct adapter currently supports the supplied x64 GW2 executable with PE timestamp `0x6ad7acf4` and image size `0x02c49000` (SHA256 `99342078ef57ca5653046392c2cf8aa197f1badbbe2c68297ca97e0d925d8621`). A GW2 update or another addon changing the native function disables direct travel. Opening the map and copying remain separate available choices. The routine was game-tested with A `[&BDAEAAA=]` and B `[&BA4EAAA=]`; the integrated event-click flow and other destinations still need game testing.

The Windows build runs unit tests for existing addon behavior, click-action config persistence, strict chat-link decoding, PE/signature checks, request validation and single-use dispatch tickets, then produces EventTimers.dll. The previous C++ diagnostic also tested the cross-thread Windows queue, and user testing confirmed direct A/B travel under Wine/Nexus. This package contains the integrated DLL and instructions, not the diagnostic addon.

## Test build 1.1.6

- Saves configuration every two seconds with temporary-file and backup recovery.
- Preserves in-session changes when the online timetable updates.
- Dynamically applies the quick-access visibility setting.
- Adds Dragon Bash Hologram Stampede and Festival of the Four Winds timers;
  seasonal tracks start hidden and can be enabled in settings.
- Fixes wrapped repeating schedules and keeps only the nearest occurrence of
  each event in the Upcoming Events panel.
- Adds optional GW2 API daily-completion tracking. An API key with `account`
  and `progression` permissions can strike completed mapped events in the
  upcoming and tracked-event tables.
