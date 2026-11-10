#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FireShutterActor.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "ProjectProject01Elevator.h"
#include "ProjectProject01LevelTransitionTrigger.h"
#include "ProjectProject01RoleSpawnPoint.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectProductionMapChecks, "ProjectProject01.Editor.ProductionMapLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProductionMapChecks::RunTest(const FString&)
{
    if (!GEditor || GEditor->IsPlaySessionInProgress()) return false;
    const auto* Destination = FindFProperty<FSoftObjectProperty>(
        AProjectProject01LevelTransitionTrigger::StaticClass(), TEXT("DestinationLevel"));
    if (!TestNotNull(TEXT("Level transition destination field"), Destination)) return false;

    for (const TCHAR* Level : {TEXT("MultiplayTest"), TEXT("MultiplayTest1"), TEXT("MultiplayTest2")})
    {
        const FString Package = TEXT("/Game/MyProject/Level/") + FString(Level);
        if (!TestTrue(Package + TEXT(" exists"), FPackageName::DoesPackageExist(Package))) continue;
        FAutomationEditorCommonUtils::LoadMap(Package);
        UWorld* World = GEditor->GetEditorWorldContext().World();
        if (!TestTrue(Package + TEXT(" loaded"), World && World->GetOutermost()->GetName() == Package)) continue;

        TSet<FVector> Locations;
        int32 Survivors = 0, Mannequins = 0;
        for (TActorIterator<AProjectProject01RoleSpawnPoint> Spawn(World); Spawn; ++Spawn)
        {
            const FVector Position = Spawn->GetActorLocation();
            TestTrue(FString::Printf(TEXT("%s: %s has a distinct finite spawn"), Level, *Spawn->GetActorLabel()),
                !Position.ContainsNaN() && !Locations.Contains(Position));
            Locations.Add(Position);
            if (Spawn->GetSpawnRole() == EProjectProject01SpawnRole::Survivor) ++Survivors;
            else ++Mannequins;
        }
        if (Survivors < 2 || Mannequins < 10)
            AddWarning(FString::Printf(TEXT("%s: explicit role starts survivor=%d/2 mannequin=%d/10; the match may use fallback starts."),
                Level, Survivors, Mannequins));

        int32 Links = 0, Elevators = 0, Shutters = 0;
        for (TActorIterator<AProjectProject01LevelTransitionTrigger> Trigger(World); Trigger; ++Trigger)
        {
            const FSoftObjectPtr Link = Destination->GetPropertyValue_InContainer(*Trigger);
            if (Link.IsNull())
            {
                if (!Trigger->IsA<AProjectProject01Elevator>())
                    AddWarning(FString::Printf(TEXT("%s: unassigned transition %s"), Level, *Trigger->GetActorLabel()));
                continue;
            }
            ++Links;
            const FString Target = Link.ToSoftObjectPath().GetLongPackageName();
            TestTrue(FString::Printf(TEXT("%s: %s has a valid different destination"), Level, *Trigger->GetActorLabel()),
                Target != Package && FPackageName::DoesPackageExist(Target));
        }
        for (TActorIterator<AProjectProject01Elevator> Elevator(World); Elevator; ++Elevator) ++Elevators;
        for (TActorIterator<AFireShutterActor> Shutter(World); Shutter; ++Shutter) ++Shutters;
        AddInfo(FString::Printf(TEXT("%s: role starts %d/%d, links %d, elevators %d, fire shutters %d"),
            Level, Survivors, Mannequins, Links, Elevators, Shutters));
    }
    return true;
}
#endif
