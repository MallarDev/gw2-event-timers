# Event-Timers

## Open a waypoint from the timeline

In Settings, choose Copy waypoint or Open map for a timeline left-click.
With the optional Pie UI addon loaded, the Open map choice on an event with a waypoint
opens the game's map at that waypoint. Select the waypoint and confirm travel
in GW2. Without Pie UI, left-click still copies the link as before; right-click
offers an explicit copy action. This does not travel automatically.

The GitHub Actions `Build Event Timers DLL` workflow runs Windows tests and
produces `EventTimers.dll` under its downloadable build artifact. Put that DLL
directly in the GW2 `addons` directory and unload the original Event Timers
first (both builds use the same addon signature).

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
