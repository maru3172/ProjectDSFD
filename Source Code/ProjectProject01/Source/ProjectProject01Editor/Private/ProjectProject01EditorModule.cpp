// File: Source/ProjectProject01Editor/Private/ProjectProject01EditorModule.cpp
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Factories/DataTableFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionStep.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ProjectProject01FrontendGameMode.h"
#include "ProjectProject01TuningData.h"
#include "Serialization/Csv/CsvParser.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "ProjectProject01Editor"

void RegisterProjectProject01DiagnosticsMenus();

namespace ProjectProject01TuningEditor
{
	const TCHAR* const DataDirectory = TEXT("/Game/MyProject/Data");
	const TCHAR* const PlayerAssetName = TEXT("DT_PlayerTuning");
	const TCHAR* const MannequinAssetName = TEXT("DT_AITuning");
	const TCHAR* const HelperAssetName = TEXT("DT_HelperTuning");
	const TCHAR* const HeartbeatVFXDirectory = TEXT("/Game/MyProject/VFX");
	const TCHAR* const HeartbeatVFXAssetName = TEXT("M_HeartbeatScreenVFX");
	const TCHAR* const HeartbeatNoiseVFXAssetName = TEXT("M_HeartbeatScreenNoiseVFX");

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
	const TArray<FString>& GetVerticalFieldNames<FPlayerTuningRow>()
	{
		static const TArray<FString> FieldNames =
		{
			TEXT("PlayerWalkSpeed"),
			TEXT("PlayerSprintSpeed"),
			TEXT("MaxStamina"),
			TEXT("StaminaDrainPerSecond"),
			TEXT("StaminaRecoveryPerSecond"),
			TEXT("BaseVisionRange"),
			TEXT("BaseVisionHalfAngleDegrees"),
			TEXT("BaseMinBPM"),
			TEXT("BaseMaxBPM"),
			TEXT("EncounterVisionRange"),
			TEXT("EncounterVisionHalfAngleDegrees"),
			TEXT("EncounterMinBPM"),
			TEXT("EncounterMaxBPM"),
			TEXT("EncounterDecayPerSecond"),
			TEXT("EncounterMemorySeconds"),
			TEXT("VisionCheckInterval"),
			TEXT("MannequinRefreshInterval"),
			TEXT("bEnableHeartbeatLog"),
			TEXT("BPMLogThreshold"),
			TEXT("HeartbeatRange"),
			TEXT("bEnableHeartbeatVFX"),
			TEXT("HeartbeatVFXDensityCountForMax"),
			TEXT("HeartbeatVFXMaxOpacity"),
			TEXT("HeartbeatVFXMaxNoiseIntensity"),
			TEXT("HeartbeatVFXMinNoiseSpeed"),
			TEXT("HeartbeatVFXMaxNoiseSpeed"),
			TEXT("HeartbeatVFXMinNoiseFrequency"),
			TEXT("HeartbeatVFXMaxNoiseFrequency"),
			TEXT("HeartbeatVFXMaxDistortionAmount"),
			TEXT("HeartbeatVFXMinDistortionSpeed"),
			TEXT("HeartbeatVFXMaxDistortionSpeed"),
			TEXT("HeartbeatVFXMinDistortionFrequency"),
			TEXT("HeartbeatVFXMaxDistortionFrequency"),
			TEXT("HeartbeatVFXBlendInSpeed"),
			TEXT("HeartbeatVFXBlendOutSpeed")
		};
		return FieldNames;
	}

