# UI / tools exhaustive read-only review (2026-10-17)

Status: completed full EOF read of all 23 assigned source/project files (14,480 physical lines), plus three tool READMEs and dashboard .gitignore. No source changes, builds, publishing, server, DB, or operational actions. Findings below are static control-flow evidence, not live multiplayer reproductions.

Completed full EOF source reads:
- BuildServerHelper: AccountRestrictionForm.cs, CrashArtifactPolicyForm.cs, CrashReportDownloadForm.cs, DatabaseRecoveryForm.cs, RoomCapacityForm.cs, MainForm.Operations.cs, Program.cs.
- ClientLauncher: Program.cs, ResumableUpdateDownloader.cs.

Confirmed static findings so far:
1. Launcher `Program.cs:634-640`: on a fresh distribution without `.projectproject01-update.json`, `EnumerateFiles(installRoot,...)` yields an absolute path; `NormalizeRelativePath` rejects it. The `현재 버전 실행` button fails until an online update has written a local manifest. Use a relative path for discovery or a separately validated absolute path.
2. Helper `Program.cs:501-513` fails to exclude `.update-cache/`, whereas launcher `Program.cs:537-539` explicitly rejects it. Running the launcher beside a published package and leaving an interrupted download cache makes the next republished manifest invalid for every client. Exclude all launcher working directories and ignore them during executable discovery.
3. Helper `Program.cs:148-179` disables only the clicked button, with no common build/publish gate. `PackageAsync:249-268`, `PublishClientUpdateAsync:294-382`, and temporary EOS ini replacement `533-561` can overlap. Publishing while packaging mutates the same archive can produce inconsistent hashes/manifest; multiple packaging actions share `DefaultEngine.ini` and UAT singleton. Serialize archive/config mutations, snapshot roots, and guard helper close while a build owns temporary config.

Known previously reported updater junction traversal and missing persistent power-loss transaction journal are intentionally not counted as new findings here.

## Further confirmed findings

4. **P1 — black-screen join can permanently stall on an ordinary concurrent room poll.** `ProjectProject01LoginWidget.cpp:5859-5888` marks fade complete and holds the entire screen black, then asks for a game ticket only if no lobby request is in flight. The one-second `RefreshLobbyState:5463-5482` can be awaiting its room GET at that point. When that GET later completes, `RefreshCurrentRoomView:5660-5670` calls `BeginGameStartFade`, but `5828` exits because `bGameStartFadeComplete` is already true. Neither the refresh nor any other timer retries the skipped ticket request; `TravelToStartedGame:5715` requires that missing ticket. Retry ticket acquisition after every relevant completion / poll once fade is complete, bounded by timeout and match identity. Confidence: high.

5. **P2 — an old configured VPN address can prevent all backend listeners from starting.** Helper `Program.cs:617-624` blindly includes `GetConfiguredVpnHost` in Kestrel URLs; that getter accepts the persisted 25.x host even if the adapter is off or its address changed (`671-693`). By contrast the display / firewall address selection correctly checks `IsLocalIpv4Address` (`655`). Starting the backend with Hamachi disconnected or a stale configured address tries to bind a non-local endpoint and fails startup, including the otherwise usable localhost/LAN URLs. Only bind configured interface addresses that currently exist; warn and retain LAN/localhost listeners. Confidence: high.

6. **P2 — fast leaderboard tab changes discard the newest request and relabel old results.** UI role/scope changes immediately update selected role/scope (`LoginWidget.cpp:6449-6515`) while every refresh calls the single-flight Lobby subsystem. `LobbySubsystem.cpp:370-384` silently drops reads if another request is active; `264-291` changes its role/scope fields even before this rejection. A survivor-tab click while the mannequin GET is pending is dropped, then that mannequin response is rendered using the currently selected survivor columns (`LoginWidget.cpp:6374-6415`), with no automatic follow-up refresh. Disable/reconcile selection until completion or queue the newest filter and discard mismatched responses. Confidence: high.

7. **P2 — authenticated settings silently persist an IP that is not active.** The server-address input is unconditionally enabled (`LoginWidget.cpp:4290-4292`). Apply stores `Settings->SetDirectServerAddress` (`4464`) before `ConfigureMultiplayerServerAddress` (`4472`). GI `1858-1893` rejects all changes while signed in / in a room; UI then suppresses this failure specifically when Auth is signed in (`4475-4482`) and reports success / saves (`4500`). Current auth/lobby/security URLs remain the old host while settings display and persist the new one, taking effect on a later reconfiguration. Reject and restore the IP separately before mutating settings, or explicitly label a deferred change. Confidence: high.

