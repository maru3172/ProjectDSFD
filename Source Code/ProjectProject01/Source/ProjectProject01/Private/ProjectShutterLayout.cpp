#include "ProjectShutterLayout.h"

#include "EngineUtils.h"
#include "FireShutterActor.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "ProjectProject01RoleSpawnPoint.h"
#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "MannequinReactiveLight.h"
#include "Net/UnrealNetwork.h"
#include "MultiplayTestGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectShutterLayout, Log, All);

namespace
{
/** Editor-only marker so designers can see the actor; nothing of it exists in a cooked game. */
void AddEditorSprite(AActor& Actor, const TCHAR* Texture)
{
#if WITH_EDITORONLY_DATA
	if (UBillboardComponent* Sprite = Actor.CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite")))
	{
		ConstructorHelpers::FObjectFinderOptional<UTexture2D> Icon(Texture);
		if (Icon.Succeeded()) Sprite->SetSprite(Icon.Get());
		Sprite->SetupAttachment(Actor.GetRootComponent());
		Sprite->bIsScreenSizeScaled = true;
	}
#endif
}

bool ByName(const AFireShutterActor& A, const AFireShutterActor& B) { return A.GetName() < B.GetName(); }

bool SegmentsCross(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
{
	auto Side = [](const FVector2D& P, const FVector2D& Q, const FVector2D& R) { return FVector2D::CrossProduct(Q - P, R - P); };
	return (Side(A, B, C) > 0) != (Side(A, B, D) > 0) && (Side(C, D, A) > 0) != (Side(C, D, B) > 0);
}
}

AProjectRouteNode::AProjectRouteNode()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	AddEditorSprite(*this, TEXT("/Engine/EditorResources/S_TargetPoint"));
	SetHidden(true);
	SetCanBeDamaged(false);
}

AProjectShutterLayout::AProjectShutterLayout()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	AddEditorSprite(*this, TEXT("/Engine/EditorResources/S_Note"));
	SetHidden(true);
	SetCanBeDamaged(false);
	bReplicates = true;
	bAlwaysRelevant = true; // tiny: one seed, so every client lights its floor the same way
}

void AProjectShutterLayout::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProjectShutterLayout, RolledSeed);
	DOREPLIFETIME(AProjectShutterLayout, bRolled);
}

void AProjectShutterLayout::OnRep_RolledSeed()
{
	if (HasAuthority() || !bRolled) return;
	// Clients roll the same floor locally (same placed actors, same pure rules) to know the damage zones for their lights.
	const FPlacement Placement = Collect();
	const FShutterLayoutResult Result = ProjectShutterLayout::Generate(Placement.Graph, Placement.Settings, RolledSeed);
	const FShutterRuin Ruin = ProjectShutterLayout::RollRuin(Placement.Graph, Result, RuinSettings(), RolledSeed);
	LastRuinZones = Ruin.Zones;
	ApplyRuinLights(Ruin.Zones, RolledSeed);
}

void AProjectShutterLayout::ApplyRuinLights(const TArray<FVector2D>& Zones, const int32 Seed) const
{
	if (!GetWorld() || GetNetMode() == NM_DedicatedServer) return; // lights are only looked at on clients
	for (TActorIterator<AMannequinReactiveLight> It(GetWorld()); It; ++It)
	{
		// Lights by the starts and over the ways out always stay on (placed with these tags by the placement script).
		if (It->Tags.Contains(FName(TEXT("LightKind_start"))) || It->Tags.Contains(FName(TEXT("LightKind_exit")))) { It->SetMatchDead(false); continue; }
		bool bIn = false;
		for (const FVector2D& Z : Zones) bIn |= FVector2D::Distance(Z, FVector2D(It->GetActorLocation())) <= DamageZoneRadius;
		FRandomStream Roll(Seed ^ int32(GetTypeHash(It->GetName())));
		It->SetMatchDead(bIn && Roll.FRand() < DeadLightChanceInZone);
	}
}