	template <>
	const TArray<FString>& GetVerticalFieldNames<FMannequinAITuningRow>()
	{
		static const TArray<FString> FieldNames =
		{
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
			TEXT("VisionFovMarginMultiplier")
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
			TEXT("StaminaVisionDrainMultiplier"),
			TEXT("StaminaVisionRecoveryMultiplier"),
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

		FCsvParser Parser(MoveTemp(SourceCsv));
		const FCsvParser::FRows& Rows = Parser.GetRows();
		if (Rows.IsEmpty())
		{
			OutError = TEXT("CSV가 비어 있습니다. 첫 행은 Field,Value,Note여야 합니다.");
			return false;
		}

		const FCsvParser::FRows::ElementType& HeaderColumns = Rows[0];
		if (HeaderColumns.Num() != 3 || FString(HeaderColumns[0]).TrimStartAndEnd() != TEXT("Field") ||
			FString(HeaderColumns[1]).TrimStartAndEnd() != TEXT("Value") ||
			FString(HeaderColumns[2]).TrimStartAndEnd() != TEXT("Note"))
		{
			OutError = TEXT("세로 CSV의 첫 행은 정확히 Field,Value,Note여야 합니다.");
			return false;
		}

		const TArray<FString>& FieldNames = GetVerticalFieldNames<RowType>();
		TMap<FString, FString> FieldValues;
		for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
		{
			const FCsvParser::FRows::ElementType& Columns = Rows[RowIndex];
			bool bBlankRow = true;
			for (const TCHAR* Column : Columns)
			{
				if (!FString(Column).TrimStartAndEnd().IsEmpty())
				{
					bBlankRow = false;
					break;
				}
			}
			if (bBlankRow)
			{
				continue;
			}

			if (Columns.Num() != 3)
			{
				OutError = FString::Printf(TEXT("%d번째 행은 Field,Value,Note 세 열을 가져야 합니다."), RowIndex + 1);
				return false;
			}

			const FString FieldName = FString(Columns[0]).TrimStartAndEnd();
			const FString FieldValue = FString(Columns[1]).TrimStartAndEnd();
			if (FieldName.IsEmpty() || FieldValue.IsEmpty())
			{
				OutError = FString::Printf(TEXT("%d번째 행의 Field 또는 Value가 비어 있습니다."), RowIndex + 1);
				return false;
			}
			if (!FieldNames.Contains(FieldName))
			{
				OutError = FString::Printf(TEXT("%d번째 행의 Field를 인식하지 못했습니다: %s"), RowIndex + 1, *FieldName);
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

	bool ReimportAndSaveTuningDataTables(FString& OutError)
	{
		FString PlayerCsv;
		FString MannequinCsv;
		FString HelperCsv;
		if (!ParseAndValidateCsv<FPlayerTuningRow>(GetCsvPath(TEXT("Player.csv")), PlayerCsv, OutError) ||
			!ParseAndValidateCsv<FMannequinAITuningRow>(GetCsvPath(TEXT("AITuning.csv")), MannequinCsv, OutError) ||
			!ParseAndValidateCsv<FHelperTuningRow>(GetCsvPath(TEXT("HelperTuning.csv")), HelperCsv, OutError))
		{
			return false;
		}

		UDataTable* PlayerTable = GetOrCreateDataTable(PlayerAssetName, FPlayerTuningRow::StaticStruct(), OutError);
		UDataTable* MannequinTable = IsValid(PlayerTable)
			? GetOrCreateDataTable(MannequinAssetName, FMannequinAITuningRow::StaticStruct(), OutError)
			: nullptr;
		UDataTable* HelperTable = IsValid(PlayerTable) && IsValid(MannequinTable)
			? GetOrCreateDataTable(HelperAssetName, FHelperTuningRow::StaticStruct(), OutError)
			: nullptr;
		if (!IsValid(PlayerTable) || !IsValid(MannequinTable) || !IsValid(HelperTable))
		{
			return false;
		}

		if (!ImportCsvIntoTable(PlayerTable, PlayerCsv, OutError) ||
			!ImportCsvIntoTable(MannequinTable, MannequinCsv, OutError) ||
			!ImportCsvIntoTable(HelperTable, HelperCsv, OutError))
		{
			return false;
		}

		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(PlayerTable->GetOutermost());
		PackagesToSave.Add(MannequinTable->GetOutermost());
		PackagesToSave.Add(HelperTable->GetOutermost());
		if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false))
		{
			OutError = TEXT("Tuning DataTable 패키지를 저장하지 못했습니다.");
			return false;
		}

		OutError.Reset();
		return true;
	}

	template <typename ExpressionType>
	ExpressionType* CreateExpression(UMaterial* Material, const int32 X, const int32 Y)
	{
		return Cast<ExpressionType>(UMaterialEditingLibrary::CreateMaterialExpression(
			Material, ExpressionType::StaticClass(), X, Y));
	}

	UMaterialExpressionScalarParameter* CreateScalarParameter(
		UMaterial* Material, const FName Name, const float DefaultValue, const int32 X, const int32 Y)
	{
		UMaterialExpressionScalarParameter* Parameter = CreateExpression<UMaterialExpressionScalarParameter>(Material, X, Y);
		if (IsValid(Parameter))
		{
			Parameter->ParameterName = Name;
			Parameter->DefaultValue = DefaultValue;
		}
		return Parameter;
	}

	bool Connect(UMaterialExpression* From, const TCHAR* FromOutput, UMaterialExpression* To, const TCHAR* ToInput)
	{
		const bool bConnected = IsValid(From) && IsValid(To) &&
			UMaterialEditingLibrary::ConnectMaterialExpressions(From, FromOutput, To, ToInput);
		if (!bConnected)
		{
			UE_LOG(LogProjectProject01Tuning, Error,
				TEXT("Heartbeat VFX material connection failed: %s.%s -> %s.%s"),
				*GetNameSafe(From), FromOutput, *GetNameSafe(To), ToInput);
		}
		return bConnected;
	}

	bool CreateOrRepairHeartbeatNoiseVFXMaterial(FString& OutError)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"),
			HeartbeatVFXDirectory, HeartbeatNoiseVFXAssetName, HeartbeatNoiseVFXAssetName);
		UMaterial* Material = LoadObject<UMaterial>(nullptr, *ObjectPath);
		if (!IsValid(Material))
		{
			UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
			if (!IsValid(Factory))
			{
				OutError = TEXT("Heartbeat VFX Material Factory를 만들지 못했습니다.");
				return false;
			}
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			Material = Cast<UMaterial>(AssetToolsModule.Get().CreateAsset(
				HeartbeatNoiseVFXAssetName, HeartbeatVFXDirectory, UMaterial::StaticClass(), Factory));
		}
		if (!IsValid(Material))
		{
			OutError = FString::Printf(TEXT("Heartbeat VFX 머티리얼을 만들지 못했습니다: %s"), *ObjectPath);
			return false;
		}

		Material->Modify();
		Material->MaterialDomain = MD_PostProcess;
		Material->BlendableLocation = BL_SceneColorAfterTonemapping;
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);

