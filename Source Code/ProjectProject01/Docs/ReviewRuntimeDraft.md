# Runtime read-only audit draft (2026-10-17)

Scope completed:81/81 files under `Source/ProjectProject01/` after excluding root-owned stems listed below (15,052lines by initial inventory). Every scoped file was read to EOF; truncated output ranges were reread. This is a static source audit, not a whole-project or live-play certification. No runtime code is changed; no build, backend, DB, game server or packaging operation was run.

Root-owned exclusions (cpp/h): ProjectProject01LoginWidget,MultiplayTestGameMode,MultiplayTestPlayerController,ProjectProject01GameInstance,ProjectProject01SaveSubsystem,ProjectProject01LevelTransitionTrigger,ProjectProject01LobbySubsystem,ProjectProject01AuthSubsystem. Editor module is separately assigned.

## Confirmed findings so far

- P2: Helper stuck recovery cannot reach the default timeout. `Private/HelperRearGuardAIController.cpp:119` checks 1.5 seconds without movement, while routine 0.25-second MoveTo reissues overwrite `LastProgressTime` at line153 even without progress. Keep progress time independent from request time and reset it only on real displacement/new target.
- P2: Reactive-light blinking ignores the reduced-flashing setting. `Private/MannequinReactiveLight.cpp:285-307,333-348` continuously switches intensity/visibility/emissive and never reads accessibility settings; default minimum0.12seconds. This is local visual logic, so each client can honor its setting without changing game rules.
- P2: Survivor hit/death visuals bypass local accessibility intensity. `Private/PlayerCharacter.cpp:1638-1650,1674-1683,1733-1740,1757-1786` uses raw opacity and direct camera transforms (including180degree turn), without GetLocalFlashScale/ScreenShakeScale. `GameInstance.cpp:257-267` only stops engine camera shakes, not these direct transforms. Heartbeat VFX correctly uses local scales at918-922, demonstrating the intended pattern.
- P2: One-shot pain/fall/capture synths never stop themselves. `Private/PlayerHeartbeatComponent.cpp:154-203,293-303,306-341,376-386,389-426,456-466`: bAutoDestroy=false; finishing a pulse only changes a boolean, then OnGenerateAudio continues filling/looping zeros until external teardown. Capture has an explicit match-end Stop in MultiplayTestPlayerController.cpp:940, but no normal pulse-end Stop; pain/fall have none in PlayerCharacter. Footsteps already implement an idleStop at115-117. This wastes audio voices/callback work after first use; do not describe it as continuing audible sound.
- P2: Single AI checkpoint is a pose/properties snapshot, not the requested behavior snapshot. `SaveSubsystem.cpp:444-451,525-546` captures mannequin/helper actors and their components, not AI controllers/BT services. `BTService_MannequinAI.h:53-77` activation/destination/transition state is non-UPROPERTY; commands in `MannequinAICharacter.h:160-162` are also nonserialized. Saved enemy that already saw the player can reload at the right location with fresh detection/roaming state and require seeing the player again.
- P2: Diagnostic collector discards explicit interrupted outcome. Deinitialize calls StopCapture("Inconclusive: world ended...") at205, but StopCapture ignores the supplied Outcome when choosing FinalOutcome at329-343 and can write Pass if numeric criteria passed before travel/termination. Interrupted test should stay inconclusive unless a completion marker was observed.

## Files read completely

Paths below are relative to `Source/ProjectProject01/`.