void AProjectShutterLayout::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority() || !GetWorld()) return;
	// Next tick: every placed shutter has run BeginPlay (and captured its closed position) by then.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]
	{
		// A floor keeps its layout for the whole match: going down to the rescue basement and back up must not reshuffle 1F.
		// Floor changes carry every visited floor's seed in the travel URL (ShutterSeeds, see the level transition trigger).
		int32 Seed = FixedSeed;
		if (Seed == 0)
			if (const AGameModeBase* Mode = GetWorld()->GetAuthGameMode())
				Seed = ProjectShutterLayout::SeedFromOptions(Mode->OptionsString, ProjectShutterLayout::MapKey(GetWorld()));
		if (Seed == 0) Seed = FMath::Max(1, FMath::Rand()); // never 0: clients learn the seed through a replicated change
		Generate(Seed);
		if (bBotsOpenWorkingShutters && !WorkingShutters.IsEmpty())
			GetWorldTimerManager().SetTimer(BotTimer, FTimerDelegate::CreateUObject(this, &AProjectShutterLayout::CheckBots), .5f, true);
	}));
}

void AProjectShutterLayout::CheckBots()
{
	TArray<FVector> Bots;
	for (TActorIterator<APlayerCharacter> It(GetWorld()); It; ++It)
		if (const AMultiplayTestPlayerController* Controller = Cast<AMultiplayTestPlayerController>(It->GetController()))
			if (Controller->IsServerControlledBot() && !It->IsGameOver()) Bots.Add(It->GetActorLocation());
	if (!Bots.IsEmpty()) OpenWorkingShuttersNear(Bots);
}

void AProjectShutterLayout::OpenWorkingShuttersNear(const TArray<FVector>& Locations)
{
	if (!HasAuthority()) return;
	for (const TWeakObjectPtr<AFireShutterActor>& Weak : WorkingShutters)
	{
		AFireShutterActor* Shutter = Weak.Get();
		if (!Shutter || !Shutter->IsHeadingDown()) continue;
		const FVector At = Shutter->GetActorLocation();
		for (const FVector& Location : Locations)
			if (FVector::DistSquared2D(Location, At) <= FMath::Square(BotOpenDistance) && FMath::Abs(Location.Z - At.Z) < 250.f)
			{
				Shutter->OpenByServer();
				break;
			}
	}
}

