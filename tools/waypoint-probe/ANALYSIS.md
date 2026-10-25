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
