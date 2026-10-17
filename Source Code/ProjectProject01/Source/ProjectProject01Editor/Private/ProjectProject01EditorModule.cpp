// File: Source/ProjectProject01Editor/Private/ProjectProject01EditorModule.cpp
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#include "AssetToolsModule.h"
#include "Editor.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "GameDelegates.h"
#include "PlayerHeartbeatComponent.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "UObject/UObjectIterator.h"
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
#include "Materials/MaterialExpressionCustom.h"
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
			TEXT("HeartbeatSFXVolumeMultiplier"),
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
			TEXT("HeartbeatVFXBlendOutSpeed"),
			TEXT("DamageFeedbackIntroSeconds"),
			TEXT("DamageFeedbackFadeSeconds"),
			TEXT("DamageFeedbackMaxOpacity"),
			TEXT("DamageFeedbackMaxThicknessPixels"),
			TEXT("PainGroanSFXVolume"),
			TEXT("DamageCameraStumbleSeconds"),
			TEXT("DamageCameraRecoverySeconds"),
			TEXT("DamageCameraTiltDegrees"),
			TEXT("DeathCameraStumbleSeconds"),
			TEXT("DeathCameraFallSeconds"),
			TEXT("DeathBlackFadeSeconds"),
			TEXT("DeathMessageHoldSeconds"),
			TEXT("DeathCameraDropCentimeters"),
			TEXT("DeathCameraTiltDegrees"),
			TEXT("BodyFallSFXVolume")
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
			TEXT("DirectChaseHalfAngleDegrees"),
			TEXT("DirectChaseSectorRadius"),
			TEXT("MannequinGatherRadius"),
			TEXT("RequiredMannequinCount"),
			TEXT("SurvivorVisionCheckIntervalSeconds"),
			TEXT("SurvivorVisionHalfAngleDegrees"),
			TEXT("DetectionMargin"),
			TEXT("VisionFovMarginMultiplier"),
			TEXT("MultiplayerPatrolRadius"),
			TEXT("MultiplayerFollowDistance"),
			TEXT("MultiplayerFollowSeparation"),
			TEXT("MultiplayerRepathInterval"),
			TEXT("MultiplayerAcceptanceRadius"),
			TEXT("MultiplayerManualAttackRange")
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

	bool CreateOrRepairHeartbeatDistortionVFXMaterial(const FSoftObjectPath& TargetPath, FString& OutError)
	{
		const FString ObjectPath = TargetPath.ToString();
		if (!TargetPath.GetLongPackageName().StartsWith(TEXT("/Game/")) ||
			!FPackageName::IsValidObjectPath(ObjectPath))
		{
			OutError = TEXT("프로젝트 콘텐츠(/Game) 안의 머티리얼을 선택하세요.");
			return false;
		}
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
				FPackageName::GetShortName(TargetPath.GetLongPackageName()),
				FPackageName::GetLongPackagePath(TargetPath.GetLongPackageName()), UMaterial::StaticClass(), Factory));
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
		UMaterialExpressionScalarParameter* PhaseOffset = CreateScalarParameter(Material, TEXT("VertigoPhaseOffset"), 0.0f, -1250, 620);

		UMaterialExpressionSceneTexture* OriginalScene = CreateExpression<UMaterialExpressionSceneTexture>(Material, 350, -300);
		if (IsValid(OriginalScene)) { OriginalScene->SceneTextureId = PPI_PostProcessInput0; OriginalScene->bFiltered = true; }
		UMaterialExpressionComponentMask* OriginalRGB = CreateExpression<UMaterialExpressionComponentMask>(Material, 550, -300);
		if (IsValid(OriginalRGB)) { OriginalRGB->R = true; OriginalRGB->G = true; OriginalRGB->B = true; OriginalRGB->A = false; }
		UMaterialExpressionCustom* Vertigo = CreateExpression<UMaterialExpressionCustom>(Material, 750, 20);
		if (!IsValid(Vertigo))
		{
			OutError = TEXT("Heartbeat vertigo expression creation failed.");
			return false;
		}
		Vertigo->Description = TEXT("Peripheral vertigo: non-periodic local warping, uneven surges and defocus");
		Vertigo->OutputType = CMOT_Float3;
		// Keep the effect implementation at the existing heartbeat material generation path.
		Vertigo->Code = TEXT(R"HLSL(
// Smooth value noise: independently timed episodes, never frame-random flicker.
struct FVertigoNoise
{
    float Hash(float2 p, float seed)
    {
        float3 q = frac(float3(p, seed) * float3(0.1031, 0.11369, 0.13787));
        q += dot(q, q.yzx + 19.19);
        return frac((q.x + q.y) * q.z);
    }
    float Sample(float x, float seed)
    {
        float cell = floor(x);
        float f = frac(x);
        f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
        float a = Hash(float2(cell, 0.0), seed);
        float b = Hash(float2(cell + 1.0, 0.0), seed);
        return lerp(a, b, f);
    }
    float Field(float2 p, float seed)
    {
        float2 cell = floor(p);
        float2 f = frac(p);
        f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
        return lerp(lerp(Hash(cell, seed), Hash(cell + float2(1, 0), seed), f.x),
                    lerp(Hash(cell + float2(0, 1), seed), Hash(cell + 1.0, seed), f.x), f.y);
    }
};
FVertigoNoise noise;
// Actual shipped tuning reaches 0.003, not 0.012. Apply density/accessibility
// intensity once to displacement/defocus, not again to the final scene blend.
float strength = saturate(max(Amount, 0.0) / 0.003);
if (strength <= 0.00001) return OriginalColor;

float clock = (TimeSeconds + PhaseOffset) * clamp(Speed, 0.0, 3.0) * 0.65;
// Warp time as well as space: changes of direction/intensity do not share a fixed beat.
float t = clock + (noise.Sample(clock * 0.29, 91.3) - 0.5) * 1.7;
float2 drift = float2(noise.Sample(t * 0.71, 19.7), noise.Sample(t * 0.43, 73.1)) * 2.0 - 1.0;
float focus = noise.Sample(t * 0.59, 112.3);
float surge = smoothstep(0.20, 0.85, noise.Sample(t * 0.83, 203.9));
float2 p = ViewportUV - 0.5;
float radius = length(p * 2.0);
// Central vision stays clear; the periphery loses focus at different times/angles.
float rim = smoothstep(0.30, 1.04, radius);
if (rim <= 0.00001) return OriginalColor;
float patch = lerp(0.72, 1.0, noise.Field(p * 1.7 + drift * 0.3, 57.4));
float mask = rim * patch;
// Two independently advected, domain-warped fields deform different screen areas.
// No spatial sine stripes, rotating lobes, or regular back-and-forth oscillation.
float scale = lerp(1.8, 4.5, saturate(Frequency / 40.0));
float2 domain = p * scale + drift * 0.65;
float2 localFlow = float2(
    noise.Field(domain + float2(t * 0.37, -t * 0.19), 31.8),
    noise.Field(domain * float2(0.83, 1.21) + float2(-t * 0.23, t * 0.41), 149.2)) * 2.0 - 1.0;
float twist = noise.Sample(t * 0.53, 37.2) * 2.0 - 1.0;
float stretch = noise.Field(domain * 0.62 + localFlow * 0.4, 251.6) * 2.0 - 1.0;
float2 flow = drift * 0.65 + localFlow * 0.85
    + float2(-p.y, p.x) * twist * 0.45 + p * stretch * 0.35;
// Geometric movement must remain visible at real two-mannequin density, not only
// at maximum intensity. Keep a hard displacement bound and the clear center.
float2 offset = flow * (0.035 * strength) * mask * lerp(0.50, 1.60, surge);
offset *= min(1.0, 0.024 / max(length(offset), 0.00001));
float2 uv = ViewportUV + offset;
float2 axis = normalize(float2(1.2 + drift.x, 1.2 + drift.y));
float blurRadius = strength * mask * lerp(0.0030, 0.0100, focus);
float2 spread = axis * blurRadius;
float2 crossSpread = float2(-axis.y, axis.x) * blurRadius * 0.65;
float2 ghost = offset * 0.7 + spread * 1.5;

// Every tap is viewport->input UV converted and clamped (PIE, screen percentage,
// split view and non-full-size render targets must never sample a neighbouring view).
float3 soft = SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(uv, 14), 14), 14, true).rgb * 0.32;
soft += SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(uv + spread, 14), 14), 14, true).rgb * 0.13;
soft += SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(uv - spread, 14), 14), 14, true).rgb * 0.13;
soft += SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(uv + crossSpread, 14), 14), 14, true).rgb * 0.13;
soft += SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(uv - crossSpread, 14), 14), 14, true).rgb * 0.13;
soft += SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(uv + ghost, 14), 14), 14, true).rgb * 0.08;
soft += SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(uv - ghost * 0.7, 14), 14), 14, true).rgb * 0.08;
float luminance = dot(soft, float3(0.2126, 0.7152, 0.0722));
// A restrained grey haze, not a white flash or a full-screen blindness effect.
float haze = strength * mask * lerp(0.025, 0.11, focus);
soft = lerp(soft, luminance.xxx * 0.85 + float3(0.10, 0.105, 0.11), haze);
return lerp(OriginalColor, soft, mask);
)HLSL");
		Vertigo->Inputs.Reset();
		for (const TCHAR* Name : { TEXT("ViewportUV"), TEXT("OriginalColor"), TEXT("TimeSeconds"),
			TEXT("Amount"), TEXT("Speed"), TEXT("Frequency"), TEXT("PhaseOffset") })
		{
			Vertigo->Inputs.AddDefaulted_GetRef().InputName = FName(Name);
		}
		UMaterialExpressionLinearInterpolate* FinalBlend = CreateExpression<UMaterialExpressionLinearInterpolate>(Material, 1050, 20);

		bool bConnected = true;
		bConnected &= Connect(ScreenPosition, TEXT(""), ScreenU, TEXT(""));
		bConnected &= Connect(ScreenPosition, TEXT(""), ScreenV, TEXT(""));
		bConnected &= Connect(ScreenU, TEXT(""), OriginalUV, TEXT("A"));
		bConnected &= Connect(ScreenV, TEXT(""), OriginalUV, TEXT("B"));
		bConnected &= Connect(OriginalScene, TEXT(""), OriginalRGB, TEXT(""));
		bConnected &= Connect(OriginalUV, TEXT(""), Vertigo, TEXT("ViewportUV"));
		bConnected &= Connect(OriginalRGB, TEXT(""), Vertigo, TEXT("OriginalColor"));
		bConnected &= Connect(Time, TEXT(""), Vertigo, TEXT("TimeSeconds"));
		bConnected &= Connect(DistortionAmount, TEXT(""), Vertigo, TEXT("Amount"));
		bConnected &= Connect(DistortionSpeed, TEXT(""), Vertigo, TEXT("Speed"));
		bConnected &= Connect(DistortionFrequency, TEXT(""), Vertigo, TEXT("Frequency"));
		bConnected &= Connect(PhaseOffset, TEXT(""), Vertigo, TEXT("PhaseOffset"));
		bConnected &= Connect(OriginalRGB, TEXT(""), FinalBlend, TEXT("A"));
		bConnected &= Connect(Vertigo, TEXT(""), FinalBlend, TEXT("B"));
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

	bool CreateOrRepairHeartbeatVFXMaterials(const TArray<FSoftObjectPath>& TargetPaths, FString& OutError)
	{
		if (TargetPaths.IsEmpty())
		{
			OutError = TEXT("적용할 머티리얼을 하나 이상 선택하세요.");
			return false;
		}
		const FString DefaultGamePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("DefaultGame.ini"));
		FString ExistingConfig;
		if (IFileManager::Get().IsReadOnly(*DefaultGamePath) || !FFileHelper::LoadFileToString(ExistingConfig, *DefaultGamePath))
		{
			OutError = TEXT("DefaultGame.ini를 읽거나 수정할 수 없어 머티리얼 연결을 저장할 수 없습니다.");
			return false;
		}
		// Validate the entire selection before changing any material graph.
		TArray<FString> Paths;
		for (const FSoftObjectPath& Target : TargetPaths)
		{
			if (!Target.GetLongPackageName().StartsWith(TEXT("/Game/")) ||
				!FPackageName::IsValidObjectPath(Target.ToString()))
			{
				OutError = FString::Printf(TEXT("프로젝트 콘텐츠 안의 머티리얼만 선택하세요: %s"), *Target.ToString());
				return false;
			}
			if (UObject* Existing = Target.TryLoad(); Existing && !Existing->IsA<UMaterial>())
			{
				OutError = FString::Printf(TEXT("노드를 편집할 수 있는 기본 머티리얼만 선택하세요: %s"), *Target.ToString());
				return false;
			}
			FString Filename;
			if (FPackageName::DoesPackageExist(Target.GetLongPackageName(), &Filename) &&
				IFileManager::Get().IsReadOnly(*Filename))
			{
				OutError = FString::Printf(TEXT("머티리얼이 읽기 전용입니다: %s"), *Target.ToString());
				return false;
			}
			Paths.AddUnique(Target.ToString());
		}
		for (TObjectIterator<UPlayerHeartbeatComponent> It; It; ++It)
			if (!It->IsTemplate() && IsValid(It->GetWorld()) && It->GetWorld()->IsGameWorld())
				It->RefreshHeartbeatVFXMaterial();

		int32 Saved = 0;
		for (const FString& Path : Paths)
		{
			if (!CreateOrRepairHeartbeatDistortionVFXMaterial(FSoftObjectPath(Path), OutError))
			{
				OutError = FString::Printf(TEXT("%d/%d개 저장 후 실패: %s\n기존 카메라 연결 목록은 유지합니다."),
					Saved, Paths.Num(), *OutError);
				return false;
			}
			++Saved;
		}
		// Update only our section, preserving all unrelated configuration and comments.
		// UpdateSections does not insert array '+' operators, so include it explicitly.
		FConfigFile Selection;
		Selection.SetArray(TEXT("ProjectProject01.HeartbeatVFX"), TEXT("+Materials"), Paths);
		Selection.UpdateSections(*DefaultGamePath);
		FConfigFile Verified;
		Verified.Read(DefaultGamePath);
		TArray<FString> SavedPaths;
		Verified.GetArray(TEXT("ProjectProject01.HeartbeatVFX"), TEXT("+Materials"), SavedPaths);
		if (SavedPaths != Paths)
		{
			OutError = TEXT("머티리얼은 저장했지만 DefaultGame.ini 기록에 실패해 연결 목록은 변경하지 않았습니다.");
			return false;
		}
		GConfig->SetArray(TEXT("ProjectProject01.HeartbeatVFX"), TEXT("Materials"), Paths, GGameIni);
		const TArray<FSoftObjectPath> RuntimePaths = GetDefault<UPlayerHeartbeatComponent>()->GetHeartbeatVFXMaterialPaths();
		if (RuntimePaths.Num() != Paths.Num())
		{
			OutError = TEXT("저장한 목록과 실제 런타임 연결 개수가 다릅니다. Output Log의 HeartbeatVFX 항목을 확인하세요.");
			return false;
		}
		for (const FString& Path : Paths)
			UE_LOG(LogProjectProject01Tuning, Log, TEXT("[HeartbeatVFX] Saved camera connection: %s"), *Path);
		UE_LOG(LogProjectProject01Tuning, Log,
			TEXT("[HeartbeatVFX] Rebuilt and connected %d selected material(s); archived/unselected assets unchanged."), Paths.Num());
		OutError.Reset();
		return true;
	}

	void RunHeartbeatVFXMaterialCommand(const TArray<FString>& Args)
	{
		TArray<FSoftObjectPath> Targets;
		for (const FString& Arg : Args) Targets.AddUnique(FSoftObjectPath(Arg));
		if (Targets.IsEmpty()) Targets = GetDefault<UPlayerHeartbeatComponent>()->GetHeartbeatVFXMaterialPaths();
		FString Error;
		if (!CreateOrRepairHeartbeatVFXMaterials(Targets, Error))
			UE_LOG(LogProjectProject01Tuning, Error, TEXT("Heartbeat VFX material creation failed: %s"), *Error);
	}

	FAutoConsoleCommand CreateHeartbeatVFXMaterialCommand(
		TEXT("ProjectProject01.CreateHeartbeatVFXMaterial"),
		TEXT("Rebuild and connect selected heartbeat materials. Optional arguments: space-separated /Game/Path/Material.Material paths. No arguments uses the saved list."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunHeartbeatVFXMaterialCommand));

	void AddHeartbeatMaterialToCook(TConstArrayView<const ITargetPlatform*>, TArray<FName>& PackagesToCook,
		TArray<FName>& PackagesToNeverCook)
	{
		for (const FSoftObjectPath& Path : GetDefault<UPlayerHeartbeatComponent>()->GetHeartbeatVFXMaterialPaths())
		{
			const FName Package(*Path.GetLongPackageName());
			if (!Package.IsNone() && !PackagesToNeverCook.Contains(Package))
			{
				PackagesToCook.AddUnique(Package);
				UE_LOG(LogProjectProject01Tuning, Log, TEXT("[HeartbeatVFX] Cook runtime material: %s"), *Package.ToString());
			}
		}
	}

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
		const bool bTitleCreated = CreateFrontendLevel(
			TEXT("/Game/MyProject/Level/TitleLevel"),
			AProjectProject01TitleGameMode::StaticClass(),
			Error);
		const bool bLoginCreated = bTitleCreated && CreateFrontendLevel(
			TEXT("/Game/MyProject/Level/LoginLevel"),
			AProjectProject01LoginGameMode::StaticClass(),
			Error);
		const bool bLobbyCreated = bLoginCreated && CreateFrontendLevel(
			TEXT("/Game/MyProject/Level/LobbyLevel"),
			AProjectProject01LobbyGameMode::StaticClass(),
			Error);
		const bool bLeaderboardCreated = bLobbyCreated && CreateFrontendLevel(
			TEXT("/Game/MyProject/Level/LeaderBoardTest"),
			AProjectProject01LeaderboardGameMode::StaticClass(),
			Error);
		if (!bTitleCreated || !bLoginCreated || !bLobbyCreated || !bLeaderboardCreated)
		{
			UE_LOG(LogProjectProject01Tuning, Error, TEXT("Frontend level creation failed: %s"), *Error);
			return;
		}

		UE_LOG(LogProjectProject01Tuning, Log,
			TEXT("TitleLevel, LoginLevel, LobbyLevel, and LeaderBoardTest are ready."));
	}

	FAutoConsoleCommand CreateFrontendLevelsCommand(
		TEXT("ProjectProject01.CreateFrontendLevels"),
		TEXT("Create TitleLevel, LoginLevel, LobbyLevel, and LeaderBoardTest without overwriting existing map packages."),
		FConsoleCommandDelegate::CreateStatic(&RunCreateFrontendLevelsCommand));
}