8. **P2 — dashboard build output has concurrent unsynchronized writers.** `DiagnosticsDashboard/Program.cs:930-936`: stdout and stderr data-received callbacks both mutate one `StringBuilder` from separate process reader threads. A mixed stdout/stderr compiler run can corrupt output or throw in those callbacks, losing the very failure log the dashboard is collecting. Use independent `ReadToEndAsync` tasks or a lock/channel. The same runner has no cancellation / timeout or FormClosing linkage, so closing the dashboard during a build leaves the child engine compiler / editor alive. Confidence: high for the race / missing child lifecycle, timing-dependent manifestation.

9. **P3 — role guide lies about remapped movement keys.** `LoginWidget.cpp:2313-2323,2336-2345` hardcodes W/A/S/D, although movement remapping is supported (`4785-4788`, stored pending keys `4215-4218`). Sprint, ping and voice key labels are dynamic but forward/back/left/right are not. Read actual active bindings for every remappable key. Confidence: high.

## Complete assigned-source coverage ledger

Each entry was read from first line to EOF; SHA-256 records the reviewed working-tree contents at completion. I did not modify these source files. Parent review can compare hashes to the initial audit inventory. Only this documentation file was written by this reviewer. Generated bin/obj/Saved products, local secrets, vendor engine code, and binary assets are not source-review targets.

| Path | Physical lines | SHA-256 |
|---|---:|---|
| Tools/ProjectProject01BuildServerHelper/AccountRestrictionForm.cs |59|26D4357BB62A12F57679339E9486CD034798E61DD03F9D899864E19AAA277D40|
| Tools/ProjectProject01BuildServerHelper/CrashArtifactPolicyForm.cs |55|EE67A5A3B00979BCB99AF6FCF7C829D6FDB45DDE01B36D58AD93A7BE81D5018A|
| Tools/ProjectProject01BuildServerHelper/CrashReportDownloadForm.cs |46|CDB0769D120E4472BF19690229AE508BA4BC4468C26C35AA5CB9A210E34740EF|
| Tools/ProjectProject01BuildServerHelper/DatabaseRecoveryForm.cs |47|57804D6B83DDA0FE2AD130C72EF80E90E081638AF94FC851FB8BFBE7EA2D5461|
| Tools/ProjectProject01BuildServerHelper/FeatureCheckAccess.cs |2|70B009FB8BEDBEB1E30F984BB874201A54474D9B402B446FD31AEBB78572D70F|
| Tools/ProjectProject01BuildServerHelper/MainForm.Operations.cs |204|361559E2DE8DAD1DF91CB9A2B4AAA32E56BD8B9427FD29001F2E1954775688BD|
| Tools/ProjectProject01BuildServerHelper/Program.cs |1179|451B7F979AA74C7F23A756060CD059DE77B05557619F4A1457B1A1B7621CDBF8|
| Tools/ProjectProject01BuildServerHelper/ProjectProject01BuildServerHelper.csproj |10|86C8C064DCDB604D6E71EE2C220C09FD2F7727C05D6942A6A74C376FF1C208C1|
| Tools/ProjectProject01BuildServerHelper/RoomCapacityForm.cs |44|62C6DC6384735D3FE8AD9A5E44454E208D4EB79B273FED958F68F133BF88FC70|
| Tools/ProjectProject01BuildServerHelper/ServerAdministrationForm.cs |593|4F70B2A9D43DD0C1FEB5CCB6CCCF3C05B0CE6563DB26B10823E47D4739882BFB|
| Tools/ProjectProject01BuildServerHelper/SurveyAdministrationForm.cs |697|6069B58D474ABCD63BC86864DBF431F315A4A3F11DF5D58261ECB20613BB4A42|
| Tools/ProjectProject01BuildServerHelper/SurveyCharts.cs |197|03AA1F1B92B362C24669AFB9AB97A9C3141715C5B9A0163C2B6F725BBD6A0074|
| Tools/ProjectProject01ClientLauncher/FeatureCheckAccess.cs |2|70B009FB8BEDBEB1E30F984BB874201A54474D9B402B446FD31AEBB78572D70F|
| Tools/ProjectProject01ClientLauncher/Program.cs |744|01A1556FED949A8B16C002E99E4B48A61962C61D889C597CD2D93E666927122B|
| Tools/ProjectProject01ClientLauncher/ProjectProject01ClientLauncher.csproj |10|8FEBC32840AB931DA5C47C4CB9333170E588AEFFE7064FCDE8EBC95F13E4A277|
| Tools/ProjectProject01ClientLauncher/ResumableUpdateDownloader.cs |188|6E893D06A69818F72E4ED2D40EBD5F8A9337DF9EBE007697A81D51531D99099B|
| Tools/ProjectProject01DiagnosticsDashboard/Program.cs |1296|1F0D126AE831597F353C71B714E555E1D5E5213CA6B4E1396EC60A285489C778|
| Tools/ProjectProject01DiagnosticsDashboard/ProjectProject01DiagnosticsDashboard.csproj |11|A2E36AFED1EEB7E6D4245CBAC1E75BBEF024A34EE8932A74DAB5961298E8603A|
| Source/ProjectProject01Editor/ProjectProject01Editor.Build.cs |17|811CB2BAAC78D7A713632C0C82BD9165BF811567339A725CBBCA761DD5326C5C|
| Source/ProjectProject01Editor/Private/ProjectProject01DiagnosticsEditor.cpp |163|0FF71F44E6739CE47794D2FB83B348E52245C087AEBA3A8E5C3EC56F633BDC51|
| Source/ProjectProject01Editor/Private/ProjectProject01EditorModule.cpp |974|AD106D2CD949FA1254D07372966813591E8617602521BA55E3770ED33FADA6AE|
| Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp |6672|7C1B5DB60C09C25459678EF75839E2B62E53511AE2223438480320D27B56DBB7|
| Source/ProjectProject01/Public/ProjectProject01LoginWidget.h |1270|09A063A5602ECD99BC36B3CDA9BD1317ACA89B2667E906B2705BA17A7B0D4FAD|

