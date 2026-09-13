# ArcDroid Product Requirements Document

**Status:** Draft — awaiting Founder approval  
**Owner:** Founder  
**Product:** ArcDroid  
**Primary target:** RetroArch/libretro on Android  
**Document role:** Product law after explicit Founder approval. This document defines **what ArcDroid must do**; it intentionally does not decide the implementation architecture or emulation engine except where product behavior requires a constraint.

---

## 1. Product definition

ArcDroid is a **RetroArch/libretro core for playing Acorn Archimedes games directly from game media with a console-like, game-first experience**.

ArcDroid is not intended to reproduce the user experience of operating an Archimedes computer for its own sake. The emulated machine, firmware generation, operating-system version, hardware profile, disk controller, joystick interface, boot sequence, and other machine details are implementation mechanisms that ArcDroid should manage automatically whenever possible.

The intended normal user experience is:

> **Select game → Play.**

A player launching an Archimedes title from RetroArch, ES-DE, or another libretro frontend should not need to understand which Archimedes model, ROM set, RISC OS or Arthur version, drive configuration, joystick standard, or boot command the game historically required.

ArcDroid may internally emulate those details with high fidelity. It must not make them routine user chores.

---

## 2. North-star experience

### 2.1 Primary flow

The default ArcDroid flow is:

1. The user selects a supported Archimedes game image or game package in a libretro frontend.
2. ArcDroid inspects the content and any available compatibility metadata.
3. ArcDroid selects an appropriate machine and firmware profile automatically.
4. ArcDroid mounts the required media and performs any safe, deterministic boot automation needed to reach the game.
5. The game becomes playable with RetroArch-native controls and frontend behavior.

The user should not normally be exposed to RISC OS administration, machine setup, drive configuration, or emulator-specific desktop UI.

### 2.2 Product test

When evaluating any feature, architecture, or upstream dependency, the primary product question is:

> **Does this make it easier to choose an Archimedes game and play it reliably without having to operate an emulated Archimedes computer?**

If a technically authentic behavior creates avoidable friction between game selection and gameplay, ArcDroid should hide, automate, or simplify it unless doing so would materially reduce compatibility or correctness.

---

## 3. Product principles

### P-1 — Game-first, not machine-first

Games and game media are the primary launch objects. Machine models and operating systems are implementation details except when exposed as advanced compatibility overrides.

### P-2 — Loose-content friendly

ArcDroid must launch supported game media directly. It must not require content to appear in a curated ROM set, software list, database whitelist, or filename convention merely to be attempted.

### P-3 — Automation before configuration

The core should automatically choose compatible machine, firmware, hardware, input, and boot settings whenever reliable rules or metadata make that possible.

### P-4 — Compatibility knowledge is advisory, never permission

An internal compatibility database may improve launch behavior, but unknown content must still be attempted using sensible fallback behavior. ArcDroid must not recreate a closed software-list model.

### P-5 — RetroArch-native UX

Where libretro provides appropriate interfaces for content loading, controller input, keyboard/mouse input, disk control, core options, save paths, messages, remaps, and related frontend behavior, ArcDroid should integrate through those interfaces rather than invent a parallel emulator UI.

### P-6 — Sophisticated internally, simple externally

ArcDroid may need substantial machine emulation complexity to reach compatibility parity with mature Archimedes emulators. That complexity belongs inside the core and its compatibility logic, not in the normal player workflow.

### P-7 — Correctness over cosmetic authenticity

The project values accurate game behavior, timing, input, audio, video, and media handling. It does not require reproducing historical setup inconvenience or an emulator application's desktop metaphors.

### P-8 — Advanced controls exist to solve problems, not define the product

Manual machine, firmware, CPU, RAM, joystick, media, or boot overrides may be provided where useful, but `Auto` should be the normal default and the ordinary launch path should not depend on manual tuning.

---

## 4. Target users

### 4.1 Primary user

A retro-gaming user who:

- uses RetroArch, ES-DE, or another libretro frontend;
- owns or lawfully possesses the required game media and firmware;
- wants to launch Archimedes games like games on any other emulated platform;
- does not want to operate a historically accurate Archimedes desktop merely to start a title;
- expects controller-friendly behavior, clean disk swapping, sensible errors, and per-game persistence.

