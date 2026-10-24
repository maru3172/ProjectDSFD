#include "ProjectOpenObserver.h"
#include "MultiplayTestGameMode.h"
#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "ProjectLoadingScreenSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"

APlayerController* AMultiplayTestGameMode::SpawnPlayerController(ENetRole InRole,const FString& Options)
{
    const auto* Claim=PendingValidatedJoinClaims.Find(UGameplayStatics::ParseOption(Options,TEXT("GameTicket")));
    if(Claim&&Claim->Role==TEXT("Observer"))
    {
        TGuardValue<TSubclassOf<APlayerController>> Class(PlayerControllerClass,AProjectOpenObserverController::StaticClass());
        return Super::SpawnPlayerController(InRole,Options);
    }
    return Super::SpawnPlayerController(InRole,Options);
}
bool AMultiplayTestGameMode::HasOpenObservers()const
{
    for(TActorIterator<AProjectOpenObserverController> It(GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed()&&It->GetAuthenticatedRole()==TEXT("Observer"))return true;
    return false;
}
void AMultiplayTestPlayerController::AdjustObserverViewStream(int32 Delta)
{
    if(!HasAuthority()||bServerControlledBot)return;
    ObserverViewConsumers=FMath::Clamp(ObserverViewConsumers+Delta,0,2);
    if(ObserverViewConsumers==1&&Delta>0)ObserverViewRotation=GetControlRotation();
    ClientObserverViewStream(ObserverViewConsumers>0);
}
void AMultiplayTestPlayerController::ClientObserverViewStream_Implementation(bool Enabled)
{bSendObserverView=Enabled;NextObserverViewAt=0;}
void AMultiplayTestPlayerController::ServerObserverViewRotation_Implementation(FRotator Rotation)
{
    const double Now=GetWorld()->GetTimeSeconds();
    if(ObserverViewConsumers<=0||Now-LastObserverViewAt<.045||Rotation.ContainsNaN())return;
    LastObserverViewAt=Now;ObserverViewRotation=Rotation.GetNormalized(); // Visual-only. Never used by AI, hit or movement validation.
}
void AProjectOpenObserverController::Admit()
{
    if(!HasAuthority()||GetAuthenticatedRole()!=TEXT("Observer"))return;
    int32 Count=0;for(TActorIterator<AProjectOpenObserverController> It(GetWorld());It;++It)++Count;
    if(Count>2){ClientFailLoadingGate(TEXT("관전 정원이 초과되었습니다."));Destroy();return;}
    if(!GetPawn())
    {
        FActorSpawnParameters Params;Params.Owner=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        FVector ObserverStartLocation(0,0,250);
        if(auto* Mode=GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>())
            if(auto* Start=Mode->FindPlayerStart(this))ObserverStartLocation=Start->GetActorLocation()+FVector(0,0,120);
        auto* FreePawn=GetWorld()->SpawnActor<ASpectatorPawn>(ASpectatorPawn::StaticClass(),ObserverStartLocation,FRotator::ZeroRotator,Params);
        if(!FreePawn){ClientFailLoadingGate(TEXT("관전 시점을 준비하지 못했습니다."));return;}
        FreePawn->SetReplicates(true);FreePawn->bOnlyRelevantToOwner=true;FreePawn->SetReplicateMovement(false);
        FreePawn->SetActorEnableCollision(false);FreePawn->SetActorHiddenInGame(true);Possess(FreePawn);
    }
    bLevelTransitionCarryover=false;bServerTransitionBlackReady=false;bSpawnAfterLevelTransition=false;
    SetServerMatchState(EMultiplayTestMatchPhase::RoleAssignment,EMultiplayTestSurvivorState::NotApplicable);
    Selected.Reset();ServerTarget.Reset();NextStateAt=0;NextPanelAt=0;bResultDelivered=false;bTargetDirty=true;ClientEnterObservation();
    UE_LOG(LogTemp,Log,TEXT("[OpenObserver] Admitted observer for match %s; excluded from playing roster."),*GetAuthenticatedMatchId());
}
void AProjectOpenObserverController::ClientEnterObservation_Implementation()
{
    SurvivorSpectatorView={};bRevealed=false;bResultDelivered=false;LocalTarget.Reset();
    bObservationEntered=true;
    ApplyObservationPresentation();
    ShowObserverWidget();
}
void AProjectOpenObserverController::ApplyObservationPresentation()
{
    // Observers never get a pawn reveal or a loading-gate release: switch those player paths off.
    bInitialGameplayFadePending=false;bInitialGameplayFadeScheduled=false;
    ResetIgnoreMoveInput();ResetIgnoreLookInput();
    GetWorldTimerManager().ClearTimer(InitialGameplayReadinessTimer);
    bLoadingGateRequired=false;bLoadingGateReleased=true;bClientArrivalViewPending=false;bLocalLevelTransitionFadeHeld=false;
    if(auto* GI=GetGameInstance())
    {
        if(auto* Loading=GI->GetSubsystem<UProjectLoadingScreenSubsystem>()){Loading->NotifyViewReady();Loading->CancelLoading();}
        if(auto* Project=Cast<UProjectProject01GameInstance>(GI))
        {
            // Stay black until the match is running; if it already revealed, keep the view open.
            if(bRevealed){if(PlayerCameraManager)PlayerCameraManager->StopCameraFade();Project->ClearEntireScreenFade();}
            else Project->HoldEntireScreenBlack();
        }
    }
    ClientConfigureSurvivorCommunication_Implementation(FString(),FString(),TEXT("Disabled"));
    bShowMouseCursor=true;SetInputMode(FInputModeGameAndUI());SetIgnoreLookInput(true);AppliedLookMode=0;
    bAutoManageActiveCameraTarget=false;
    if(AActor* Followed=LocalTarget.Get()){SetIgnoreMoveInput(true);SetViewTarget(Followed);}
    else if(GetPawn())SetViewTarget(GetPawn());
    UpdateLookMode();
}
void AProjectOpenObserverController::UpdateLookMode()
{
    // Switch the input mode only when it really changes: re-applying it (e.g. on every mannequin body swap)
    // releases the mouse capture, and the view stops turning until the viewport is clicked again.
    const int8 Wanted=LocalTarget.IsValid()&&!bLocked&&!bCursorHeld&&!bResultDelivered?1:0;
    if(Wanted)ResetIgnoreLookInput();else SetIgnoreLookInput(true);
    if(Wanted==AppliedLookMode)return;
    AppliedLookMode=Wanted;
    if(Wanted){bShowMouseCursor=false;SetInputMode(FInputModeGameOnly());}
    else{bShowMouseCursor=true;SetInputMode(FInputModeGameAndUI());}
}
void AProjectOpenObserverController::ShowObserverWidget()
{
    // Seamless travel can deliver the admission RPC before the new controller owns a viewport player.
    if(!IsLocalController()||!GetLocalPlayer()){bObserverWidgetPending=true;return;}
    bObserverWidgetPending=false;
    if(ObserverWidget)ObserverWidget->RemoveFromParent();
    ObserverWidget=CreateWidget<UProjectOpenObserverWidget>(this,UProjectOpenObserverWidget::StaticClass());
    if(ObserverWidget)ObserverWidget->AddToViewport(1000);
}
void AProjectOpenObserverController::BeginPlay()
{
    Super::BeginPlay();
    if(bObservationEntered&&!bResultDelivered)ApplyObservationPresentation(); // Same guard as ReceivedPlayer.
}
void AProjectOpenObserverController::ReceivedPlayer()
{
    Super::ReceivedPlayer();
    // The base class sets up a player's presentation here (black hold, loading gate, pawn-reveal timer).
    // When admission arrived first, that would leave an observer on a black screen for the whole match.
    if(bObservationEntered&&!bResultDelivered)ApplyObservationPresentation();
    if(bObserverWidgetPending&&!bResultDelivered)ShowObserverWidget();
}
void AProjectOpenObserverController::SetupInputComponent()
{
    APlayerController::SetupInputComponent();
    if(InputComponent)InputComponent->BindKey(EKeys::Escape,IE_Pressed,static_cast<AMultiplayTestPlayerController*>(this),&AMultiplayTestPlayerController::ToggleSessionMenu);
}
bool AProjectOpenObserverController::InputKey(const FInputKeyEventArgs& Params)
{
    if(bResultDelivered)return true; // Do not reopen movement/look during the final blackout.
    if((Params.Key==EKeys::LeftAlt||Params.Key==EKeys::RightAlt)&&LocalTarget.IsValid()&&!bLocked&&(Params.Event==IE_Pressed||Params.Event==IE_Released))
    {bCursorHeld=Params.Event==IE_Pressed;UpdateLookMode();}
    // Free flight: hold the right button to turn the view.
    if(Params.Key==EKeys::RightMouseButton&&!bLocked&&!LocalTarget.IsValid())
    {
        if(Params.Event==IE_Pressed){ResetIgnoreLookInput();bShowMouseCursor=false;SetInputMode(FInputModeGameOnly());AppliedLookMode=1;}
        else if(Params.Event==IE_Released){SetIgnoreLookInput(true);bShowMouseCursor=true;SetInputMode(FInputModeGameAndUI());AppliedLookMode=0;}
    }
    return APlayerController::InputKey(Params); // No voice, attack, possession or player command bindings.
}
void AProjectOpenObserverController::PlayerTick(float Delta)
{
    Super::PlayerTick(Delta);
    if(!IsLocalController()||bLocalMatchResultRequested)return;
    if(LocalTarget.IsValid())
    {
        if(bLocked){SetControlRotation(FMath::RInterpTo(GetControlRotation(),ReceivedRotation,Delta,18));RotationInput=FRotator::ZeroRotator;}
        if(!IsMoveInputIgnored())SetIgnoreMoveInput(true);
        if(auto* Movement=GetPawn()?GetPawn()->GetMovementComponent():nullptr)Movement->StopMovementImmediately();
    }
}
void AProjectOpenObserverController::TickActor(float Delta,ELevelTick Kind,FActorTickFunction& TickFunction)
{
    Super::TickActor(Delta,Kind,TickFunction);
    if(!HasAuthority()||bResultDelivered||GetAuthenticatedRole()!=TEXT("Observer"))return;
    auto* Mode=GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>();if(!Mode)return;
    const double Now=GetWorld()->GetTimeSeconds();if(Now<NextStateAt)return;NextStateAt=Now+.1;
    AMultiplayTestPlayerController* SelectedController=nullptr;AActor* Target=nullptr;
    TArray<FProjectOpenObserverEntry> Entries;int32 Active=0,Escaped=0,Eliminated=0;
    for(const auto& Pair:Mode->MatchPlayerRecords)if(auto* PC=Pair.Key.Get())
    {
        FProjectOpenObserverEntry Entry;Entry.PlayerId=Pair.Value.UserId;Entry.Name=Pair.Value.DisplayName;Entry.Role=Pair.Value.Role;
        const auto State=Pair.Value.SurvivorState;
        Active+=State==EMultiplayTestSurvivorState::Active;Escaped+=State==EMultiplayTestSurvivorState::Escaped;
        Eliminated+=State==EMultiplayTestSurvivorState::Eliminated;
        Entry.State=State==EMultiplayTestSurvivorState::Active?TEXT("생존"):State==EMultiplayTestSurvivorState::Eliminated?TEXT("탈락"):
            State==EMultiplayTestSurvivorState::Escaped?TEXT("탈출"):State==EMultiplayTestSurvivorState::TransitionWaiting?TEXT("다음 층 대기"):TEXT("마네킹");
        Entries.Add(Entry);
        if(Entry.PlayerId==Selected)
        {
            SelectedController=PC;Target=Pair.Value.Role==TEXT("Mannequin")?static_cast<AActor*>(PC->GetViewedMannequin()):static_cast<AActor*>(Pair.Value.SurvivorPawn.Get());
            // Mid body swap the mannequin player has no viewed body for a moment: stay on the last one.
            if(!Target&&Pair.Value.Role==TEXT("Mannequin")&&!ServerTarget.IsStale())Target=ServerTarget.Get();
        }
    }
    if(StreamedPlayer.Get()!=(bLocked?SelectedController:nullptr))
    {
        if(auto* Old=StreamedPlayer.Get())Old->AdjustObserverViewStream(-1);
        StreamedPlayer=bLocked?SelectedController:nullptr;if(auto* New=StreamedPlayer.Get())New->AdjustObserverViewStream(1);
    }
    const FRotator Rotation=SelectedController?(SelectedController->IsServerControlledBot()&&Target?Target->GetActorRotation():SelectedController->GetObserverViewRotation()):FRotator::ZeroRotator;
    if(bTargetDirty||ServerTarget.IsStale()||ServerTarget.Get()!=Target)
    {bTargetDirty=false;ServerTarget=Target;SetViewTarget(Target?Target:GetPawn());ClientObservationTarget(Target,bLocked,Rotation);}
    if(bLocked&&Target)ClientObservedRotation(Rotation);
    const FString Summary=FString::Printf(TEXT("%s\n경기 시간 %.0f초\n생존 %d / 탈출 %d / 탈락 %d"),*GetWorld()->GetMapName(),Mode->GetElapsedMatchSeconds(),Active,Escaped,Eliminated);
    if(Now>=NextPanelAt||GetMatchPhase()!=Mode->AuthoritativeMatchPhase)
    {NextPanelAt=Now+.5;ClientObservationState(Entries,Summary,Mode->IsAuthoritativeMatchInProgress());}
    if(GetMatchPhase()!=Mode->AuthoritativeMatchPhase)SetServerMatchState(Mode->AuthoritativeMatchPhase,EMultiplayTestSurvivorState::NotApplicable);
    if(!bResultDelivered&&(Mode->AuthoritativeMatchPhase==EMultiplayTestMatchPhase::Ending||Mode->AuthoritativeMatchPhase==EMultiplayTestMatchPhase::Results))
    {
        bResultDelivered=true;if(auto* Old=StreamedPlayer.Get())Old->AdjustObserverViewStream(-1);StreamedPlayer.Reset();
        FMultiplayTestMatchResult Result;Result.MatchId=GetAuthenticatedMatchId();Result.Role=TEXT("Observer");Result.Outcome=TEXT("Observed");Result.MatchDurationSeconds=Mode->GetElapsedMatchSeconds();
        auto Append=[&](AMultiplayTestPlayerController* Participant,const FMultiplayTestServerPlayerRecord& Record)
        {
            const auto Final=Mode->BuildFinalResult(Participant,Record);
            FProjectObservedPlayerResult Row;Row.Name=Record.DisplayName;Row.Role=Final.Role;Row.Outcome=Final.Outcome;Row.bSuccess=Final.bSuccess;
            Row.CaptureCount=Final.CaptureCount;Row.RescueCount=Final.RescueCount;Row.FirstCaptureSeconds=Final.FirstCaptureSeconds;Row.AllCapturedSeconds=Final.AllCapturedSeconds;Row.EscapeSeconds=Final.EscapeSeconds;
            Result.ObservedPlayers.Add(Row);
        };
        for(const auto& Pair:Mode->MatchPlayerRecords)Append(Pair.Key.Get(),Pair.Value);
        for(const auto& Pair:Mode->DisconnectedMatchRecords)Append(nullptr,Pair.Value);
        Result.ObservedPlayers.StableSort([](const auto& A,const auto& B){return A.Role==TEXT("Mannequin")&&B.Role!=TEXT("Mannequin");});
        if(Mode->TimelineLog){Result.Timeline=Mode->TimelineLog->Events;Result.OmittedTimelineEvents=Mode->TimelineLog->OmittedEvents;}
        ClientFinishObservation(Result);
    }
}
void AProjectOpenObserverController::Select(const FString& Id,bool Locked){ServerSelect(Id,Locked);}
void AProjectOpenObserverController::ServerSelect_Implementation(const FString& Id,bool Locked)
{
    if(bResultDelivered||Id.Len()>64||GetAuthenticatedRole()!=TEXT("Observer"))return;
    const double Now=GetWorld()->GetTimeSeconds();if(Now-LastSelectionAt<.08)return;
    auto* Mode=GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>();if(!Mode)return;
    bool Valid=Id.IsEmpty();for(const auto& Pair:Mode->MatchPlayerRecords)Valid|=Pair.Value.UserId==Id;
    if(!Valid)return;
    LastSelectionAt=Now;
    Selected=Id;bLocked=Locked&&!Id.IsEmpty();bTargetDirty=true;NextStateAt=0;
}
void AProjectOpenObserverController::ClientObservationTarget_Implementation(AActor* Target,bool Locked,FRotator Rotation)
{
    if(bResultDelivered)return;
    LocalTarget=Target;bLocked=Locked;ReceivedRotation=Rotation;
    SurvivorSpectatorView.bEnabled=Target&&Target->IsA<APlayerCharacter>();SurvivorSpectatorView.Target=Cast<APlayerCharacter>(Target);
    LocalSpectatorLookTarget=Target;
    ResetIgnoreMoveInput();if(Target)SetIgnoreMoveInput(true);
    bCursorHeld=false;UpdateLookMode();
    if(!Target&&GetPawn()&&PlayerCameraManager)GetPawn()->SetActorLocation(PlayerCameraManager->GetCameraLocation());
    SetViewTarget(Target?Target:GetPawn());
}
void AProjectOpenObserverController::ClientObservedRotation_Implementation(FRotator Rotation){ReceivedRotation=Rotation;}
void AProjectOpenObserverController::ClientObservationState_Implementation(const TArray<FProjectOpenObserverEntry>& Entries,const FString& Summary,bool Playing)
{
    if(bResultDelivered)return;
    if(ObserverWidget)ObserverWidget->Refresh(Entries,Summary);
    if(Playing&&!bRevealed)
    {
        bRevealed=true;if(PlayerCameraManager)PlayerCameraManager->StopCameraFade();
        if(auto* GI=GetGameInstance<UProjectProject01GameInstance>())GI->FadeEntireScreenFromBlack(.6f);
    }
}
void AProjectOpenObserverController::ClientCloseObservation_Implementation()
{bResultDelivered=true;if(ObserverWidget)ObserverWidget->RemoveFromParent();}
void AProjectOpenObserverController::ClientFinishObservation_Implementation(const FMultiplayTestMatchResult& Result)
{
    bResultDelivered=true;SetIgnoreMoveInput(true);SetIgnoreLookInput(true);
    bInitialGameplayFadePending=false;bInitialGameplayFadeScheduled=false;
    GetWorldTimerManager().ClearTimer(InitialGameplayReadinessTimer);
    GetWorldTimerManager().ClearTimer(InitialGameplayRevealTimer);
    float Remaining=0;if(const auto* Survivor=Cast<APlayerCharacter>(GetViewTarget()))Remaining=Survivor->GetLocalDeathPresentationRemaining(this);
    auto Black=FTimerDelegate::CreateWeakLambda(this,[this,Result]()
    {
        if(auto* GI=GetGameInstance<UProjectProject01GameInstance>())GI->FadeEntireScreenToBlack(.6f);
        GetWorldTimerManager().SetTimer(ObserverEndTimer,FTimerDelegate::CreateWeakLambda(this,[this,Result]()
        {ClientCloseObservation_Implementation();ClientPresentMatchResult_Implementation(Result);}),.65f,false);
    });
    if(Remaining>.001f)GetWorldTimerManager().SetTimer(ObserverEndTimer,Black,FMath::Min(Remaining,5.f),false);
    else Black.Execute();
}
void AProjectOpenObserverController::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(ObserverEndTimer);
    if(HasAuthority())if(auto* Old=StreamedPlayer.Get())Old->AdjustObserverViewStream(-1);
    if(ObserverWidget)ObserverWidget->RemoveFromParent();Super::EndPlay(Reason);
}
