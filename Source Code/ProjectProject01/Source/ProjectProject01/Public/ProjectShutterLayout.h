#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectShutterLayoutRules.h"
#include "ProjectShutterLayout.generated.h"

class AFireShutterActor;
class UWorld;

namespace ProjectShutterLayout
{
	/** Short map name used as the key in the ShutterSeeds travel option. */
	PROJECTPROJECT01_API FString MapKey(const UWorld* World);
	/** This map's seed from a ShutterSeeds option ("Map-123.Other-456"), 0 when absent. */
	PROJECTPROJECT01_API int32 SeedFromOptions(const FString& Options, const FString& Map);
	/** Every floor seed visited so far plus this floor's, for the next travel URL. */
	PROJECTPROJECT01_API FString SeedsForTravel(const UWorld* World);
}
class AProjectProject01LevelTransitionTrigger;

/**
 * A corridor junction for the fire shutter layout (Docs/FireShutterLayout.md).
 * Place one at each corridor crossing/turn and link it to its neighbours (links count both ways).
 */
UCLASS(BlueprintType)
class PROJECTPROJECT01_API AProjectRouteNode : public AActor
{
	GENERATED_BODY()

public:
	AProjectRouteNode();

	/** Neighbouring junctions reachable along a corridor. A link set on either side is enough. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Route")
	TArray<TObjectPtr<AProjectRouteNode>> Links;

	/** Survivors start here. Without any, the nearest node to each survivor spawn point is used. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Route")
	bool bSurvivorStart = false;

	/** The exit (1st floor). Always kept reachable, like the escalators. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Route")
	bool bExit = false;
};

/**
 * Rolls this floor's fire shutters once per match, on the server (Docs/FireShutterLayout.md).
 * Place one per floor map. Every escalator works; one shutter around it opens, the rest are broken.
 * Corridors: a few broken barriers and a few barriers with one working shutter, never cutting any spawn off an
 * escalator/exit, never a long detour, never two on a junction.
 */
UCLASS(BlueprintType)
class PROJECTPROJECT01_API AProjectShutterLayout : public AActor
{
	GENERATED_BODY()
	friend class FProjectShutterLayoutRuntimeTest;

public:
	AProjectShutterLayout();

	/** Builds and applies a layout now (server/standalone). Returns what it decided. */
	FShutterLayoutResult Generate(int32 Seed);

	/** Placed shutters, grouped the way the rules see them. Reads the world only (changes nothing). */
	struct FPlacement
	{
		FShutterLayoutGraph Graph;
		FShutterLayoutSettings Settings;
		TArray<TArray<AFireShutterActor*>> SideShutters; // Per escalator, in the order of Graph.Escalators[i].Sides.
		TArray<AFireShutterActor*> SlotShutters;          // Per Graph.Slots entry.
		TArray<AFireShutterActor*> OffLinkShutters;       // Corridor shutters not near any route link.
		TArray<AFireShutterActor*> StoreShutters;
		/** Exit rows (tag ExitRow_<n>, one group per row), each sorted by name. */
		TArray<TArray<AFireShutterActor*>> ExitRows;
		TArray<FName> EscalatorIds;
	};
	FPlacement Collect() const;

	/** Server: working shutters still down open for any of these (bot) locations close to them. Bots cannot press F. */
	void OpenWorkingShuttersNear(const TArray<FVector>& Locations);