### 4.2 Secondary user

An enthusiast or preservation-focused user who understands Archimedes hardware and wants manual overrides for troubleshooting or unusual titles, but still benefits from a game-first frontend.

### 4.3 Explicitly non-primary user

A user seeking a full historical workstation environment for productivity applications, hardware experimentation, networking, development, or peripheral simulation unrelated to playing games. ArcDroid may incidentally support some of this if inherited from the chosen engine, but it is not the product driver.

---

## 5. Product goals

### G-1 — Deliver a first-class Archimedes gaming core for RetroArch

ArcDroid must be usable as a libretro core and provide a normal RetroArch-style content launch experience.

### G-2 — Remove machine-operation friction

A user should not normally need to choose an Archimedes model, OS version, disk controller, joystick interface, or boot procedure before playing a game.

### G-3 — Reach Arculator-class game compatibility

The long-term compatibility target is **game-compatibility parity with Arculator**: if Arculator can successfully run an Archimedes game using a valid supported configuration, ArcDroid should ultimately be capable of running that game as well, subject to legal, licensing, and technically unavoidable constraints.

This is a game-compatibility target, not a requirement to reproduce every non-game peripheral or workstation feature that Arculator may emulate.

### G-4 — Support the firmware breadth required for that compatibility

ArcDroid must be capable of recognizing and using the Arthur/RISC OS and related firmware variants needed for game compatibility across the supported Archimedes family. The user should supply firmware once; ArcDroid should select among available valid firmware automatically.

### G-5 — Make unknown content launchable

The core should have safe fallback configurations so titles not present in the compatibility database can still be attempted.

### G-6 — Preserve a path to future compatibility growth

The product must not be architected around a tiny fixed subset of games, one ROM set, one firmware generation, or one narrowly curated media type in a way that prevents later expansion toward the full Arculator game-compatibility envelope.

---

## 6. Non-goals

Unless the Founder explicitly changes scope, ArcDroid is **not** required to:

1. Recreate Arculator's desktop UI, configuration workflow, or machine-management UX.
2. Recreate MAME's software-list or ROM-set dependency model.
3. Emulate every historical Archimedes peripheral merely for completeness.
4. Ship copyrighted commercial games, disk images, manuals, RISC OS ROMs, Arthur ROMs, or other firmware without verified redistribution rights.
5. Become a general-purpose RISC OS productivity environment.
6. Require users to understand Archimedes hardware terminology before playing games.
7. Choose a specific emulation engine in this PRD.
8. Preserve an upstream emulator's UI architecture if it conflicts with ArcDroid's game-first product requirements.
9. Treat exact historical inconvenience as a compatibility feature.
10. Depend on a remote service, online database, or account to launch locally owned content.

---

## 7. Platform and frontend requirements

### FR-1 — Libretro core

ArcDroid shall be delivered as a libretro core suitable for use with RetroArch.

### FR-2 — Android is the primary product target

Android is the primary user platform. Architecture, build choices, dependencies, performance decisions, storage behavior, and input handling must be evaluated with Android as a first-class target rather than as an afterthought.

Host-platform builds may be used for development, testing, CI, and portability if useful, but they do not replace Android validation.

### FR-3 — Frontend independence within libretro expectations

ArcDroid should behave correctly with conforming libretro frontends where practical and must not rely on ES-DE-specific behavior to function. ES-DE integration is important to the intended user experience but should remain a launcher layer above the core.

### FR-4 — Direct content launch

A frontend must be able to launch a supported game by passing the selected media/content to the ArcDroid core. The user should not first have to launch an empty machine and then manually insert the game.

---

## 8. Content and media requirements

### FR-5 — Loose-file launch

ArcDroid must support direct launch of supported Archimedes game media stored as ordinary user files.

Recognition may use file extension, content inspection, hashes, metadata, or other deterministic methods, but filename conventions must not be required where the underlying media can otherwise be recognized.

### FR-6 — No software-list gate

A title must not be rejected solely because it is absent from a known-title database, ROM set, or software list.

### FR-7 — Unknown-content fallback

When content is not recognized as a known title, ArcDroid shall attempt a sensible generic configuration if the media format itself is supported.

The fallback may be conservative, but it must be a real attempt rather than a database-denial error.