## Independent native cross-check requested by parent reviewer

This is a targeted cross-check, not extra EOF coverage of the game mode/controller (another reviewer owns their full review).

- **Confirmed: cross-level cumulative result statistics / duration are lost.** New destination game mode `HandleSeamlessTravelPlayer:1671-1691` calls `RegisterMatchPlayer`, which creates a fresh default `FMultiplayTestServerPlayerRecord` (`1879`) and restores only identity, role, state and a newly spawned pawn's current death count (`1880-1912`). `FMultiplayTestServerPlayerRecord` defaults CaptureCount/RescueCount/FirstCaptureSeconds/AllCapturedSeconds/EscapeSeconds to zero. The outgoing path `PrepareLevelTransitionTravel:2570-2593` persists only timeline elapsed time and spawn eligibility; controller `SeamlessTravelFrom:143-163` copies no result statistics. New match `TryStartAuthoritativeMatch:2174` resets MatchStartWorldSeconds, and `GetElapsedMatchSeconds:2646-2656` ignores timeline PreviousLevelsSeconds; `BuildFinalResult:2692-2704` submits these reset totals/times. Thus previous-level captures and time disappear from final match results despite the same authenticated MatchId and continuous timeline. Death-count carryover is also absent, but whether it should reset at each new stage is a design question; do not classify death-count reset alone as a confirmed bug without clarifying desired rules.

- **Confirmed: a two-controller next-stage transition never reaches InProgress / reveal.** If one eliminated / forfeited survivor has left, only mannequin + advancing survivor controllers arrive. `Logout:1783` removed the departed controller; disconnected records are old-game-mode-local and not carried. New mode keeps ExpectedMatchPlayers=3 (`MultiplayTestGameMode.h:315`, no runtime assignment found). `TryStartAuthoritativeMatch:2138` always returns with two records, and `2166` additionally demands two survivor-role records. Consequently `FinalizeLevelTransitionArrivals:2187` never runs and `CompleteLevelTransitionCarryover:2219` never releases the surviving player's held fade/input. UE base `AGameModeBase::HandleSeamlessTravelPlayer` initializes only each real arriving controller; it does not recreate a departed player's controller. `GetSeamlessTravelActorList` retains old game mode only to transition world, not final destination, so it does not preserve the old map's roster. Fix requires carrying the authoritative match roster / expected arrivals across stages and counting only eligible connected advancing players for readiness, while retaining departed results independently. Confidence high.