class FProjectProject01EditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FProjectProject01EditorModule::RegisterMenus));
		FGameDelegates::Get().GetModifyCookDelegate().AddRaw(this, &FProjectProject01EditorModule::ModifyHeartbeatCook);
	}

	virtual void ShutdownModule() override
	{
		FGameDelegates::Get().GetModifyCookDelegate().RemoveAll(this);
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
			LOCTEXT("CreateHeartbeatVFXMaterialTooltip", "Select one or more materials (Ctrl/Shift), rebuild only the selection, and connect all selected materials to the single/multiplayer camera."),
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

	void ModifyHeartbeatCook(TConstArrayView<const ITargetPlatform*> Platforms, TArray<FName>& PackagesToCook,
		TArray<FName>& PackagesToNeverCook)
	{
		ProjectProject01TuningEditor::AddHeartbeatMaterialToCook(Platforms, PackagesToCook, PackagesToNeverCook);
	}

	void CreateHeartbeatVFXMaterial()
	{
		using namespace ProjectProject01TuningEditor;
		const TArray<FSoftObjectPath> Current = GetDefault<UPlayerHeartbeatComponent>()->GetHeartbeatVFXMaterialPaths();
		FOpenAssetDialogConfig Dialog;
		Dialog.DialogTitleOverride = FText::FromString(TEXT("심박 VFX 머티리얼 선택 — Ctrl/Shift로 여러 개 선택"));
		Dialog.DefaultPath = Current.IsEmpty() ? TEXT("/Game/MyProject/VFX")
			: FPackageName::GetLongPackagePath(Current[0].GetLongPackageName());
		Dialog.AssetClassNames.Add(UMaterial::StaticClass()->GetClassPathName());
		Dialog.bAllowMultipleSelection = true;
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		const TArray<FAssetData> Selected = ContentBrowser.Get().CreateModalOpenAssetDialog(Dialog);
		if (Selected.IsEmpty()) return; // Cancellation never changes assets or their connection.

		TArray<FSoftObjectPath> Targets;
		TArray<FString> Names;
		for (const FAssetData& Asset : Selected)
		{
			Targets.AddUnique(Asset.GetSoftObjectPath());
			Names.AddUnique(Asset.GetSoftObjectPath().ToString());
		}
		const FText Confirmation = FText::FromString(FString::Printf(
			TEXT("선택한 %d개 머티리얼:\n%s\n\n선택한 모든 머티리얼의 기존 노드를 심박 현기증 VFX로 재생성하고 저장합니다.\n카메라에는 선택한 목록 전체가 연결됩니다(효과가 겹쳐질 수 있습니다).\n선택하지 않은 머티리얼은 변경하지 않습니다.\n\n계속하시겠습니까?"),
			Targets.Num(), *FString::Join(Names, TEXT("\n"))));
		if (FMessageDialog::Open(EAppMsgType::YesNo, Confirmation) != EAppReturnType::Yes) return;

		FString Error;
		const bool bSuccess = CreateOrRepairHeartbeatVFXMaterials(Targets, Error);
		if (!bSuccess)
			UE_LOG(LogProjectProject01Tuning, Error, TEXT("Heartbeat VFX material creation failed: %s"), *Error);
		ShowResultNotification(bSuccess
			? FString::Printf(TEXT("선택한 %d개 심박 VFX 머티리얼 저장·연결 완료"), Targets.Num()) : Error, bSuccess);
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