### FR-8 — Multi-disk games

ArcDroid shall support multi-disk titles using libretro's appropriate disk/media-control mechanisms where technically applicable.

Normal user behavior should be expressed in game language such as eject/select/next disk rather than requiring the user to manage emulated floppy-controller details.

### FR-9 — Media-format roadmap

The long-term media-support target is every game-relevant media format and access mode required to reach the defined Arculator game-compatibility goal.

The architecture phase shall identify:

- which formats are required for the first usable release;
- which formats are required for broad Archimedes game compatibility;
- which formats are required for protected, unusual, or CD-based titles;
- whether any format support introduces licensing or dependency constraints.

The PRD intentionally does not freeze the exact v1 extension list before that investigation.

### FR-10 — Media fidelity

For titles whose behavior depends on disk layout, protection, timing, sector metadata, or other media characteristics, ArcDroid must preserve the information needed for correct emulation rather than normalizing everything to a lowest-common-denominator disk representation.

---

## 9. Firmware and operating-system requirements

### FR-11 — User-supplied firmware

ArcDroid shall support user-supplied Archimedes firmware/OS images through a deterministic RetroArch-compatible firmware location.

The exact directory layout is an architecture decision, but setup should be one-time and compatible with standard frontend expectations.

### FR-12 — Broad OS/ROM compatibility

ArcDroid shall be capable of using the game-relevant Arthur, RISC OS, and machine-specific firmware variants needed to reach the project's Arculator-class compatibility target.

The product requirement is **access to the required firmware universe**, not forcing every game through one preferred OS version.

### FR-13 — Automatic firmware selection

When the required or preferred firmware is present, ArcDroid should select it automatically according to the content's compatibility profile and machine requirements.

Manual firmware selection shall not be required for normal known-title launches.

### FR-14 — Firmware identification independent of friendly filenames

Where practical, ArcDroid should identify known firmware using stable content fingerprints and validated structure rather than relying only on a single exact filename.

Users should not lose compatibility merely because a legally obtained dump has a different filename.

### FR-15 — Firmware inventory behavior

ArcDroid should be able to determine which recognized firmware components are present and which are missing.

This inventory may be surfaced through core information, logs, frontend messages, or another RetroArch-appropriate mechanism defined by architecture.

### FR-16 — Missing firmware errors

If a title cannot run because required firmware is unavailable, ArcDroid shall fail cleanly and communicate a useful explanation identifying the missing requirement as specifically as reasonably possible.

It should not silently boot into an unrelated desktop, crash, hang indefinitely, or emit only an opaque generic failure when the missing firmware is known.

### FR-17 — Compatible fallback

If the preferred firmware is missing but a verified compatible alternative is available, ArcDroid may use that fallback automatically when doing so is known to preserve correct game behavior.

It must not guess across firmware generations in ways known to break titles merely to avoid an error.

### FR-18 — No unauthorized firmware distribution

ArcDroid releases shall not bundle copyrighted firmware or operating-system ROM images unless redistribution rights have been explicitly verified and accepted under project protocol.

---

## 10. Machine and hardware configuration requirements

### FR-19 — Automatic machine profile

For known titles, ArcDroid should automatically select a compatible emulated machine profile, including game-relevant CPU, memory, chipset/controller, and peripheral behavior required for successful launch.

### FR-20 — Auto is the normal configuration

Machine-related core options should default to automatic selection wherever reliable automation is possible.

### FR-21 — Manual compatibility overrides

ArcDroid may expose advanced overrides for machine model, CPU class, RAM, firmware, controller/joystick interface, media behavior, or other game-relevant hardware settings when they materially help troubleshooting or unsupported content.

These settings should be clearly secondary to automatic operation.

### FR-22 — Per-game persistence

Where RetroArch supports per-game core options, remaps, or related configuration, ArcDroid should work naturally with those mechanisms so a compatibility override or control adjustment can be fixed once and then reused automatically.

### FR-23 — Game-relevant hardware scope

ArcDroid must emulate or preserve access to any hardware capability required for games inside the compatibility target.

Hardware unrelated to playing games is not automatically required merely because a source emulator implements it.

---

## 11. Boot and launch automation

### FR-24 — Minimize visible operating-system interaction