		UMaterialExpressionScreenPosition* ScreenPosition = CreateExpression<UMaterialExpressionScreenPosition>(Material, -1900, -50);
		UMaterialExpressionComponentMask* ScreenU = CreateExpression<UMaterialExpressionComponentMask>(Material, -1700, -160);
		UMaterialExpressionComponentMask* ScreenV = CreateExpression<UMaterialExpressionComponentMask>(Material, -1700, 40);
		if (IsValid(ScreenU)) { ScreenU->R = true; ScreenU->G = false; ScreenU->B = false; ScreenU->A = false; }
		if (IsValid(ScreenV)) { ScreenV->R = false; ScreenV->G = true; ScreenV->B = false; ScreenV->A = false; }
		UMaterialExpressionAppendVector* OriginalUV = CreateExpression<UMaterialExpressionAppendVector>(Material, -1400, -220);

		UMaterialExpressionTime* Time = CreateExpression<UMaterialExpressionTime>(Material, -1900, 500);
		UMaterialExpressionScalarParameter* EffectOpacity = CreateScalarParameter(Material, TEXT("EffectOpacity"), 0.0f, 600, 500);
		UMaterialExpressionScalarParameter* NoiseIntensity = CreateScalarParameter(Material, TEXT("NoiseIntensity"), 0.0f, -700, 900);
		UMaterialExpressionScalarParameter* NoiseSpeed = CreateScalarParameter(Material, TEXT("NoiseSpeed"), 0.75f, -1700, 850);
		UMaterialExpressionScalarParameter* NoiseFrequency = CreateScalarParameter(Material, TEXT("NoiseFrequency"), 80.0f, -1700, 650);

		// 화면 셀은 고정하고 시간 프레임마다 해시 결과만 교체하는 흑백 TV 스노우.
		// 서로 다른 공간/시간 계수를 사용하므로 패턴이 좌우로 평행 이동하지 않는다.
		UMaterialExpressionMultiply* NoiseU = CreateExpression<UMaterialExpressionMultiply>(Material, -1400, 650);
		UMaterialExpressionMultiply* NoiseV = CreateExpression<UMaterialExpressionMultiply>(Material, -1400, 760);
		UMaterialExpressionFloor* NoiseUCell = CreateExpression<UMaterialExpressionFloor>(Material, -1200, 620);
		UMaterialExpressionFloor* NoiseVCell = CreateExpression<UMaterialExpressionFloor>(Material, -1200, 730);
		UMaterialExpressionMultiply* NoiseUScale = CreateExpression<UMaterialExpressionMultiply>(Material, -1000, 620);
		if (IsValid(NoiseUScale)) { NoiseUScale->ConstB = 12.9898f; }
		UMaterialExpressionMultiply* NoiseVScale = CreateExpression<UMaterialExpressionMultiply>(Material, -1000, 760);
		if (IsValid(NoiseVScale)) { NoiseVScale->ConstB = 78.233f; }
		UMaterialExpressionAdd* NoiseSpatial = CreateExpression<UMaterialExpressionAdd>(Material, -900, 680);
		UMaterialExpressionMultiply* NoiseTemporal = CreateExpression<UMaterialExpressionMultiply>(Material, -1400, 950);
		UMaterialExpressionMultiply* NoiseFrameRate = CreateExpression<UMaterialExpressionMultiply>(Material, -1150, 950);
		if (IsValid(NoiseFrameRate)) { NoiseFrameRate->ConstB = 12.0f; }
		UMaterialExpressionFloor* NoiseFrame = CreateExpression<UMaterialExpressionFloor>(Material, -950, 950);
		UMaterialExpressionMultiply* NoiseFrameHashScale = CreateExpression<UMaterialExpressionMultiply>(Material, -750, 950);
		if (IsValid(NoiseFrameHashScale)) { NoiseFrameHashScale->ConstB = 37.719f; }
		UMaterialExpressionAdd* NoisePhase = CreateExpression<UMaterialExpressionAdd>(Material, -650, 700);
		UMaterialExpressionSine* NoiseSine = CreateExpression<UMaterialExpressionSine>(Material, -450, 620);
		UMaterialExpressionMultiply* NoiseHashScale = CreateExpression<UMaterialExpressionMultiply>(Material, -250, 620);
		if (IsValid(NoiseHashScale)) { NoiseHashScale->ConstB = 43758.5453f; }
		UMaterialExpressionFrac* NoiseFrac = CreateExpression<UMaterialExpressionFrac>(Material, -50, 620);
		UMaterialExpressionStep* NoiseBinary = CreateExpression<UMaterialExpressionStep>(Material, 150, 620);
		if (IsValid(NoiseBinary)) { NoiseBinary->ConstY = 0.5f; }
		UMaterialExpressionSubtract* NoiseCentered = CreateExpression<UMaterialExpressionSubtract>(Material, 350, 620);
		if (IsValid(NoiseCentered)) { NoiseCentered->ConstB = 0.5f; }
		UMaterialExpressionMultiply* NoiseSigned = CreateExpression<UMaterialExpressionMultiply>(Material, 550, 620);
		if (IsValid(NoiseSigned)) { NoiseSigned->ConstB = 2.0f; }