AProjectShutterLayout::FPlacement AProjectShutterLayout::Collect() const
{
	FPlacement Out;
	UWorld* World = GetWorld();
	if (!World) return Out;
	FShutterLayoutGraph& Graph = Out.Graph;

	// Route graph from the placed junctions (sorted by name: same seed, same layout, whatever the actor order).
	TArray<AProjectRouteNode*> Nodes; TMap<const AProjectRouteNode*, int32> NodeIndex;
	for (TActorIterator<AProjectRouteNode> It(World); It; ++It) Nodes.Add(*It);
	Nodes.Sort([](const AProjectRouteNode& A, const AProjectRouteNode& B) { return A.GetName() < B.GetName(); });
	for (AProjectRouteNode* Node : Nodes) NodeIndex.Add(Node, Graph.AddNode(FVector2D(Node->GetActorLocation())));
	for (const AProjectRouteNode* Node : Nodes)
		for (const TObjectPtr<AProjectRouteNode>& Link : Node->Links)
			if (const int32* Other = NodeIndex.Find(Link.Get())) Graph.AddEdge(NodeIndex[Node], *Other);
	auto NearestNode = [&Graph](const FVector& Location)
	{
		int32 Best = INDEX_NONE; double BestDistance = TNumericLimits<double>::Max();
		for (int32 I = 0; I < Graph.Nodes.Num(); ++I)
			if (const double Distance = FVector2D::DistSquared(Graph.Nodes[I], FVector2D(Location)); Distance < BestDistance) { BestDistance = Distance; Best = I; }
		return Best;
	};
	for (const AProjectRouteNode* Node : Nodes)
	{
		if (Node->bSurvivorStart) Graph.Starts.AddUnique(NodeIndex[Node]);
		if (Node->bExit) Graph.Exits.AddUnique(NodeIndex[Node]);
	}
	// Spawns: survivors are the starts (unless marked), mannequins are the threat the horror weighting reads.
	Graph.Threat.Init(0.f, Graph.Nodes.Num());
	const bool bMarkedStarts = !Graph.Starts.IsEmpty();
	for (TActorIterator<AProjectProject01RoleSpawnPoint> It(World); It; ++It)
	{
		const int32 Node = NearestNode(It->GetActorLocation());
		if (Node == INDEX_NONE) continue;
		if (It->GetSpawnRole() == EProjectProject01SpawnRole::Survivor) { if (!bMarkedStarts) Graph.Starts.AddUnique(Node); }
		else Graph.Threat[Node] += 1.f;
	}

	// Escalators: shutters sharing an id close in one opening, each shutter one side (the junction in front of it).
	// Elevators are never part of this: their shutters do not get the Escalator role.
	TArray<AFireShutterActor*> Shutters;
	for (TActorIterator<AFireShutterActor> It(World); It; ++It) if (It->GetLayoutRole() != EProjectShutterLayoutRole::None) Shutters.Add(*It);
	Shutters.Sort(ByName);
	for (const AFireShutterActor* Shutter : Shutters)
		if (Shutter->GetLayoutRole() == EProjectShutterLayoutRole::Escalator && !Shutter->GetEscalatorId().IsNone()) Out.EscalatorIds.AddUnique(Shutter->GetEscalatorId());
	Out.EscalatorIds.Sort(FNameLexicalLess());
	Out.SideShutters.SetNum(Out.EscalatorIds.Num()); Graph.Escalators.SetNum(Out.EscalatorIds.Num());
	for (AFireShutterActor* Shutter : Shutters)
		switch (Shutter->GetLayoutRole())
		{
		case EProjectShutterLayoutRole::Escalator:
			if (const int32 Escalator = Out.EscalatorIds.IndexOfByKey(Shutter->GetEscalatorId()); Escalator != INDEX_NONE)
			{
				Out.SideShutters[Escalator].Add(Shutter);
				Graph.Escalators[Escalator].Sides.Add(NearestNode(Shutter->GetActorLocation()));
			}
			break;
		case EProjectShutterLayoutRole::Corridor:
		case EProjectShutterLayoutRole::Chokepoint:
			// Corridor shutters become slots on the nearest route link (several on one link = one barrier across it).
			if (const int32 Edge = Graph.FindNearestEdge(FVector2D(Shutter->GetActorLocation()), CorridorSnapDistance); Edge != INDEX_NONE)
			{
				FShutterLayoutGraph::FSlot Slot; Slot.Edge = Edge; Slot.Location = FVector2D(Shutter->GetActorLocation());
				Slot.bChokepoint = Shutter->GetLayoutRole() == EProjectShutterLayoutRole::Chokepoint;
				Slot.Width = 300.f * FMath::Abs(Shutter->GetActorScale3D().Y);
				for (const FName& Tag : Shutter->Tags)
					if (const FString T = Tag.ToString(); T.StartsWith(TEXT("CompLine_"))) Slot.Line = FCString::Atoi(*T.RightChop(9));
				// Every link the shutter stands across closes with it (a wide corridor has parallel links on a fine graph).
				const FVector2D Half = FVector2D(Shutter->GetActorRightVector()).GetSafeNormal() * (150.f * Shutter->GetActorScale3D().Y);
				for (int32 E = 0; E < Graph.Edges.Num(); ++E)
					if (E != Edge && SegmentsCross(Slot.Location - Half, Slot.Location + Half, Graph.Nodes[Graph.Edges[E].A], Graph.Nodes[Graph.Edges[E].B]))
						Slot.Crosses.Add(E);
				Graph.Slots.Add(Slot); Out.SlotShutters.Add(Shutter);
			}
			else Out.OffLinkShutters.Add(Shutter);
			break;
		case EProjectShutterLayoutRole::Store: Out.StoreShutters.Add(Shutter); break;
		case EProjectShutterLayoutRole::Exit:
		{
			// Not a slot: the exit is never cut off (a working shutter always stays in the row), so the rules do not see it.
			int32 Row = 0;
			for (const FName& Tag : Shutter->Tags)
				if (const FString T = Tag.ToString(); T.StartsWith(TEXT("ExitRow_"))) Row = FCString::Atoi(*T.RightChop(8));
			if (Out.ExitRows.Num() <= Row) Out.ExitRows.SetNum(Row + 1);
			Out.ExitRows[Row].Add(Shutter);
			break;
		}
		default: break;
		}

	FShutterLayoutSettings& Settings = Out.Settings;
	Settings.MinSealed = MinSealedCorridors; Settings.MaxSealed = FMath::Max(MinSealedCorridors, MaxSealedCorridors);
	Settings.MinDoors = MinWorkingCorridors; Settings.MaxDoors = FMath::Max(MinWorkingCorridors, MaxWorkingCorridors);
	Settings.MinChokepoints = MinChokepoints; Settings.MaxChokepoints = FMath::Max(MinChokepoints, MaxChokepoints);
	Settings.ChokepointSealedChance = ChokepointSealedChance;
	Settings.MaxClosableRowWidth = MaxClosableRowWidth;
	Settings.OpenLineChance = OpenLineChance;
	Settings.MaxDetourRatio = MaxDetourRatio; Settings.DetourSlack = DetourSlack; Settings.MinSealSpacing = MinSealSpacing;
	Settings.bProtectObjectiveEdges = bProtectObjectiveCorridors; Settings.HorrorWeight = HorrorWeight;
	return Out;
}