For known titles, ArcDroid should automate deterministic steps between machine boot and game launch whenever technically safe and compatible.

Examples may include media insertion, boot configuration, command execution, application launch, or equivalent machine setup, but the architecture must determine the safest mechanism for each class of content.

### FR-25 — Do not fake compatibility through fragile title hacks

Boot automation may use title profiles, but the emulator itself must remain generally correct. The project must not substitute hard-coded game-specific patches for missing machine emulation when the title genuinely depends on hardware or OS behavior that should be emulated.

### FR-26 — Graceful fallback to manual interaction when necessary

If a game genuinely requires user interaction with an Archimedes environment that cannot safely be automated, ArcDroid may expose the required interaction rather than blocking the title.

Such cases should be treated as compatibility exceptions, not as the default product workflow.

### FR-27 — Optional advanced computer access

A general-purpose or "Computer Mode" may be considered in the future for troubleshooting, applications, or enthusiasts, but it is not required for the first product and must not complicate the default game-launch experience.

---

## 12. Input requirements

### FR-28 — Controller-first gaming

The RetroPad shall be the primary default gaming input abstraction for titles that can reasonably be mapped to a controller.

### FR-29 — Automatic game-oriented mappings

Where a title is known to use a specific Archimedes joystick interface or keyboard controls that map cleanly to a controller, ArcDroid should provide a sensible default mapping.

### FR-30 — Keyboard support

ArcDroid shall support keyboard input for titles that require it and for compatibility/troubleshooting workflows.

### FR-31 — Mouse support

ArcDroid shall support mouse or pointer input for titles that require it.

### FR-32 — Touch/front-end input compatibility

On Android, the architecture should allow RetroArch's supported touch/overlay mechanisms to reach the core without ArcDroid requiring its own unrelated touch UI.

### FR-33 — Remapping

ArcDroid shall coexist with RetroArch input remapping and should avoid unnecessary internal mapping behavior that prevents users from customizing controls.

### FR-34 — Input profile transparency

Automatic title-specific input profiles should improve defaults without making remapping impossible. Users must be able to override them.

---

## 13. Video requirements

### FR-35 — Correct game output

ArcDroid shall render supported Archimedes video modes required by games inside the compatibility target with correct geometry, timing, and color behavior to the extent required for accurate gameplay.

### FR-36 — RetroArch presentation controls

ArcDroid should provide correct native/core geometry and allow RetroArch to handle frontend presentation concerns such as shaders, integer scaling, display scaling, and overlays where appropriate.

### FR-37 — Aspect and resolution behavior

The core shall report video geometry consistently enough for frontends to present games without requiring users to understand Archimedes monitor configuration.

### FR-38 — No machine-monitor setup as normal UX

Users should not have to configure emulated monitors or desktop display modes before launching ordinary games unless a title presents an unavoidable special case.

---

## 14. Audio and timing requirements

### FR-39 — Real-time emulation

ArcDroid shall target real-time emulation on representative supported Android hardware without requiring routine manual frameskip or speed adjustment.

The architecture phase must define a representative device/performance matrix before release acceptance.

### FR-40 — Stable audio

Audio should remain synchronized and free from persistent crackle, underrun, runaway buffering, or drift caused by normal core operation on supported hardware.

### FR-41 — Timing correctness

Game logic, CPU timing, video timing, audio timing, and media behavior must prioritize compatibility and correctness rather than forcing all titles into one arbitrary refresh model.

### FR-42 — Frame pacing

ArcDroid should integrate with libretro timing in a way that produces stable frame pacing and avoids unnecessary latency or jitter on the primary Android target.

---

## 15. Save, persistence, and session requirements

### FR-43 — Persistent machine/game state

Where games or the emulated environment legitimately write persistent data, ArcDroid shall store it through deterministic frontend-compatible save locations rather than arbitrary emulator-specific paths.

### FR-44 — Save-state feasibility

RetroArch save states are desirable, but they shall be considered mandatory only after architecture verifies that the chosen emulation engine can serialize and restore the complete relevant machine state reliably.

A broken or deceptively partial save-state implementation is worse than explicitly marking save states unsupported.

### FR-45 — Save-state compatibility expectations