- ProjectProject01.Build.cs
- ProjectProject01.cpp
- ProjectProject01.h
- ProjectProject01Server.Target.cs
- Public/AnimNotify_MannequinFootstep.h
- Private/AnimNotify_MannequinFootstep.cpp
- Public/BTTask_CompleteRoamingTask.h
- Private/BTTask_CompleteRoamingTask.cpp
- Public/HelperRearGuardAIController.h
- Private/HelperRearGuardAIController.cpp
- Public/InteractionActorBase.h
- Private/InteractionActorBase.cpp
- Public/InteractionInterface.h
- Private/InteractionInterface.cpp
- Public/MannequinAIController.h
- Private/MannequinAIController.cpp
- Public/BTService_MannequinAI.h
- Private/BTService_MannequinAI.cpp
- Public/PlayerInteractionComponent.h
- Private/PlayerInteractionComponent.cpp
- Public/ProjectPracticeGameModeBase.h
- Private/ProjectPracticeGameModeBase.cpp
- Public/ProjectProject01Checkpoint.h
- Private/ProjectProject01Checkpoint.cpp
- Public/ProjectProject01CheckpointSaveable.h
- Private/ProjectProject01CheckpointSaveable.cpp
- Public/ProjectProject01CheckpointPreview.h
- Private/ProjectProject01CheckpointPreview.cpp
- Public/HelperRearGuardCharacter.h
- Private/HelperRearGuardCharacter.cpp
- Public/MannequinAICharacter.h
- Private/MannequinAICharacter.cpp
- Public/FootstepSynthComponent.h
- Private/FootstepSynthComponent.cpp
- Public/ProjectProject01SpatialAudio.h
- Private/ProjectProject01SpatialAudio.cpp
- Public/MannequinReactiveLight.h
- Private/MannequinReactiveLight.cpp
- Public/PlayerFeedbackWidgets.h
- Private/PlayerFeedbackWidgets.cpp
- Public/PlayerCharacter.h
- Private/PlayerCharacter.cpp
- Public/PlayerHeartbeatComponent.h
- Private/PlayerHeartbeatComponent.cpp
- Public/ProjectProject01VoiceAudibility.h
- Public/ProjectProject01VoiceParticipantControls.h
- Public/ProjectProject01VoiceChatSubsystem.h
- Private/ProjectProject01VoiceChatSubsystem.cpp
- Private/ProjectProject01VoicePreferences.cpp
- Private/ProjectProject01VoiceRoster.cpp
- Public/ProjectProject01VoiceSettingsWidget.h
- Private/ProjectProject01VoiceSettingsWidget.cpp
- Public/ProjectProject01TuningData.h
- Private/ProjectProject01TuningData.cpp
- Public/ProjectProject01SinglePlayerSaveGame.h
- Private/ProjectProject01SinglePlayerSaveGame.cpp
- Public/ProjectProject01MatchTimeline.h
- Private/ProjectProject01MatchTimeline.cpp
- Public/MultiplayMannequinTypes.h
- Public/ProjectProject01DiagnosticsTypes.h
- Public/ProjectProject01DiagnosticsSubsystem.h
- Private/ProjectProject01DiagnosticsSubsystem.cpp
- Private/ProjectProject01CrashFiles.h
- Private/ProjectProject01CrashFiles.cpp
- Public/ProjectProject01FrontendGameMode.h
- Private/ProjectProject01FrontendGameMode.cpp
- Public/ProjectProject01Localization.h
- Private/ProjectProject01Localization.cpp
- Public/ProjectProject01PingMarker.h
- Private/ProjectProject01PingMarker.cpp
- Public/ProjectProject01RoleSpawnPoint.h
- Private/ProjectProject01RoleSpawnPoint.cpp
- Public/ProjectProject01VersionContract.h
- Private/ProjectProject01VersionContract.cpp
- Private/ProjectProject01CheckpointPreviewTests.cpp
- Private/ProjectProject01MannequinCameraTests.cpp
- Private/ProjectProject01MatchTimelineTests.cpp
- Private/ProjectProject01OperationsTests.cpp
- Private/ProjectProject01SpatialAudioTests.cpp
- Private/ProjectProject01VoiceParticipantTests.cpp
- Private/ProjectProject01VoiceSecurityTests.cpp

## Cross-check status

All initially unconfirmed candidates were checked against their call sites and the local UE implementation. The checkpoint system does not even capture the separate AIController/Blackboard by default; prior assumptions that it did were incorrect.

Additional cross-read evidence (not part of81file count): SaveSubsystem.cpp444-451,486-673; GameInstance.h68,82-86 and GameInstance.cpp245-275; local UE AudioMixer SynthComponent.cpp160-218,435-500,630-640 confirms a full-length zero buffer does not complete the synth. DefaultGame.ini48-57 confirms recognized default diagnostic criteria. No live audio, navigation, packaged client, EOS multi-PC or checkpoint roundtrip tests were run for these findings. Findings are source-confirmed defects with concrete trigger paths, not newly observed playtest failures.

