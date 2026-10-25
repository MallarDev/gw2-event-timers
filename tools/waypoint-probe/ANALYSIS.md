# UI confirmation investigation

Input: supplied Gw2-64.exe, SHA256 99342078ef57ca5653046392c2cf8aa197f1badbbe2c68297ca97e0d925d8621. Addresses below are RVAs in that exact image. Interpretations require runtime validation.

- Compass CpResShrine checks WORLD_MAP_MODEL_TYPE_RES_SHRINE (enum 10) at RVA 0x470cce. Code at 0x470afa calls validation 0x4710b0, obtains the POI definition using a model virtual call, and delegates to DialogManager slot 0x490.
- DialogManager proxy singleton is returned by 0x2e6970. Its constructor 0x2e60d0 installs vtable VA 0x14193e3d8. Slot 0x490 is proxy 0x2ed530, forwarding to the registered command object.
- Constructor 0x2b5c80 registers this command through DialogManager slot 0x4e0 (setter 0x2ead60). Its vtable VA 0x141937100 slot 0x490 resolves to 0x2bb920.
- Command 0x2bb920 constructs a dialog using callback 0x4ef910. This is DlgMarkerConfirmation.cpp, identified by assertion source references; it is relevant to res-shrine confirmation, contrary to the earlier unverified hypothesis that it was only a guild marker dialog.
- Callback data vtable and event dispatch lead to handler 0x4efe30. It branches on event DWORD +4 and subtype DWORD +8. Control 0/subtype 3 closes; control 1/subtype 3 executes the affirmative branch; control 6 updates UI state.
- Setup 0x4f0160 binds the POI definition argument RDX to dialog+0x68. Definition+0x28 is the numeric ID passed to the player action later. Probe observes this setup and the handler above.
- Affirmative handler uses the POI at dialog+0x68, obtains gameplay player through context+0x98 virtual slot 0x98, and dispatches virtual slot 0x658 with EDX=POI ID or slot 0x660 with EDX=POI ID plus selected route UUID. Relevant call sites: 0x4eff65, 0x4eff94, 0x4effa6. It then closes the dialog.
- Runtime target of these player virtual slots has not yet been established or invoked by the addon. No functional direct travel is claimed.

Runtime discrimination: open A/cancel, open A/accept and arrive, open B/accept and arrive. Check IDs 1072/1038 and automatic button events. No packet capture or timing marks are needed. This establishes whether the identified UI path and ID field apply to the user's actual travel, before attempting to expose or invoke the action.

## v0.6 runtime evidence and native sender resolution

`waypoint_ui_v6_1.tsv`: both hooks active; 8 records; no drops; all data reads valid. Observed A(1072) cancel, A accept and B(1038) accept. DESTINATION caller RVA 0x470b5e, button handler caller RVA 0x4efcf5, thread 480. Handler return and entry fall in the same millisecond; these logs alone do not verify actual arrival.

- Game TLS context: 0x9b1fb0 returns the context. Context slot 0x98 is index 0x13 in the registry initialized by 0x9b1c00. Registration at 0x1242c60 uses factory 0x1242c90 -> 0x11b4550 -> constructor 0x11b1de0. ChCliContext vtable VA 0x14215dfc8 slot 0x98 -> 0x11b51b0.
- Getter 0x11b51b0 forwards through the current ChCliPlayer at context+0xa0, slot 0x80. ChCliPlayer vtable VA 0x14215e9d8 slot 0x80 -> 0x11bada0, returning its character at +0x20 when the gameplay flag is set.
- ChCliCharacter constructor installs vtable VA 0x142160be0. Slots 0x658 and 0x660 resolve to 0x11e27f0 and 0x11e2830. Both assert a nonzero POI ID and tail-call native message builders, without retaining the character receiver.
- Simple builder RVA 0x1263c70 takes uint32 ID in ECX, creates the normal 6-byte request (type 0xd0 + ID), obtains the client's global connection via getter 0x23f640, and calls normal send dispatcher 0xff0110. Source assertions identify ChCliMsg.cpp and `pointOfInterestDataId != 0`.
- Routed builder RVA 0x1263cc0 takes ID in ECX and a 16-byte route reference in RDX. It builds type 0xd1 + ID + route, 22 bytes, through the same dispatcher. The probe does not copy or log route contents.
- Getter 0x23f640 simply returns global pointer at image RVA 0x266c1d0. Dispatcher 0xff0110 proceeds only when connection DWORD +0x108 equals 3 and flag byte +0 has bit 4 set. v0.7 repeats these scalar checks before enabling a user direct call; no memory dump is recorded.

v0.7 observes native builders to validate the final ID and correlate the sender with confirmation. An optional direct user button is available only for each allowlisted ID after a matching manual simple call in this capture, same thread and connection identity, active connection, all hooks healthy, active capture, and 5-second spacing. Routed calls cannot unlock a direct button. This allows a single runtime session to validate the actual sender and, where prerequisites match, test direct travel. No automatic calls occur upon observation.