If save states are implemented, the project must define their compatibility expectations across core versions and configuration changes. ArcDroid must not imply guarantees it cannot maintain.

### FR-46 — Clean restart and content switching

Repeated load/unload/restart behavior must not depend on restarting the Android application or RetroArch process merely to recover emulator state.

---

## 16. Compatibility knowledge system

### FR-47 — Internal compatibility profiles

ArcDroid may maintain structured compatibility profiles containing game-specific information such as:

- recognized media fingerprints;
- preferred machine profile;
- required or preferred firmware;
- controller/joystick interface;
- boot behavior;
- disk set/order;
- known safe fallbacks;
- known limitations.

### FR-48 — Profiles are optimization, not allowlist

Compatibility profiles exist to automate and improve behavior. Their absence must not prevent otherwise supported media from being attempted.

### FR-49 — Deterministic identification

Where title identification affects configuration, it should be based on deterministic evidence such as content hashes, disk identity, media structure, or other reliable signals rather than filename guessing alone.

### FR-50 — User override of bad detection

If automatic title/profile detection is wrong, the user shall have a path to disable or override the resulting configuration without modifying the game file.

### FR-51 — Compatibility evidence

Claims that a title is supported should ultimately be backed by reproducible test evidence or a documented verified result, not merely by database presence.

---

## 17. Compatibility target and parity definition

### 17.1 Long-term parity target

ArcDroid's long-term target is to play the **game library that Arculator can play**, including titles that require different supported Archimedes machine configurations, operating-system ROMs, input interfaces, and game-relevant media behavior.

### 17.2 What parity means

For a title to count toward compatibility parity, ArcDroid should be able to reach materially equivalent playable behavior using a legally available user firmware setup and supported media representation.

Parity focuses on:

- successful boot/launch;
- correct game logic;
- usable controls;
- materially correct video and audio;
- required media changes;
- stability sufficient to play;
- required persistent behavior.

### 17.3 What parity does not mean

Parity does not require ArcDroid to duplicate:

- Arculator's user interface;
- Arculator's configuration screens;
- every non-game podule or peripheral;
- every workstation feature unrelated to gaming;
- every historical setup step visible to the user.

### 17.4 Compatibility regression policy

Once a title or representative behavior is accepted as working, future changes should treat loss of that behavior as a regression unless the Founder explicitly accepts the tradeoff.

---

## 18. Error handling and diagnostics

### FR-52 — Human-readable launch failures

Expected failures such as missing firmware, unsupported media, invalid/corrupt content, or incompatible configuration should produce useful frontend-visible messages when libretro allows it.

### FR-53 — No silent fallback that changes the game materially

ArcDroid should not silently choose a known-incompatible machine or firmware merely to make something appear to launch.

### FR-54 — Technical diagnostics without technical UX dependence

Logs may contain detailed machine, firmware, media, timing, and configuration information for troubleshooting. The player should not need to read those logs for routine operation.

### FR-55 — Fail safely

Malformed or unsupported media should not crash the frontend, corrupt unrelated save data, or leave native resources in a state that prevents subsequent content from loading cleanly.

---

## 19. Android product requirements

### FR-56 — Android-first native stability

The core must behave correctly across repeated Android lifecycle and RetroArch content-load/unload patterns relevant to normal use.

### FR-57 — Supported ABI strategy

The architecture phase shall define the Android ABI set for release. At minimum, it must evaluate the current practical RetroArch Android environment and prioritize the ABI used by contemporary supported devices.

### FR-58 — No dependence on desktop-only UI frameworks

The shipping Android core must not require an upstream emulator's desktop GUI framework to run.

### FR-59 — Storage behavior

Firmware, saves, persistent state, compatibility metadata, and temporary files shall use deterministic locations compatible with RetroArch/Android constraints and must not rely on arbitrary desktop paths.

### FR-60 — Offline operation

Once installed with required firmware and user content, ArcDroid must be able to launch and play games without an internet connection.

---

## 20. Performance and reliability requirements

### FR-61 — Representative-device performance gate

Before the first public-quality release, the project shall define representative Android test hardware and demonstrate real-time gameplay across a representative compatibility corpus without persistent performance-related audio/video failure.

### FR-62 — No uncontrolled resource growth