namespace ProjectShutterLayout
{
FString MapKey(const UWorld* World)
{
	return World ? FPackageName::GetShortName(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())) : FString();
}

int32 SeedFromOptions(const FString& Options, const FString& Map)
{
	TArray<FString> Entries;
	UGameplayStatics::ParseOption(Options, TEXT("ShutterSeeds")).ParseIntoArray(Entries, TEXT("."));
	for (const FString& Entry : Entries)
	{
		FString Name, Value;
		if (Entry.Split(TEXT("-"), &Name, &Value, ESearchCase::CaseSensitive, ESearchDir::FromEnd) && Name == Map) return FCString::Atoi(*Value);
	}
	return 0;
}

FString SeedsForTravel(const UWorld* World)
{
	if (!World) return FString();
	const AGameModeBase* Mode = World->GetAuthGameMode();
	TArray<FString> Entries;
	if (Mode) UGameplayStatics::ParseOption(Mode->OptionsString, TEXT("ShutterSeeds")).ParseIntoArray(Entries, TEXT("."));
	const FString Here = MapKey(World);
	for (TActorIterator<AProjectShutterLayout> It(const_cast<UWorld*>(World)); It; ++It)
		if (It->GetRolledSeed() != 0)
		{
			Entries.RemoveAll([&](const FString& E) { FString N, V; return E.Split(TEXT("-"), &N, &V, ESearchCase::CaseSensitive, ESearchDir::FromEnd) && N == Here; });
			Entries.Add(FString::Printf(TEXT("%s-%d"), *Here, It->GetRolledSeed()));
		}
	return FString::Join(Entries, TEXT("."));
}
}

FShutterRuinSettings AProjectShutterLayout::RuinSettings() const
{
	FShutterRuinSettings S;
	S.MinZones = MinDamageZones; S.MaxZones = FMath::Max(MinDamageZones, MaxDamageZones); S.ZoneRadius = DamageZoneRadius;
	S.JamChanceIn = JamChanceInZone; S.JamChanceOut = StuckChance;
	S.HangChanceIn = HangingChanceInZone; S.HangChanceOut = HangingChance;
	S.PartialRowChanceIn = PartialRowChanceInZone; S.PartialRowChanceOut = PartialRowChance;
	S.TiltIn = MaxBrokenTilt; S.TiltOut = MaxBrokenTilt * .4f;
	S.WideRowWidth = MaxClosableRowWidth;
	S.JamGapMin = StuckGapMin; S.JamGapMax = StuckGapMax; S.HangGapMin = HangingGapMin; S.HangGapMax = HangingGapMax;
	return S;
}