		UMaterialExpressionAppendVector* NoiseRG = CreateExpression<UMaterialExpressionAppendVector>(Material, 750, 700);
		UMaterialExpressionAppendVector* NoiseRGB = CreateExpression<UMaterialExpressionAppendVector>(Material, 950, 760);
		UMaterialExpressionMultiply* NoiseValue = CreateExpression<UMaterialExpressionMultiply>(Material, 1150, 760);

		UMaterialExpressionSceneTexture* OriginalScene = CreateExpression<UMaterialExpressionSceneTexture>(Material, 100, -320);
		if (IsValid(OriginalScene)) { OriginalScene->SceneTextureId = PPI_PostProcessInput0; OriginalScene->bFiltered = true; }
		UMaterialExpressionComponentMask* OriginalRGB = CreateExpression<UMaterialExpressionComponentMask>(Material, 350, -320);
		if (IsValid(OriginalRGB)) { OriginalRGB->R = true; OriginalRGB->G = true; OriginalRGB->B = true; OriginalRGB->A = false; }
		UMaterialExpressionAdd* OriginalWithNoise = CreateExpression<UMaterialExpressionAdd>(Material, 1400, 120);
		UMaterialExpressionLinearInterpolate* FinalBlend = CreateExpression<UMaterialExpressionLinearInterpolate>(Material, 1650, 50);

		bool bConnected = true;
		bConnected &= Connect(ScreenPosition, TEXT(""), ScreenU, TEXT(""));
		bConnected &= Connect(ScreenPosition, TEXT(""), ScreenV, TEXT(""));
		bConnected &= Connect(ScreenU, TEXT(""), OriginalUV, TEXT("A"));
		bConnected &= Connect(ScreenV, TEXT(""), OriginalUV, TEXT("B"));

		bConnected &= Connect(ScreenU, TEXT(""), NoiseU, TEXT("A"));
		bConnected &= Connect(NoiseFrequency, TEXT(""), NoiseU, TEXT("B"));
		bConnected &= Connect(ScreenV, TEXT(""), NoiseV, TEXT("A"));
		bConnected &= Connect(NoiseFrequency, TEXT(""), NoiseV, TEXT("B"));
		bConnected &= Connect(NoiseU, TEXT(""), NoiseUCell, TEXT(""));
		bConnected &= Connect(NoiseV, TEXT(""), NoiseVCell, TEXT(""));
		bConnected &= Connect(NoiseUCell, TEXT(""), NoiseUScale, TEXT("A"));
		bConnected &= Connect(NoiseVCell, TEXT(""), NoiseVScale, TEXT("A"));
		bConnected &= Connect(NoiseUScale, TEXT(""), NoiseSpatial, TEXT("A"));
		bConnected &= Connect(NoiseVScale, TEXT(""), NoiseSpatial, TEXT("B"));
		bConnected &= Connect(Time, TEXT(""), NoiseTemporal, TEXT("A"));
		bConnected &= Connect(NoiseSpeed, TEXT(""), NoiseTemporal, TEXT("B"));
		bConnected &= Connect(NoiseTemporal, TEXT(""), NoiseFrameRate, TEXT("A"));
		bConnected &= Connect(NoiseFrameRate, TEXT(""), NoiseFrame, TEXT(""));
		bConnected &= Connect(NoiseFrame, TEXT(""), NoiseFrameHashScale, TEXT("A"));
		bConnected &= Connect(NoiseSpatial, TEXT(""), NoisePhase, TEXT("A"));
		bConnected &= Connect(NoiseFrameHashScale, TEXT(""), NoisePhase, TEXT("B"));
		bConnected &= Connect(NoisePhase, TEXT(""), NoiseSine, TEXT(""));
		bConnected &= Connect(NoiseSine, TEXT(""), NoiseHashScale, TEXT("A"));
		bConnected &= Connect(NoiseHashScale, TEXT(""), NoiseFrac, TEXT(""));
		bConnected &= Connect(NoiseFrac, TEXT(""), NoiseBinary, TEXT("X"));
		bConnected &= Connect(NoiseBinary, TEXT(""), NoiseCentered, TEXT("A"));
		bConnected &= Connect(NoiseCentered, TEXT(""), NoiseSigned, TEXT("A"));