	/** Corridors sealed by broken shutters each match (rolled between Min and Max). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MinSealedCorridors = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxSealedCorridors = 2;

	/** Corridors closed by a barrier with one working shutter each match (rolled between Min and Max). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MinWorkingCorridors = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxWorkingCorridors = 2;

	/**
	 * Fire compartment openings (shutters with the Chokepoint role, placed along compartment lines) closed each match.
	 * Default: all of them, like a store whose fire doors have dropped; each is either openable (working shutters in the
	 * row) or, by Chokepoint Sealed Chance, broken through so survivors go around. Every area always stays reachable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0", ClampMax = "999"))
	int32 MinChokepoints = 999;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0", ClampMax = "999"))
	int32 MaxChokepoints = 999;
	/** Exit rows: one working shutter per this many cm of row (at least one, and at least one broken when the row has two). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "300"))
	float ExitWorkingSpacing = 900.f;
	/** Chance a whole fire compartment line stays open this match (no shutter down along it): zones that are not blocked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OpenLineChance = .35f;
	/** Rows wider than this (main corridors) never close; they only look smashed or hang. Keeps the store's corridors open. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "300.0", Units = "cm"))
	float MaxClosableRowWidth = 1000.f;
	/** Chance a closed chokepoint is broken shutters only (no way through there; another way always remains). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChokepointSealedChance = .3f;

	/** Longest allowed way around: original distance x ratio + slack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float MaxDetourRatio = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float DetourSlack = 1000.f;

	/** Closed corridors keep at least this far apart. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float MinSealSpacing = 1500.f;

	/** Never close a corridor that touches a spawn, an escalator's working side or the exit junction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout")
	bool bProtectObjectiveCorridors = true;

	/** A corridor shutter further than this from every route link is left alone (raised). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float CorridorSnapDistance = 500.f;

	/** Chance a store-front shutter starts down (players can still raise it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StoreClosedChance = 0.3f;

	/**
	 * Ruined look, shutters only (Docs/FireShutterLayout.md): broken shutters jammed at odd heights and bent, unused
	 * ones hanging overhead, store fronts half open. Heights keep the rules: a stuck gap stays too low to pass, a
	 * hanging shutter stays high enough to walk under.
	 */
	/** Damage zones each match: where shutters are worst off (more jammed, hanging, smashed rows, bent harder). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0", ClampMax = "6"))
	int32 MinDamageZones = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0", ClampMax = "6"))
	int32 MaxDamageZones = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "500.0", Units = "cm"))
	float DamageZoneRadius = 2500.f;
	/** Broken shutters jammed with a gap at the floor (inside a damage zone / elsewhere); the rest are all the way down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JamChanceInZone = .6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StuckChance = .3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0.0", ClampMax = "120.0", Units = "cm"))
	float StuckGapMin = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0.0", ClampMax = "120.0", Units = "cm"))
	float StuckGapMax = 90.f;
	/** Unused shutters hanging overhead (inside a damage zone / elsewhere). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HangingChanceInZone = .55f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HangingChance = .1f;
	/** Open rows smashed: some panels down, a wide one left open (inside a damage zone / elsewhere). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PartialRowChanceInZone = .7f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PartialRowChance = .15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "210.0", Units = "cm"))
	float HangingGapMin = 220.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "210.0", Units = "cm"))
	float HangingGapMax = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StoreHalfChance = .25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0.0", ClampMax = "10.0", Units = "deg"))
	float MaxBrokenTilt = 8.f;

	/** Working shutters (escalator sides and corridor barriers) start down; survivors raise them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout")
	bool bWorkingShuttersStartClosed = true;

	/** Bot survivors open a working shutter by walking up to it (they cannot press F). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout")
	bool bBotsOpenWorkingShutters = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "50.0", Units = "cm"))
	float BotOpenDistance = 220.f;

	/** Horror weighting of the random picks (0 = uniform). Fairness rules always win. See Docs/FireShutterLayout.md. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float HorrorWeight = 1.f;

	/** Non-zero: same layout every match (testing). 0: a new roll each match. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout")
	int32 FixedSeed = 0;

	/** The ruined look settings (damage zones, jams, hanging, smashed rows) as the rules take them. */
	FShutterRuinSettings RuinSettings() const;
	/** Fixtures inside a damage zone that are dead this match (no light). Shutters only rule the zones; lights follow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire Shutter Layout|Ruin", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DeadLightChanceInZone = .35f;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Damage zones of the last roll (for checks and debugging). */
	const TArray<FVector2D>& GetRuinZones() const { return LastRuinZones; }
	/** The seed this floor was rolled with this match (0 before the roll). */
	int32 GetRolledSeed() const { return RolledSeed; }

protected:
	virtual void BeginPlay() override;


private:
	TArray<FVector2D> LastRuinZones;
	/** The match seed, sent to clients so their lights follow the same damage zones (they roll them locally, same rules). */
	UPROPERTY(ReplicatedUsing = OnRep_RolledSeed) int32 RolledSeed = 0;
	UPROPERTY(Replicated) bool bRolled = false;
	UFUNCTION() void OnRep_RolledSeed();
	void ApplyRuinLights(const TArray<FVector2D>& Zones, int32 Seed) const;
	void CheckBots();
	/** Working shutters this layout placed (escalator sides and corridor barriers). */
	TArray<TWeakObjectPtr<AFireShutterActor>> WorkingShutters;
	FTimerHandle BotTimer;
};