FShutterLayoutResult AProjectShutterLayout::Generate(const int32 Seed)
{
	FShutterLayoutResult Result; Result.Seed = Seed;
	if (!GetWorld() || !HasAuthority()) return Result;
	const FPlacement Placement = Collect();
	Result = ProjectShutterLayout::Generate(Placement.Graph, Placement.Settings, Seed);

	// Apply. The ruined look rolls from its own stream so it never changes which shutters close.
	WorkingShutters.Reset();
	const bool bWorkingOpen = !bWorkingShuttersStartClosed;
	FRandomStream Look(Seed ^ 0x51ED);
	auto Alpha = [](const AFireShutterActor* Shutter, float Gap) { return Shutter->GetOpenHeight() > 1.f ? FMath::Clamp(Gap / Shutter->GetOpenHeight(), 0.f, 1.f) : 0.f; };
	// Damage zones and each shutter's look come from the pure rules, so the pictures in the checks show exactly this.
	const FShutterRuin Ruin = ProjectShutterLayout::RollRuin(Placement.Graph, Result, RuinSettings(), Seed);
	LastRuinZones = Ruin.Zones;
	RolledSeed = Seed; bRolled = true; ForceNetUpdate();
	ApplyRuinLights(Ruin.Zones, Seed);
	auto Show = [&](AFireShutterActor* Shutter, const FShutterLook& L)
	{
		switch (L.Look)
		{
		case EShutterLook::Working: Shutter->ApplyLayout(EProjectShutterLayoutState::Operable, bWorkingOpen); WorkingShutters.Add(Shutter); break;
		case EShutterLook::Down: Shutter->ApplyLayout(EProjectShutterLayoutState::Sealed, 0.f, L.Tilt); break;
		case EShutterLook::Jammed: Shutter->ApplyLayout(EProjectShutterLayoutState::Sealed, Alpha(Shutter, L.Gap), L.Tilt); break;
		case EShutterLook::Hanging: Shutter->ApplyLayout(EProjectShutterLayoutState::Hanging, Alpha(Shutter, L.Gap), L.Tilt); break;
		default: Shutter->ApplyLayout(EProjectShutterLayoutState::Raised, true); break;
		}
	};
	for (int32 E = 0; E < Placement.SideShutters.Num(); ++E)
		for (int32 Side = 0; Side < Placement.SideShutters[E].Num(); ++Side)
			if (Ruin.EscalatorSides.IsValidIndex(E) && Ruin.EscalatorSides[E].IsValidIndex(Side)) Show(Placement.SideShutters[E][Side], Ruin.EscalatorSides[E][Side]);
	for (int32 S = 0; S < Placement.SlotShutters.Num(); ++S)
		if (Ruin.Slots.IsValidIndex(S)) Show(Placement.SlotShutters[S], Ruin.Slots[S]);
	for (AFireShutterActor* Shutter : Placement.OffLinkShutters)
	{
		UE_LOG(LogProjectShutterLayout, Warning, TEXT("%s is not on any route link; it stays raised."), *GetNameSafe(Shutter));
		Shutter->ApplyLayout(EProjectShutterLayoutState::Raised, true);
	}
	// The 1F exit is always shut: working shutters (closed, players raise them) mixed with broken ones that stay down.
	int32 ExitWorking = 0;
	for (const TArray<AFireShutterActor*>& Row : Placement.ExitRows)
	{
		if (Row.IsEmpty()) continue;
		float Width = 0.f; for (const AFireShutterActor* S : Row) Width += 300.f * FMath::Abs(S->GetActorScale3D().Y);
		const int32 Working = Row.Num() < 2 ? Row.Num() : FMath::Clamp(FMath::RoundToInt(Width / FMath::Max(300.f, ExitWorkingSpacing)), 1, Row.Num() - 1);
		TArray<int32> Order; for (int32 I = 0; I < Row.Num(); ++I) Order.Add(I);
		for (int32 I = Order.Num() - 1; I > 0; --I) Order.Swap(I, Look.RandRange(0, I));
		for (int32 K = 0; K < Order.Num(); ++K)
		{
			AFireShutterActor* Shutter = Row[Order[K]];
			if (K < Working) { Shutter->ApplyLayout(EProjectShutterLayoutState::Operable, 0.f, 0.f); WorkingShutters.Add(Shutter); ++ExitWorking; }
			else if (Look.FRand() < .5f) Shutter->ApplyLayout(EProjectShutterLayoutState::Sealed, 0.f, Look.FRandRange(-4.f, 4.f));
			else Shutter->ApplyLayout(EProjectShutterLayoutState::Sealed, Alpha(Shutter, Look.FRandRange(15.f, 35.f)), Look.FRandRange(-6.f, 6.f)); // jammed low: a gap to peek under, no way through
		}
	}
	for (AFireShutterActor* Shutter : Placement.StoreShutters)
	{
		// Store fronts: down, up, or half up so you can look under into the store (still operable).
		const float Roll = Look.FRand();
		const float Open = Roll < StoreClosedChance ? 0.f : Roll < StoreClosedChance + StoreHalfChance ? Alpha(Shutter, Look.FRandRange(90.f, 120.f)) : 1.f;
		Shutter->ApplyLayout(EProjectShutterLayoutState::Operable, Open, 0.f);
	}

	UE_LOG(LogProjectShutterLayout, Log, TEXT("Fire shutter layout seed=%d nodes=%d links=%d escalators=%d corridor slots=%d sealed corridors=%d working-shutter corridors=%d (chokepoints %d) exit rows=%d (working %d)%s%s"),
		Seed, Placement.Graph.Nodes.Num(), Placement.Graph.Edges.Num(), Placement.EscalatorIds.Num(), Placement.Graph.Slots.Num(),
		Result.SealedEdges.Num(), Result.DoorEdges.Num(), Result.ChokepointEdges, Placement.ExitRows.Num(), ExitWorking, Result.Note.IsEmpty() ? TEXT("") : TEXT(" note="), *Result.Note);
	return Result;
}