		bConnected &= Connect(NoiseSigned, TEXT(""), NoiseRG, TEXT("A"));
		bConnected &= Connect(NoiseSigned, TEXT(""), NoiseRG, TEXT("B"));
		bConnected &= Connect(NoiseRG, TEXT(""), NoiseRGB, TEXT("A"));
		bConnected &= Connect(NoiseSigned, TEXT(""), NoiseRGB, TEXT("B"));
		bConnected &= Connect(NoiseRGB, TEXT(""), NoiseValue, TEXT("A"));
		bConnected &= Connect(NoiseIntensity, TEXT(""), NoiseValue, TEXT("B"));

		bConnected &= Connect(OriginalUV, TEXT(""), OriginalScene, TEXT(""));
		bConnected &= Connect(OriginalScene, TEXT(""), OriginalRGB, TEXT(""));
		bConnected &= Connect(OriginalRGB, TEXT(""), OriginalWithNoise, TEXT("A"));
		bConnected &= Connect(NoiseValue, TEXT(""), OriginalWithNoise, TEXT("B"));
		bConnected &= Connect(OriginalRGB, TEXT(""), FinalBlend, TEXT("A"));
		bConnected &= Connect(OriginalWithNoise, TEXT(""), FinalBlend, TEXT("B"));
		bConnected &= Connect(EffectOpacity, TEXT(""), FinalBlend, TEXT("Alpha"));
		bConnected &= UMaterialEditingLibrary::ConnectMaterialProperty(FinalBlend, TEXT(""), MP_EmissiveColor);
		if (!bConnected)
		{
			OutError = TEXT("Heartbeat VFX 머티리얼 노드 연결에 실패했습니다.");
			return false;
		}

		const TArray<FString> CompileErrors = UMaterialEditingLibrary::RecompileMaterial(Material);
		if (!CompileErrors.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Heartbeat VFX 머티리얼 컴파일 오류: %s"), *FString::Join(CompileErrors, TEXT(" | ")));
			return false;
		}

