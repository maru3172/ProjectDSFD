# Backend whole-source review draft (2026-10-17)

Read-only audit. Findings below are static source evidence, not live production tests. No DB/server/build/publish action performed.

## Confirmed candidates

- P1 `LobbyEndpoints.cs:355-383,1988-2019`: current/return requires only Started membership, not authoritative match-ended evidence. All current members calling it during a live match resets room/match/roles. In managed mode `GameServerDemandManager.cs:181` then stops live process after room becomes Waiting. Needs server-ended marker or validated forced-departure semantics.
- WITHDRAWN as current defect: `/api/server/offline` global invalidation is intentionally called only after `ProjectProject01BuildServerHelper/Program.cs:847-906` StopAllGameServersAsync. Do not report this as a single-server shutdown bug. It would need scoping only if multi-host/per-server stop APIs are later added.
- P1 `GameServerDemandManager.cs:190-193,211`: after ObservedMatch, watchdog checks process exit but never expires missing heartbeat (`ready=false` indefinite). Hung process remains counted forever and blocks queued matches. Add separate last-heartbeat watchdog with bounded recovery.
- P2 `VoiceEndpoints.cs:58,70,106`, `Program.cs:52-193`: Results.Forbid executes without registered authentication service/default forbid scheme, so intended membership403 path becomes500. Return StatusCode403 or configure authentication scheme.
- P2 `ProjectProject01Admin.ps1:47,51`, `ServiceAdministration.cs` pending cross-check: announcement actions write effective maintenanceEnabled, not manualMaintenanceEnabled, potentially making scheduled maintenance permanent.
- P2 `ClientUpdateEndpoints.cs:38-60`: download path lexical containment check does not reject filesystem links/reparse points and serves any file under published root, not manifest allowlist. Only locally placed junction/sensitive publication content can exploit; not arbitrary remote junction creation.
- P1 `Program.cs:592-601`, `ServiceAdministration.cs:545-555`, `LobbyEndpoints.cs:993-1022,1742-1795`: normal logout / admin single-account revoke remove membership and revoke auth session, but leave already issued game_join_tickets while room survives. Consume validates ticket and assigned slot, not current membership/session, so a pre-issued unused ticket remains usable for up to60 seconds after revocation. AccountRestrictions.cs58 explicitly deletes tickets, showing inconsistent revocation semantics. Parent crosschecked native DS PreLogin: no further HTTP auth-session check after claims consumption. Fix ticket revocation transactionally and revalidate consume claims against current membership/session generation.
- P2 `SurveyEndpoints.cs:53-55,98,232-248,538-546`: submitted_at_utc is caller-controlled and only default is rejected. Any authenticated survey answer can set a huge future/past date; overall statistics then materializes every day across min..max, potentially millions of rows. Date9999-12-31 specifically makes DateOnly.AddDays(1) overflow; +9h SQL timezone conversion can itself produce NULL. Use server receive time, bounded acceptable timestamp skew and bounded trend window.
- P2 `ProjectProject01Admin.ps1:25-34,43-51`, `ServiceAdministration.cs:479-496`: old -ShutdownAtUtc script posts end without start, new API rejects paired-field mismatch400. Announce/Clear also copy effective maintenanceEnabled instead of manualMaintenanceEnabled; when invoked inside active scheduled maintenance, the script turns manual maintenance on and prevents auto-release after reservation ends.

## Additional confirmed resilience / hardening findings

