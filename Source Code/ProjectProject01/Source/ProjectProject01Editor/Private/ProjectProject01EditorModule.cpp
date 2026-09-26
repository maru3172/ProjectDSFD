// File: Source/ProjectProject01Editor/Private/ProjectProject01EditorModule.cpp
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Factories/DataTableFactory.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ProjectProject01TuningData.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "ProjectProject01Editor"

namespace ProjectProject01TuningEditor
{
	const TCHAR* const DataDirectory = TEXT("/Game/MyProject/Data");
	const TCHAR* const MannequinAssetName = TEXT("DT_AITuning");
	const TCHAR* const HelperAssetName = TEXT("DT_HelperTuning");

	FString GetCsvPath(const TCHAR* FileName)
	{
		return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MyProject"), TEXT("Data"), FileName);
	}

	void ShowResultNotification(const FString& Message, const bool bSuccess)
	{
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = bSuccess ? 5.0f : 9.0f;
		Info.bUseSuccessFailIcons = true;
		Info.Image = bSuccess
			? FAppStyle::Get().GetBrush(TEXT("Icons.Success"))
			: FAppStyle::Get().GetBrush(TEXT("Icons.Error"));
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	template <typename RowType>
	const TArray<FString>& GetVerticalFieldNames();

	template <>
	const TArray<FString>& GetVerticalFieldNames<FMannequinAITuningRow>()
	{
		static const TArray<FString> FieldNames =
		{
			TEXT("PlayerWalkSpeed"),
			TEXT("MannequinWalkSpeed"),
			TEXT("DirectChaseRadius"),
			TEXT("RoamingOuterRadius"),
			TEXT("PostPossessionCommandDurationSeconds"),
			TEXT("DirectChaseHalfAngleDegrees"),
			TEXT("DirectChaseSectorRadius"),
			TEXT("MannequinGatherRadius"),
			TEXT("RequiredMannequinCount"),
			TEXT("SurvivorVisionCheckIntervalSeconds"),
			TEXT("SurvivorVisionHalfAngleDegrees"),
			TEXT("DetectionMargin"),
			TEXT("VisionFovMarginMultiplier"),
			TEXT("MinBPM"),
			TEXT("MaxDistanceBPM"),
			TEXT("MaxEncounterBPM"),
			TEXT("MinEncounterBoost"),
			TEXT("MaxEncounterBoost"),
			TEXT("BPMDecayPerSecond"),
			TEXT("HeartbeatRange"),
			TEXT("RetentionRange"),
			TEXT("RecognitionHalfAngleDegrees"),
			TEXT("bUseRetentionRules"),
			TEXT("RetentionHalfAngleDegrees"),
			TEXT("LostSightGraceSeconds"),
			TEXT("SurpriseRearmDelay"),
			TEXT("SurpriseCooldown"),
			TEXT("bEnableRediscovery"),
			TEXT("VisionCheckInterval"),
			TEXT("MannequinRefreshInterval"),
			TEXT("bEnableHeartbeatLog"),
			TEXT("BPMLogThreshold")
		};
		return FieldNames;
	}

	template <>
	const TArray<FString>& GetVerticalFieldNames<FHelperTuningRow>()
	{
		static const TArray<FString> FieldNames =
		{
			TEXT("HelperWalkSpeed"),
			TEXT("FollowDistance"),
			TEXT("MinimumFollowSeparation"),
			TEXT("GuardSightRadius"),
			TEXT("GuardHalfAngleDegrees"),
			TEXT("GuardSearchHalfAngleDegrees"),
			TEXT("VisionDirectionChangeWindowSeconds"),
			TEXT("VisionDirectionChangeRequiredCount"),
			TEXT("VisionDirectionChangeThresholdDegrees"),
			TEXT("RepathInterval"),
			TEXT("RepathDistance"),
			TEXT("AcceptanceRadius"),
			TEXT("StuckTimeout"),
			TEXT("StuckWaitTime"),
			TEXT("ProgressDistance")
		};
		return FieldNames;
	}

	template <typename RowType>
	bool ConvertVerticalCsvToDataTableCsv(const FString& CsvPath, FString& OutDataTableCsv, FString& OutError)
	{
		FString SourceCsv;
		if (!FFileHelper::LoadFileToString(SourceCsv, *CsvPath))
		{
			OutError = FString::Printf(TEXT("CSV 파일을 읽지 못했습니다: %s"), *CsvPath);
			return false;
		}

		TArray<FString> Lines;
		SourceCsv.ParseIntoArrayLines(Lines, true);
		if (Lines.IsEmpty())
		{
			OutError = TEXT("CSV가 비어 있습니다. 첫 행은 Field,Value여야 합니다.");
			return false;
		}

		TArray<FString> HeaderColumns;
		Lines[0].ParseIntoArray(HeaderColumns, TEXT(","), false);
		if (HeaderColumns.Num() != 2 || HeaderColumns[0].TrimStartAndEnd() != TEXT("Field") ||
			HeaderColumns[1].TrimStartAndEnd() != TEXT("Value"))
		{
			OutError = TEXT("세로 CSV의 첫 행은 정확히 Field,Value여야 합니다.");
			return false;
		}

		const TArray<FString>& FieldNames = GetVerticalFieldNames<RowType>();
		TMap<FString, FString> FieldValues;
		for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
		{
			TArray<FString> Columns;
			Lines[LineIndex].ParseIntoArray(Columns, TEXT(","), false);
			if (Columns.Num() != 2)
			{
				OutError = FString::Printf(TEXT("%d번째 행은 Field,Value 두 열만 가져야 합니다."), LineIndex + 1);
				return false;
			}

			const FString FieldName = Columns[0].TrimStartAndEnd();
			const FString FieldValue = Columns[1].TrimStartAndEnd();
			if (FieldName.IsEmpty() || FieldValue.IsEmpty())
			{
				OutError = FString::Printf(TEXT("%d번째 행의 Field 또는 Value가 비어 있습니다."), LineIndex + 1);
				return false;
			}
			if (!FieldNames.Contains(FieldName))
			{
				OutError = FString::Printf(TEXT("%d번째 행의 Field를 인식하지 못했습니다: %s"), LineIndex + 1, *FieldName);
				return false;
			}
			if (FieldValues.Contains(FieldName))
			{
				OutError = FString::Printf(TEXT("Field가 중복되었습니다: %s"), *FieldName);
				return false;
			}
			FieldValues.Add(FieldName, FieldValue);
		}

		for (const FString& FieldName : FieldNames)
		{
			if (!FieldValues.Contains(FieldName))
			{
				OutError = FString::Printf(TEXT("필수 Field가 없습니다: %s"), *FieldName);
				return false;
			}
		}

		OutDataTableCsv = FString::Printf(TEXT("Name,%s\nDefault"), *FString::Join(FieldNames, TEXT(",")));
		for (const FString& FieldName : FieldNames)
		{
			OutDataTableCsv += TEXT(",");
			OutDataTableCsv += FieldValues.FindChecked(FieldName);
		}
		return true;
	}

	template <typename RowType>
	bool ParseAndValidateCsv(const FString& CsvPath, FString& OutCsvText, FString& OutError)
	{
		OutCsvText.Reset();
		OutError.Reset();

		if (!ConvertVerticalCsvToDataTableCsv<RowType>(CsvPath, OutCsvText, OutError))
		{
			return false;
		}

		UDataTable* TemporaryTable = NewObject<UDataTable>(GetTransientPackage());
		if (!IsValid(TemporaryTable))
		{
			OutError = TEXT("CSV 검증용 임시 DataTable을 만들지 못했습니다.");
			return false;
		}

		TemporaryTable->RowStruct = RowType::StaticStruct();
		const TArray<FString> ImportProblems = TemporaryTable->CreateTableFromCSVString(OutCsvText);
		if (!ImportProblems.IsEmpty())
		{
			OutError = FString::Printf(TEXT("CSV 열 또는 값 형식 오류: %s"), *FString::Join(ImportProblems, TEXT(" | ")));
			return false;
		}

		const RowType* DefaultRow = TemporaryTable->FindRow<RowType>(TEXT("Default"), TEXT("ProjectProject01 tuning validation"));
		if (DefaultRow == nullptr)
		{
			OutError = TEXT("세로 CSV를 DataTable Default 행으로 변환하지 못했습니다.");
			return false;
		}

		FString ValidationError;
		if (!DefaultRow->IsValidForApplication(ValidationError))
		{
			OutError = FString::Printf(TEXT("Default 행의 범위가 잘못되었습니다: %s"), *ValidationError);
			return false;
		}

		return true;
	}

	UDataTable* GetOrCreateDataTable(const TCHAR* AssetName, UScriptStruct* RowStruct, FString& OutError)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), DataDirectory, AssetName, AssetName);
		if (UDataTable* ExistingTable = LoadObject<UDataTable>(nullptr, *ObjectPath))
		{
			if (ExistingTable->GetRowStruct() != RowStruct)
			{
				OutError = FString::Printf(TEXT("기존 DataTable의 행 구조가 맞지 않습니다: %s"), *ObjectPath);
				return nullptr;
			}
			return ExistingTable;
		}

		UDataTableFactory* Factory = NewObject<UDataTableFactory>();
		if (!IsValid(Factory))
		{
			OutError = TEXT("DataTable Factory를 만들지 못했습니다.");
			return nullptr;
		}
		Factory->Struct = RowStruct;

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		UDataTable* NewTable = Cast<UDataTable>(AssetToolsModule.Get().CreateAsset(
			AssetName, DataDirectory, UDataTable::StaticClass(), Factory));
		if (!IsValid(NewTable))
		{
			OutError = FString::Printf(TEXT("DataTable 에셋을 만들지 못했습니다: %s/%s"), DataDirectory, AssetName);
		}
		return NewTable;
	}

	bool ImportCsvIntoTable(UDataTable* Table, const FString& CsvText, FString& OutError)
	{
		if (!IsValid(Table))
		{
			OutError = TEXT("대상 DataTable이 유효하지 않습니다.");
			return false;
		}

		const TArray<FString> ImportProblems = Table->CreateTableFromCSVString(CsvText);
		if (!ImportProblems.IsEmpty())
		{
			OutError = FString::Printf(TEXT("DataTable 반영 실패: %s"), *FString::Join(ImportProblems, TEXT(" | ")));
			return false;
		}

		Table->MarkPackageDirty();
		return true;
	}
}