		Material->MarkPackageDirty();
		TArray<UPackage*> PackagesToSave{ Material->GetOutermost() };
		if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false))
		{
			OutError = TEXT("Heartbeat VFX 머티리얼 패키지를 저장하지 못했습니다.");
			return false;
		}
		OutError.Reset();
		return true;
	}

	bool CreateOrRepairHeartbeatDistortionVFXMaterial(FString& OutError)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"),
			HeartbeatVFXDirectory, HeartbeatVFXAssetName, HeartbeatVFXAssetName);
		UMaterial* Material = LoadObject<UMaterial>(nullptr, *ObjectPath);
		if (!IsValid(Material))
		{
			UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
			if (!IsValid(Factory))
			{
				OutError = TEXT("Heartbeat distortion VFX Material Factory를 만들지 못했습니다.");
				return false;
			}
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			Material = Cast<UMaterial>(AssetToolsModule.Get().CreateAsset(
				HeartbeatVFXAssetName, HeartbeatVFXDirectory, UMaterial::StaticClass(), Factory));
		}
		if (!IsValid(Material))
		{
			OutError = FString::Printf(TEXT("Heartbeat distortion VFX 머티리얼을 만들지 못했습니다: %s"), *ObjectPath);
			return false;
		}

		Material->Modify();
		Material->MaterialDomain = MD_PostProcess;
		Material->BlendableLocation = BL_SceneColorAfterTonemapping;
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);

		UMaterialExpressionScreenPosition* ScreenPosition = CreateExpression<UMaterialExpressionScreenPosition>(Material, -1500, -100);
		UMaterialExpressionComponentMask* ScreenU = CreateExpression<UMaterialExpressionComponentMask>(Material, -1300, -180);
		UMaterialExpressionComponentMask* ScreenV = CreateExpression<UMaterialExpressionComponentMask>(Material, -1300, 20);
		if (IsValid(ScreenU)) { ScreenU->R = true; ScreenU->G = false; ScreenU->B = false; ScreenU->A = false; }
		if (IsValid(ScreenV)) { ScreenV->R = false; ScreenV->G = true; ScreenV->B = false; ScreenV->A = false; }
		UMaterialExpressionAppendVector* OriginalUV = CreateExpression<UMaterialExpressionAppendVector>(Material, -1050, -250);
		UMaterialExpressionTime* Time = CreateExpression<UMaterialExpressionTime>(Material, -1500, 420);
		UMaterialExpressionScalarParameter* EffectOpacity = CreateScalarParameter(Material, TEXT("EffectOpacity"), 0.0f, 850, 350);
		UMaterialExpressionScalarParameter* DistortionAmount = CreateScalarParameter(Material, TEXT("DistortionAmount"), 0.0f, -450, 260);
		UMaterialExpressionScalarParameter* DistortionSpeed = CreateScalarParameter(Material, TEXT("DistortionSpeed"), 0.35f, -1250, 440);
		UMaterialExpressionScalarParameter* DistortionFrequency = CreateScalarParameter(Material, TEXT("DistortionFrequency"), 6.0f, -1250, 240);

		UMaterialExpressionMultiply* DistortionSpatial = CreateExpression<UMaterialExpressionMultiply>(Material, -1000, 100);
		UMaterialExpressionMultiply* DistortionTemporal = CreateExpression<UMaterialExpressionMultiply>(Material, -1000, 360);
		UMaterialExpressionAdd* DistortionPhase = CreateExpression<UMaterialExpressionAdd>(Material, -750, 220);
		UMaterialExpressionSine* DistortionWave = CreateExpression<UMaterialExpressionSine>(Material, -550, 220);
		UMaterialExpressionMultiply* DistortionOffset = CreateExpression<UMaterialExpressionMultiply>(Material, -300, 100);
		UMaterialExpressionAdd* DistortedU = CreateExpression<UMaterialExpressionAdd>(Material, -50, -100);
		UMaterialExpressionAppendVector* DistortedUV = CreateExpression<UMaterialExpressionAppendVector>(Material, 150, -20);

		UMaterialExpressionSceneTexture* OriginalScene = CreateExpression<UMaterialExpressionSceneTexture>(Material, 350, -300);
		UMaterialExpressionSceneTexture* DistortedScene = CreateExpression<UMaterialExpressionSceneTexture>(Material, 350, 20);
		if (IsValid(OriginalScene)) { OriginalScene->SceneTextureId = PPI_PostProcessInput0; OriginalScene->bFiltered = true; }
		if (IsValid(DistortedScene)) { DistortedScene->SceneTextureId = PPI_PostProcessInput0; DistortedScene->bFiltered = true; }
		UMaterialExpressionComponentMask* OriginalRGB = CreateExpression<UMaterialExpressionComponentMask>(Material, 550, -300);
		UMaterialExpressionComponentMask* DistortedRGB = CreateExpression<UMaterialExpressionComponentMask>(Material, 550, 20);
		if (IsValid(OriginalRGB)) { OriginalRGB->R = true; OriginalRGB->G = true; OriginalRGB->B = true; OriginalRGB->A = false; }
		if (IsValid(DistortedRGB)) { DistortedRGB->R = true; DistortedRGB->G = true; DistortedRGB->B = true; DistortedRGB->A = false; }
		UMaterialExpressionLinearInterpolate* FinalBlend = CreateExpression<UMaterialExpressionLinearInterpolate>(Material, 1050, 20);

		bool bConnected = true;
		bConnected &= Connect(ScreenPosition, TEXT(""), ScreenU, TEXT(""));
		bConnected &= Connect(ScreenPosition, TEXT(""), ScreenV, TEXT(""));
		bConnected &= Connect(ScreenU, TEXT(""), OriginalUV, TEXT("A"));
		bConnected &= Connect(ScreenV, TEXT(""), OriginalUV, TEXT("B"));
		bConnected &= Connect(ScreenV, TEXT(""), DistortionSpatial, TEXT("A"));
		bConnected &= Connect(DistortionFrequency, TEXT(""), DistortionSpatial, TEXT("B"));
		bConnected &= Connect(Time, TEXT(""), DistortionTemporal, TEXT("A"));
		bConnected &= Connect(DistortionSpeed, TEXT(""), DistortionTemporal, TEXT("B"));
		bConnected &= Connect(DistortionSpatial, TEXT(""), DistortionPhase, TEXT("A"));
		bConnected &= Connect(DistortionTemporal, TEXT(""), DistortionPhase, TEXT("B"));
		bConnected &= Connect(DistortionPhase, TEXT(""), DistortionWave, TEXT(""));
		bConnected &= Connect(DistortionWave, TEXT(""), DistortionOffset, TEXT("A"));
		bConnected &= Connect(DistortionAmount, TEXT(""), DistortionOffset, TEXT("B"));
		bConnected &= Connect(ScreenU, TEXT(""), DistortedU, TEXT("A"));
		bConnected &= Connect(DistortionOffset, TEXT(""), DistortedU, TEXT("B"));
		bConnected &= Connect(DistortedU, TEXT(""), DistortedUV, TEXT("A"));
		bConnected &= Connect(ScreenV, TEXT(""), DistortedUV, TEXT("B"));
		bConnected &= Connect(OriginalUV, TEXT(""), OriginalScene, TEXT(""));
		bConnected &= Connect(DistortedUV, TEXT(""), DistortedScene, TEXT(""));
		bConnected &= Connect(OriginalScene, TEXT(""), OriginalRGB, TEXT(""));
		bConnected &= Connect(DistortedScene, TEXT(""), DistortedRGB, TEXT(""));
		bConnected &= Connect(OriginalRGB, TEXT(""), FinalBlend, TEXT("A"));
		bConnected &= Connect(DistortedRGB, TEXT(""), FinalBlend, TEXT("B"));
		bConnected &= Connect(EffectOpacity, TEXT(""), FinalBlend, TEXT("Alpha"));
		bConnected &= UMaterialEditingLibrary::ConnectMaterialProperty(FinalBlend, TEXT(""), MP_EmissiveColor);
		if (!bConnected)
		{
			OutError = TEXT("Heartbeat distortion VFX 머티리얼 노드 연결에 실패했습니다.");
			return false;
		}

		const TArray<FString> CompileErrors = UMaterialEditingLibrary::RecompileMaterial(Material);
		if (!CompileErrors.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Heartbeat distortion VFX 머티리얼 컴파일 오류: %s"),
				*FString::Join(CompileErrors, TEXT(" | ")));
			return false;
		}

		Material->MarkPackageDirty();
		TArray<UPackage*> PackagesToSave{ Material->GetOutermost() };
		if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false))
		{
			OutError = TEXT("Heartbeat distortion VFX 머티리얼 패키지를 저장하지 못했습니다.");
			return false;
		}
		OutError.Reset();
		return true;
	}

	bool CreateOrRepairHeartbeatVFXMaterial(FString& OutError)
	{
		return CreateOrRepairHeartbeatDistortionVFXMaterial(OutError) &&
			CreateOrRepairHeartbeatNoiseVFXMaterial(OutError);
	}

	void RunHeartbeatVFXMaterialCommand()
	{
		FString Error;
		const bool bSuccess = CreateOrRepairHeartbeatVFXMaterial(Error);
		if (bSuccess)
		{
			UE_LOG(LogProjectProject01Tuning, Log,
				TEXT("Heartbeat distortion and archived noise VFX materials created, compiled, and saved."));
		}
		else
		{
			UE_LOG(LogProjectProject01Tuning, Error, TEXT("Heartbeat VFX material creation failed: %s"), *Error);
		}
	}

	FAutoConsoleCommand CreateHeartbeatVFXMaterialCommand(
		TEXT("ProjectProject01.CreateHeartbeatVFXMaterial"),
		TEXT("Create or repair the active heartbeat distortion VFX and the separate archived noise VFX."),
		FConsoleCommandDelegate::CreateStatic(&RunHeartbeatVFXMaterialCommand));

	void RunReimportTuningCommand()
	{
		FString Error;
		if (ReimportAndSaveTuningDataTables(Error))
		{
			UE_LOG(LogProjectProject01Tuning, Log, TEXT("ProjectProject01 tuning DataTables reimported and saved."));
		}
		else
		{
			UE_LOG(LogProjectProject01Tuning, Error, TEXT("ProjectProject01 tuning reimport failed: %s"), *Error);
		}
	}

	FAutoConsoleCommand ReimportTuningCommand(
		TEXT("ProjectProject01.ReimportTuningDataTables"),
		TEXT("Validate the three vertical tuning CSV files, then import and save their DataTables."),
		FConsoleCommandDelegate::CreateStatic(&RunReimportTuningCommand));

	bool CreateFrontendLevel(const FString& PackagePath, UClass* GameModeClass, FString& OutError)
	{
		FText PackageNameError;
		if (!FPackageName::IsValidLongPackageName(PackagePath, true, &PackageNameError))
		{
			OutError = PackageNameError.ToString();
			return false;
		}

		UWorld* NewWorld = FPackageName::DoesPackageExist(PackagePath)
			? UEditorLoadingAndSavingUtils::LoadMap(PackagePath)
			: UEditorLoadingAndSavingUtils::NewBlankMap(false);
		if (!IsValid(NewWorld))
		{
			OutError = FString::Printf(TEXT("프런트엔드 월드를 생성하거나 불러오지 못했습니다: %s"), *PackagePath);
			return false;
		}
		AWorldSettings* WorldSettings = NewWorld->GetWorldSettings();
		if (!IsValid(WorldSettings) || !IsValid(GameModeClass) || !GameModeClass->IsChildOf(AGameModeBase::StaticClass()))
		{
			OutError = FString::Printf(TEXT("레벨 GameMode 설정이 잘못되었습니다: %s"), *PackagePath);
			return false;
		}
		WorldSettings->DefaultGameMode = GameModeClass;
		WorldSettings->MarkPackageDirty();

		if (!UEditorLoadingAndSavingUtils::SaveMap(NewWorld, PackagePath))
		{
			OutError = FString::Printf(TEXT("레벨 패키지를 저장하지 못했습니다: %s"), *PackagePath);
			return false;
		}

		UE_LOG(LogProjectProject01Tuning, Log, TEXT("Frontend level configured and saved: %s"), *PackagePath);
		return true;
	}

	void RunCreateFrontendLevelsCommand()
	{
		FString Error;
		const bool bLoginCreated = CreateFrontendLevel(
			TEXT("/Game/MyProject/Level/LoginLevel"),
			AProjectProject01LoginGameMode::StaticClass(),
			Error);
		const bool bLobbyCreated = bLoginCreated && CreateFrontendLevel(
			TEXT("/Game/MyProject/Level/LobbyLevel"),
			AProjectProject01LobbyGameMode::StaticClass(),
			Error);
		if (!bLoginCreated || !bLobbyCreated)
		{
			UE_LOG(LogProjectProject01Tuning, Error, TEXT("Frontend level creation failed: %s"), *Error);
			return;
		}

		UE_LOG(LogProjectProject01Tuning, Log, TEXT("LoginLevel and LobbyLevel are ready."));
	}

	FAutoConsoleCommand CreateFrontendLevelsCommand(
		TEXT("ProjectProject01.CreateFrontendLevels"),
		TEXT("Create LoginLevel and LobbyLevel without overwriting existing map packages."),
		FConsoleCommandDelegate::CreateStatic(&RunCreateFrontendLevelsCommand));
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
			LOCTEXT("ReimportTuningTooltip", "Validate all three ProjectProject01 tuning CSV files before updating any DataTable."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FProjectProject01EditorModule::ReimportAndValidate)));
		Section.AddMenuEntry(
			TEXT("ProjectProject01ApplyPieTuning"),
			LOCTEXT("ApplyPieTuning", "Apply Tuning to PIE"),
			LOCTEXT("ApplyPieTuningTooltip", "Apply the validated DataTables to the server/standalone PIE world."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FProjectProject01EditorModule::ApplyToPie)));
		Section.AddMenuEntry(
			TEXT("ProjectProject01CreateHeartbeatVFXMaterial"),
			LOCTEXT("CreateHeartbeatVFXMaterial", "Create or Repair Heartbeat Screen VFX Materials"),
			LOCTEXT("CreateHeartbeatVFXMaterialTooltip", "Create the active distortion-only material and the separate archived noise-only material."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FProjectProject01EditorModule::CreateHeartbeatVFXMaterial)));

		RegisterProjectProject01DiagnosticsMenus();
	}

	void ReimportAndValidate()
	{
		using namespace ProjectProject01TuningEditor;
		FString Error;
		if (!ReimportAndSaveTuningDataTables(Error))
		{
			UE_LOG(LogProjectProject01Tuning, Error, TEXT("ProjectProject01 tuning reimport rejected: %s"), *Error);
			ShowResultNotification(Error, false);
			return;
		}
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

	void CreateHeartbeatVFXMaterial()
	{
		using namespace ProjectProject01TuningEditor;
		FString Error;
		const bool bSuccess = CreateOrRepairHeartbeatVFXMaterial(Error);
		if (!bSuccess)
		{
			UE_LOG(LogProjectProject01Tuning, Error, TEXT("Heartbeat VFX material creation failed: %s"), *Error);
		}
		ShowResultNotification(
			bSuccess ? TEXT("Heartbeat distortion and archived noise VFX materials created, compiled, and saved.") : Error,
			bSuccess);
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