- P2 `GameServerDemandManager.cs:55-56,69,95-113`; `LobbyEndpoints.cs:44-45,1815-1839`: managed registry/config only in memory and no startup reconciliation. Abrupt backend crash/restart loses owner instance IDs; surviving child Unreal processes have all heartbeats rejected409. Crosscheck `ProjectProject01GameInstance.cpp:1441-1447` self-terminates those processes after30 seconds, so infinite orphan-process claim is false. However persisted Started rooms/membership are not reclaimed by the restarted empty manager or stale cleanup (Started excluded), occupying slots/room/user membership until manual leave/logout/clear. Add bounded startup reconciliation against authenticated live process state.
- `ProjectProject01MySql.ps1:247-265,353-354` enables unverified leaderboard submissions automatically; `LobbyEndpoints.cs:1340-1344` checks only direct RemoteIpAddress loopback, so external requests forwarded by an on-host HTTPS reverse proxy satisfy the development bypass if that local config is reused. Set default false and use trusted forwarded identity / explicit development environment.
- P2 `Program.cs:347-364,715-736`: failed-login count is read and incremented outside a locking transaction; concurrent failures overwrite the same count. Use atomic increment or SELECT FOR UPDATE so distributed parallel attempts cannot evade the five-failure account lock. This is concurrency/static evidence, not an executed attack.
- P2 `Program.cs:107-108,127-128,157,163-164`: auth/ticket/survey-read budgets are still per shared IP. Voice was separated by account, but school NAT can still exhaust auth10/minute, tickets30/minute, survey reads300/minute across legitimate clients. This is a capacity-policy caveat (not every three-player session fails); split authenticated budgets by account while keeping broad IP abuse caps.
- P3 `Program.cs:274-286`: explicit JSON null displayName passes AuthValidation (null means login validation mode) then register dereferences Trim. Return structured400 for all missing/null fields rather than500. Do not interpret this as valid registration being broken.

## Test scope / confidence

All findings above are static analysis against current sources; no builds, HTTP requests, DB operations or gameplay were executed in this review. Existing FeatureChecks test successful API/storage cases but do not test revoked-ticket consumption, live heartbeat loss after ObservedMatch, hostile survey dates, reverse-proxy unverified scores, or whole-map gameplay. Existing SmokeTest Program.cs224-244 synthesizes server results and never drives actual UE movement/escape/win logic; its name is not proof of end-to-end gameplay coverage. Smoke code verifies top50 presence (263-285), so repeated runs on populated staging DB can fail once new synthetic entries are below top50 even when publication succeeded. Prefer querying the newly published record ID in an isolated fixture.

## Final coverage:45 files,9913 physical lines

Every file below read completely (large files in nontruncated sequential batches). Excluded generated bin/obj/LocalMySql and local secret configs. Initial combined output was truncated, so affected files were re-read in smaller batches.

### Tools/ProjectProject01Backend (28)

- AccountRestrictions.cs
- appsettings.json
- ClientUpdateEndpoints.cs
- ConfigureProjectProject01EosVoice.ps1
- CrashArtifactEndpoints.cs
- CrashArtifactStore.cs
- Database/001_CreateAuthSchema.sql
- EOSVoiceSetup.md
- EosVoiceIdentity.cs
- EosVoiceTokenService.cs
- FeatureCheckAccess.cs
- GameServerDemandManager.cs
- GameServerPresenceState.cs
- GameServerSlots.cs
- LobbyEndpoints.cs
- Program.cs
- ProjectProject01Admin.ps1
- ProjectProject01Backend.csproj
- ProjectProject01MySql.ps1
- README.md
- RoomCapacity.cs
- ServiceAdministration.cs
- SurveyDailyStatistics.cs
- SurveyEndpoints.cs
- VersionCompatibility.cs
- VoiceAccountRateLimit.cs
- VoiceEndpoints.cs
- VoiceParticipantDirectory.cs

### Tools/ProjectProject01FeatureChecks (14)

- CrashPolicyHttpChecks.cs
- CrashRetentionChecks.cs
- DemandSessionChecks.cs
- DrainSessionChecks.cs
- IntegrationChecks.cs
- OperationsIntegration.cs
- Program.cs
- ProjectProject01FeatureChecks.csproj
- RecoveryChecks.ps1
- ResumeDownloadChecks.cs
- Run-IsolatedIntegration.ps1
- UpdateInstallChecks.cs
- VoiceDirectoryChecks.cs
- VoiceSecurityChecks.cs

### Tools/ProjectProject01SmokeTest (3)

- Program.cs
- ProjectProject01SmokeTest.csproj
- README.md

Additional cross-boundary reads only: native GameInstance.cpp1383-1457 for orphan self-exit; Helper Program.cs847-930 for intentional global stop. Those reads do not count as full-file coverage owned by this audit partition.