class FProjectProject01EditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FProjectProject01EditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		if (Menu == nullptr)
		{
			ensureMsgf(false, TEXT("ProjectProject01 tuning menu could not extend the Tools menu."));
			return;
		}

		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("ProjectProject01Tuning"));
		Section.Label = LOCTEXT("TuningSection", "ProjectProject01 Tuning");
		Section.AddMenuEntry(
			TEXT("ProjectProject01ReimportTuning"),
			LOCTEXT("ReimportTuning", "Reimport and Validate Tuning DataTables"),
			LOCTEXT("ReimportTuningTooltip", "Validate both ProjectProject01 tuning CSV files before updating either DataTable."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FProjectProject01EditorModule::ReimportAndValidate)));
		Section.AddMenuEntry(
			TEXT("ProjectProject01ApplyPieTuning"),
			LOCTEXT("ApplyPieTuning", "Apply Tuning to PIE"),
			LOCTEXT("ApplyPieTuningTooltip", "Apply the validated DataTables to the server/standalone PIE world."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FProjectProject01EditorModule::ApplyToPie)));
	}

	void ReimportAndValidate()
	{
		using namespace ProjectProject01TuningEditor;

		FString MannequinCsv;
		FString HelperCsv;
		FString Error;
		if (!ParseAndValidateCsv<FMannequinAITuningRow>(GetCsvPath(TEXT("AITuning.csv")), MannequinCsv, Error) ||
			!ParseAndValidateCsv<FHelperTuningRow>(GetCsvPath(TEXT("HelperTuning.csv")), HelperCsv, Error))
		{
			UE_LOG(LogProjectProject01Tuning, Error, TEXT("ProjectProject01 tuning reimport rejected: %s"), *Error);
			ShowResultNotification(Error, false);
			return;
		}

		UDataTable* MannequinTable = GetOrCreateDataTable(MannequinAssetName, FMannequinAITuningRow::StaticStruct(), Error);
		UDataTable* HelperTable = IsValid(MannequinTable)
			? GetOrCreateDataTable(HelperAssetName, FHelperTuningRow::StaticStruct(), Error)
			: nullptr;
		if (!IsValid(MannequinTable) || !IsValid(HelperTable))
		{
			UE_LOG(LogProjectProject01Tuning, Error, TEXT("ProjectProject01 tuning DataTable setup failed: %s"), *Error);
			ShowResultNotification(Error, false);
			return;
		}

		if (!ImportCsvIntoTable(MannequinTable, MannequinCsv, Error) || !ImportCsvIntoTable(HelperTable, HelperCsv, Error))
		{
			UE_LOG(LogProjectProject01Tuning, Error, TEXT("ProjectProject01 tuning DataTable import failed: %s"), *Error);
			ShowResultNotification(Error, false);
			return;
		}

		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(MannequinTable->GetOutermost());
		PackagesToSave.Add(HelperTable->GetOutermost());
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false);
		const bool bAppliedToActivePie = ApplyToPieInternal(false);
		UE_LOG(LogProjectProject01Tuning, Log, TEXT("ProjectProject01 tuning DataTables reimported and saved."));
		ShowResultNotification(
			bAppliedToActivePie
				? TEXT("Tuning CSV tables saved and applied to active PIE.")
				: TEXT("Tuning CSV tables saved. New PIE sessions apply them automatically."),
			true);
	}

	void ApplyToPie()
	{
		ApplyToPieInternal(true);
	}

	bool ApplyToPieInternal(const bool bShowFailureNotification)
	{
		using namespace ProjectProject01TuningEditor;

		if (!IsValid(GEngine))
		{
			if (bShowFailureNotification)
			{
				ShowResultNotification(TEXT("Engine instance를 찾지 못했습니다."), false);
			}
			return false;
		}

		bool bAppliedToAuthorityWorld = false;
		FString LastError;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!IsValid(World) || World->WorldType != EWorldType::PIE || World->GetNetMode() == NM_Client)
			{
				continue;
			}

			UProjectProject01TuningSubsystem* TuningSubsystem = World->GetSubsystem<UProjectProject01TuningSubsystem>();
			if (!IsValid(TuningSubsystem))
			{
				LastError = TEXT("PIE tuning subsystem을 찾지 못했습니다.");
				continue;
			}

			FString ApplyError;
			if (TuningSubsystem->ApplyToCurrentWorld(ApplyError))
			{
				bAppliedToAuthorityWorld = true;
			}
			else
			{
				LastError = ApplyError;
			}
		}

		if (bAppliedToAuthorityWorld)
		{
			if (bShowFailureNotification)
			{
				ShowResultNotification(TEXT("Validated tuning applied to PIE authority world."), true);
			}
			return true;
		}

		if (LastError.IsEmpty())
		{
			LastError = TEXT("실행 중인 Standalone 또는 서버 PIE 월드를 찾지 못했습니다.");
		}
		if (bShowFailureNotification)
		{
			UE_LOG(LogProjectProject01Tuning, Warning, TEXT("ProjectProject01 tuning was not applied: %s"), *LastError);
			ShowResultNotification(LastError, false);
		}
		return false;
	}
};

IMPLEMENT_MODULE(FProjectProject01EditorModule, ProjectProject01Editor)

#undef LOCTEXT_NAMESPACE
