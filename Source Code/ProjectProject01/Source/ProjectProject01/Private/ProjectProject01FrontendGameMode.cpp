// File: Source/ProjectProject01/Private/ProjectProject01FrontendGameMode.cpp
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01FrontendGameMode.h"

#include "Blueprint/UserWidget.h"
#include "ProjectProject01LoginWidget.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectDFSDStartupIntro.h"
#include "ProjectDFSDTitleScene.h"

void AProjectProject01TitlePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	bShowMouseCursor = true;
	bAutoManageActiveCameraTarget = false;
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	FActorSpawnParameters SceneSpawn;
	SceneSpawn.Owner = this;
	SceneSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TitleScene = GetWorld()->SpawnActor<AProjectDFSDTitleScene>(FVector(0, 0, -20000), FRotator::ZeroRotator, SceneSpawn);
	if (TitleScene) { SetViewTarget(TitleScene); TitleScene->SetPresentationActive(true); }
	if (auto* Instance = GetGameInstance<UProjectProject01GameInstance>()) Instance->ClearEntireScreenFade();

	TitleWidget = CreateWidget<UProjectProject01TitleWidget>(this, UProjectProject01TitleWidget::StaticClass());
	if (ensureMsgf(IsValid(TitleWidget), TEXT("ProjectProject01 title widget creation failed.")))
	{
		TitleWidget->AddToViewport(100);
		if (auto* Instance = GetGameInstance<UProjectProject01GameInstance>(); Instance && Instance->ConsumeStartupIntro())
		{
			StartupIntro = CreateWidget<UProjectDFSDStartupIntro>(this, UProjectDFSDStartupIntro::StaticClass());
			if (StartupIntro)
			{
				// Block input without greying out the title underneath the final reveal.
				TitleWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
				if (TitleScene) TitleScene->SetPresentationActive(false);
				StartupIntro->OnFinished.BindUObject(this, &AProjectProject01TitlePlayerController::CompleteStartupIntro);
				StartupIntro->AddToViewport(90000);
				StartupIntro->SetKeyboardFocus();
			}
		}
	}
}

void AProjectProject01TitlePlayerController::CompleteStartupIntro()
{
	if (IsValid(TitleWidget)) { TitleWidget->SetVisibility(ESlateVisibility::Visible); TitleWidget->SetKeyboardFocus(); }
	if (IsValid(TitleScene)) TitleScene->SetPresentationActive(true);
}

void AProjectProject01TitlePlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (IsValid(StartupIntro)) { StartupIntro->OnFinished.Unbind(); StartupIntro->RemoveFromParent(); }
	if (IsValid(TitleScene)) { TitleScene->SetPresentationActive(false); TitleScene->Destroy(); TitleScene = nullptr; }
	Super::EndPlay(Reason);
}

AProjectProject01TitleGameMode::AProjectProject01TitleGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AProjectProject01TitlePlayerController::StaticClass();
	bStartPlayersAsSpectators = true;
}

void AProjectProject01LoginPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	LoginWidget = CreateWidget<UProjectProject01LoginWidget>(this, UProjectProject01LoginWidget::StaticClass());
	if (ensureMsgf(IsValid(LoginWidget), TEXT("ProjectProject01 login widget creation failed.")))
	{
		LoginWidget->AddToViewport(100);
	}
}

AProjectProject01LoginGameMode::AProjectProject01LoginGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AProjectProject01LoginPlayerController::StaticClass();
}

void AProjectProject01LobbyPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	LobbyWidget = CreateWidget<UProjectProject01LobbyWidget>(this, UProjectProject01LobbyWidget::StaticClass());
	if (ensureMsgf(IsValid(LobbyWidget), TEXT("ProjectProject01 lobby widget creation failed.")))
	{
		LobbyWidget->AddToViewport(100);
	}
}

AProjectProject01LobbyGameMode::AProjectProject01LobbyGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AProjectProject01LobbyPlayerController::StaticClass();
}

void AProjectProject01LeaderboardPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	LeaderboardWidget = CreateWidget<UProjectProject01LeaderboardWidget>(
		this, UProjectProject01LeaderboardWidget::StaticClass());
	if (ensureMsgf(IsValid(LeaderboardWidget), TEXT("ProjectProject01 leaderboard widget creation failed.")))
	{
		LeaderboardWidget->AddToViewport(100);
	}
}

AProjectProject01LeaderboardGameMode::AProjectProject01LeaderboardGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AProjectProject01LeaderboardPlayerController::StaticClass();
}