Loading, unloading, restarting, and switching titles must not cause unbounded memory, thread, file-handle, or native-resource growth.

### FR-63 — Crash containment

A malformed disk image or failed title launch should not normally crash the entire RetroArch process.

### FR-64 — Deterministic teardown

The core must release emulation resources sufficiently for repeated frontend sessions and test automation.

### FR-65 — Performance must not be bought through compatibility-breaking shortcuts

Optimization may not intentionally remove machine behavior required by accepted game compatibility without an explicit product decision.

---

## 21. Legal, licensing, and provenance requirements

### FR-66 — Verify upstream rights before incorporation

Any third-party emulator source, library, binary, firmware, or other dependency incorporated into ArcDroid must have its provenance and license verified before integration.

### FR-67 — Preserve required notices and source obligations

ArcDroid must comply with applicable attribution, notice, source-disclosure, linking, modification, and redistribution obligations of incorporated components.

### FR-68 — No game/firmware leakage

Commercial game images, firmware dumps, manuals, private test collections, credentials, and non-redistributable assets must not be committed to the repository or included in public CI/release artifacts without verified rights.

### FR-69 — Local private compatibility testing is allowed

Lawfully obtained private game and firmware assets may be used locally for compatibility testing, but test evidence should avoid redistributing the assets themselves.

### FR-70 — Product design must not depend on shipping unauthorized firmware

The core must remain usable with a user-supplied firmware model if the required operating-system ROMs cannot legally be bundled.

---

## 22. Upstream/emulation-engine requirements

The PRD intentionally does **not** select Arculator, CLK, or another engine.

The architecture process must evaluate candidate engines against the product requirements above, including:

- achievable game compatibility;
- breadth of Archimedes machine/firmware support;
- media fidelity and protected-media behavior;
- controllability of machine configuration and boot flow;
- ability to remove/bypass desktop UI dependencies;
- libretro integration feasibility;
- Android portability and native dependencies;
- timing/audio/video architecture;
- input flexibility;
- disk swapping/media interfaces;
- save-state feasibility;
- code quality and maintainability;
- upstream activity and synchronization strategy;
- license compatibility and distribution obligations;
- testability and determinism.

### AR-1 — Engine choice must serve ArcDroid UX

A mature emulator may be reused even if its existing UI is unsuitable, provided its emulation engine can be separated or adapted cleanly enough to meet ArcDroid's product requirements.

### AR-2 — No UI inheritance requirement

Selecting an upstream engine does not imply retaining that project's GUI, configuration philosophy, directory conventions, or machine-first workflow.

### AR-3 — No parity claim without evidence

The architecture team must not assume that reusing Arculator automatically produces Arculator compatibility, nor that another engine is insufficient, without verification.

---

## 23. First usable release definition

The exact version numbering and release channel are architecture/release decisions, but the first usable ArcDroid milestone should demonstrate the product thesis end-to-end.

At minimum, that milestone must prove:

1. A real libretro core loads in RetroArch on Android.
2. A user can launch representative loose Archimedes game media directly.
3. Required firmware can be supplied through the defined firmware location and recognized reliably.
4. At least one automatic machine/firmware profile path works without manual machine setup.
5. At least one unknown but supported disk can be attempted through a generic fallback rather than being rejected for database absence.
6. RetroPad input reaches a representative game correctly.
7. Keyboard and mouse paths are available for titles that require them.
8. Multi-disk control is demonstrated if included in the selected first-release media scope; otherwise the architecture and interface for it must already be defined and not blocked by design.
9. Audio and video run stably at real-time speed on representative Android hardware for the milestone corpus.
10. Repeated content load/unload does not require restarting RetroArch.
11. Missing required firmware produces a useful failure instead of an opaque crash/hang.
12. No copyrighted commercial firmware or game image is required in the repository or CI.

The first usable release does **not** need full Arculator parity. It must establish that the architecture can plausibly scale toward it.

---

## 24. Long-term release goals

After the first usable release, the project should progress toward:

1. Broad machine/firmware auto-selection across the Archimedes game library.
2. Increasing media-format coverage, including unusual/protected formats required for compatibility.
3. Multi-disk and CD/game-media workflows where required.
4. A growing evidence-backed compatibility profile database.
5. Better automatic controller mappings.
6. Arculator-class game compatibility across the representative library.
7. Stable save states if the chosen architecture can support them correctly.
8. Performance optimization across a wider Android device range.
9. Upstream-friendly maintenance where compatible with project goals and licensing.
10. Potential submission/distribution through appropriate libretro/RetroArch channels once product quality justifies it.

---

## 25. Success metrics

The project shall ultimately measure success using concrete evidence rather than subjective impressions.

### SM-1 — Launch simplicity

For recognized titles in the compatibility corpus, normal launch should require no manual machine/firmware configuration after one-time firmware setup.

### SM-2 — Compatibility coverage

Track the percentage and absolute count of the defined representative game corpus that reaches accepted playable status.

### SM-3 — Automatic configuration success

Track recognized titles that launch correctly using default `Auto` settings versus titles requiring manual overrides.

### SM-4 — Unknown-content behavior

Supported-format unknown content must be attempted rather than rejected for lack of database membership.

### SM-5 — Stability

Track crash-free repeated load/unload and gameplay sessions across representative Android hardware.

### SM-6 — Performance

Track sustained real-time emulation, frame pacing, and audio stability across the representative performance corpus and device matrix.

### SM-7 — Regression control

Previously accepted compatibility cases should remain passing through automated or reproducible regression testing.

The exact numerical release thresholds shall be set by Fable with Founder approval once the representative corpus and device matrix are defined.

---

## 26. Required compatibility/test corpus

Before a public-quality release, Fable shall define with the Founder a representative private/local test corpus covering materially different behavior, including where available:

- early and later Archimedes-era titles;
- different machine/CPU expectations;
- multiple firmware/OS generations;
- joystick-controlled games;
- keyboard-heavy games;
- mouse-driven games;
- multi-disk titles;
- protected or unusual disk formats;
- titles sensitive to CPU/timing behavior;
- titles requiring unusual boot behavior;
- titles that exercise persistent writes;
- CD-based or other non-floppy content if included in compatibility scope.

The corpus may use lawfully obtained private commercial content for local testing but must not place those assets in the repository or public CI.

Open, redistributable, synthetic, or purpose-built test assets should be added wherever they can verify the same technical behavior without rights concerns.

---

## 27. UX requirements summary

A successful ArcDroid user should experience the product approximately as follows:

### First-time setup

- Install the ArcDroid libretro core.
- Place legally obtained required Archimedes firmware in the documented RetroArch system location.
- Launch games from RetroArch/ES-DE.

### Normal known-title use

- Select title.
- Game launches.
- Controller works with sensible defaults.
- Swap disks through RetroArch if needed.
- Quit back to frontend normally.

### Unknown title

- Select media.
- ArcDroid attempts a generic compatible configuration.
- User can use advanced overrides if required.

### Missing firmware

- Select title.
- ArcDroid explains which required firmware capability is missing or what class of firmware is needed.
- Frontend remains stable.

### Problem title

- User can change advanced per-game options/remaps.
- The override persists through normal RetroArch mechanisms.
- User does not have to modify or rename the game file to keep the fix.

---

## 28. Explicit product decisions captured by this PRD

Upon Founder approval, the following are settled product decisions unless the Founder later changes the PRD:

1. **ArcDroid is a libretro/RetroArch core, not a standalone machine-emulator UI as its primary product.**
2. **Android is the primary user platform.**
3. **The normal UX is game-first: select game and play.**
4. **Machine and operating-system complexity should be automated and hidden from normal use.**
5. **Loose supported media must be launchable without a curated ROM-set/software-list gate.**
6. **Unknown supported media must be attempted, not blocked for lack of a compatibility record.**
7. **The long-term game-compatibility target is parity with the games Arculator can play, not parity with every non-game peripheral Arculator emulates.**
8. **ArcDroid must support access to the full game-relevant firmware/OS range needed to pursue that compatibility target.**
9. **Firmware is user-supplied when redistribution rights do not permit bundling and should be recognized/selected automatically after setup.**
10. **RetroPad is the default gaming input abstraction, with keyboard and mouse support where games require them.**
11. **RetroArch-native mechanisms should be used for disk control, remapping, core options, save locations, and frontend integration where appropriate.**
12. **Advanced machine configuration may exist but should not define the normal user experience.**
13. **The PRD does not select the emulation engine. Fable and Astra must evaluate and reconcile that architecture decision from current evidence before Sol implementation is authorized.**
14. **ArcDroid will not depend on distributing unauthorized firmware or commercial game content.**

