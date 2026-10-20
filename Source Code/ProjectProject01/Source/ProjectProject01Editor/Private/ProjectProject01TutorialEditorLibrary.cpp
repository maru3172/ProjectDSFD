#include "ProjectProject01TutorialEditorLibrary.h"
#include "AI/NavigationSystemBase.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "ProjectProject01Tutorial.h"
#include "ToolMenus.h"
#include "Containers/Ticker.h"
#include "Misc/PackageName.h"
#include "HAL/IConsoleManager.h"

namespace ProjectTutorialEditor
{
	const TCHAR* Map=TEXT("/Game/MyProject/Level/MultiplayerTutorial");
	FString PreviousURL;
	bool bPendingOverride=false;
	FDelegateHandle StartedHandle,EndedHandle;
	FTSTicker::FDelegateHandle TimeoutHandle;
	void RestoreOverrides()
	{
		if (bPendingOverride && GEditor) GEditor->UserEditedPlayWorldURL=PreviousURL;
		bPendingOverride=false; PreviousURL.Reset();
		FEditorDelegates::PostPIEStarted.Remove(StartedHandle); StartedHandle.Reset();
		FEditorDelegates::EndPIE.Remove(EndedHandle); EndedHandle.Reset();
		FTSTicker::GetCoreTicker().RemoveTicker(TimeoutHandle); TimeoutHandle.Reset();
	}
	bool CanStart() { return GEditor && !GEditor->IsPlaySessionInProgress(); }
}

FRequestPlaySessionParams UProjectProject01TutorialEditorLibrary::MakePracticeRequest()
{
	// Never mutate/save the user's normal multi-client PIE settings.
	auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
	Settings->SetPlayNetMode(PIE_Standalone); Settings->SetPlayNumberOfClients(1);
	Settings->SetRunUnderOneProcess(true); Settings->bLaunchSeparateServer=false;
	FRequestPlaySessionParams Request;
	Request.EditorPlaySettings=Settings; Request.bAllowOnlineSubsystem=false;
	Request.SessionDestination=EPlaySessionDestinationType::InProcess;
	Request.WorldType=EPlaySessionWorldType::PlayInEditor;
	Request.GlobalMapOverride=ProjectTutorialEditor::Map;
	Request.GameModeOverride=AProjectProject01TutorialGameMode::StaticClass();
	return Request;
}

bool UProjectProject01TutorialEditorLibrary::StartPractice(const bool bMannequin)
{
	using namespace ProjectTutorialEditor;
	if (!CanStart() || !FPackageName::DoesPackageExist(Map)) return false;
	RestoreOverrides();
	PreviousURL=GEditor->UserEditedPlayWorldURL; bPendingOverride=true;
	// GlobalMapOverride loads an asset (not a URL). Keep role options in the PIE
	// URL until local login completes, then immediately restore the editor value.
	GEditor->UserEditedPlayWorldURL=FString(Map)+(bMannequin ? TEXT("?TutorialRole=Mannequin") : TEXT("?TutorialRole=Survivor"));
	StartedHandle=FEditorDelegates::PostPIEStarted.AddLambda([](bool){ RestoreOverrides(); });
	EndedHandle=FEditorDelegates::EndPIE.AddLambda([](bool){ RestoreOverrides(); });
	const double Deadline=FPlatformTime::Seconds()+60;
	TimeoutHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Deadline](float)
	{
		if (!GEditor || FPlatformTime::Seconds()>Deadline || !GEditor->IsPlaySessionInProgress())
		{ RestoreOverrides(); return false; }
		return true;
	}),0.1f);
	GEditor->RequestPlaySession(MakePracticeRequest());
	return true;
}

void UProjectProject01TutorialEditorLibrary::RegisterMenus()
{
	auto* Menu=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (!Menu) return;
	auto& Section=Menu->FindOrAddSection(TEXT("ProjectProject01Tutorial"));
	Section.Label=FText::FromString(TEXT("멀티 튜토리얼 테스트"));
	for (const bool bMannequin:{false,true})
		Section.AddMenuEntry(bMannequin ? TEXT("ProjectTutorialMannequin") : TEXT("ProjectTutorialSurvivor"),
			FText::FromString(bMannequin ? TEXT("마네킹 튜토리얼 테스트") : TEXT("생존자 튜토리얼 테스트")),
			FText::FromString(TEXT("서버·로그인 없이 1인 연습을 실행합니다. 기존 멀티 PIE 설정과 편집 중인 맵은 유지합니다.")),FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([bMannequin](){ StartPractice(bMannequin); }),
				FCanExecuteAction::CreateStatic(&ProjectTutorialEditor::CanStart)));
}

void UProjectProject01TutorialEditorLibrary::ShutdownPractice() { ProjectTutorialEditor::RestoreOverrides(); }

bool UProjectProject01TutorialEditorLibrary::PrepareNavigation(UWorld* World)
{
	if (!World || World->IsGameWorld()) return false;
	if (!FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		FNavigationSystem::AddNavigationSystemToWorld(*World,FNavigationSystemRunMode::EditorMode);
	auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav) return false;
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It) Nav->OnNavigationBoundsUpdated(*It);
	return Nav->GetDefaultNavDataInstance(FNavigationSystem::Create)!=nullptr;
}