---

## 29. Open product questions to resolve before release

These questions are intentionally left open because the current Founder discussion has not fixed them or because they require architecture evidence first:

1. What exact game/media set constitutes the v1 compatibility corpus?
2. Which media formats are mandatory in the first public release versus later parity milestones?
3. Which Android API levels and ABIs are supported at first release?
4. What minimum representative device class defines acceptable real-time performance?
5. Are save states required for v1, or accepted only if the selected engine supports complete reliable serialization?
6. Is an explicit Computer Mode ever a product feature, or merely a development/troubleshooting mechanism?
7. What numerical compatibility percentage is required for the first public-quality release?
8. Which distribution channels are targeted first: private core builds, manual RetroArch installation, libretro buildbot/upstream channels, or another sequence?
9. How much compatibility-profile data should ship inside the core versus in a separate project data file, provided offline launch remains guaranteed?
10. Which titles/media are the minimum mandatory test set for asserting meaningful Arculator-parity progress?

These questions do not block architecture discovery. Fable should resolve them with the Founder when evidence or implementation planning makes them material.

---

## 30. Acceptance criteria for the product architecture phase

Before implementation of the core begins, the architecture handoff must demonstrate that the proposed design can credibly satisfy this PRD.

### AC-PRD-1 — Libretro feasibility

The proposed architecture must explain how the emulation engine will operate as a libretro core without depending on an upstream desktop UI.

### AC-PRD-2 — Android feasibility

The architecture must verify Android build/runtime feasibility for the selected engine and its required dependencies.

### AC-PRD-3 — Compatibility path

The architecture must show a credible path from the initial milestone toward the Arculator-class game-compatibility target rather than knowingly choosing an engine incapable of the required machine/media breadth without a justified plan.

### AC-PRD-4 — Firmware model

The architecture must define how firmware is discovered, fingerprinted/validated, selected, and reported when missing without bundling unauthorized ROMs.

### AC-PRD-5 — Direct launch

The architecture must define how content passed by RetroArch becomes mounted/configured and, for known titles, reaches gameplay with minimal machine-level interaction.

### AC-PRD-6 — Unknown-content fallback

The architecture must define a real generic fallback path for supported but unrecognized media.

### AC-PRD-7 — Input model

The architecture must define how RetroPad, keyboard, mouse/pointer, remapping, and title-specific defaults interact.

### AC-PRD-8 — Media/disk model

The architecture must define how current and future media formats and multi-disk behavior map to libretro interfaces without recreating an emulator desktop workflow.

### AC-PRD-9 — Timing/audio/video

The architecture must identify the timing, audio, video, frame-pacing, and synchronization model and how it maps safely to libretro.

### AC-PRD-10 — Persistence and lifecycle

The architecture must define save/persistent-data locations, lifecycle ownership, repeated load/unload behavior, and whether complete save states are technically feasible.

### AC-PRD-11 — Licensing/provenance

The architecture must verify the selected upstream source and dependency licenses and state the obligations created by the planned distribution model.

### AC-PRD-12 — Test strategy

The architecture must define deterministic automated tests plus representative local/device compatibility evidence sufficient to evaluate the requirements above without committing copyrighted assets.

### AC-PRD-13 — Astra challenge

Because core/emulation-engine selection is a material architectural decision, Fable must obtain and adjudicate an independent Astra challenge before implementation is authorized.

### AC-PRD-14 — Founder gates

Any material change to the product decisions in Section 28, unresolved licensing risk, or architecture choice that materially changes the promised UX must be surfaced to the Founder before implementation proceeds.

---

## 31. Definition of product success

ArcDroid succeeds when an Archimedes game library can be treated like a game library rather than a collection of computer-emulation projects.

The core should be sophisticated enough internally to satisfy the strange machine, firmware, media, input, and timing requirements of Archimedes software, while making that sophistication largely invisible to the player.

The enduring product promise is:

> **ArcDroid handles the Archimedes. The player handles the game.**
