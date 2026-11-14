#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ProjectShutterLayout.h"
#include "ProjectShutterLayoutRules.h"
#include "FireShutterActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "ProjectShutterNavigation.h"

namespace
{
constexpr int32 SampleColumns = 7;
int32 At(int32 Row, int32 Column) { return Row * SampleColumns + Column; }

/**
 * A 3rd-floor-like test floor: three corridor lanes (west to east) with cross links, two survivor spawns and three
 * escalator openings, each closed in on two sides. Every link has a corridor shutter slot; the middle lane is wide,
 * so its links carry a barrier of two shutters.
 */
FShutterLayoutGraph MakeSampleFloor()
{
	FShutterLayoutGraph G;
	for (int32 R = 0; R < 3; ++R) for (int32 C = 0; C < SampleColumns; ++C) G.AddNode(FVector2D(C * 2500.f, R * 2000.f));
	for (int32 R = 0; R < 3; ++R) for (int32 C = 0; C + 1 < SampleColumns; ++C) G.AddEdge(At(R, C), At(R, C + 1));
	for (int32 C = 0; C < SampleColumns; C += 2) G.AddEdge(At(0, C), At(1, C));
	for (int32 C = 1; C < SampleColumns; C += 2) G.AddEdge(At(1, C), At(2, C));
	for (int32 E = 0; E < G.Edges.Num(); ++E)
	{
		const bool bWide = G.Nodes[G.Edges[E].A].Y == 2000.f && G.Nodes[G.Edges[E].B].Y == 2000.f;
		if (bWide) { G.AddSlotOnEdge(E, .4f); G.AddSlotOnEdge(E, .6f); }
		else
		{
			// Cross links between the lanes are the passages between areas: chokepoints.
			const int32 S = G.AddSlotOnEdge(E);
			G.Slots[S].bChokepoint = G.Nodes[G.Edges[E].A].X == G.Nodes[G.Edges[E].B].X;
		}
	}
	G.Starts = { At(1, 0), At(0, 3) };
	G.Escalators.SetNum(3);
	G.Escalators[0].Sides = { At(0, 1), At(1, 1) };
	G.Escalators[1].Sides = { At(2, 4), At(1, 4) };
	G.Escalators[2].Sides = { At(0, 6), At(1, 6) };
	return G;
}

/** Draws a floor and a few rolled layouts side by side (Saved/ShutterLayoutQA), for people to look at. */
void WriteLayoutPreview(const FShutterLayoutGraph& G, const TArray<FShutterLayoutResult>& Layouts, const FString& File)
{
	const float Scale = .022f, Pad = 46.f, CellW = 6 * 2500 * Scale + Pad * 2, CellH = 2 * 2000 * Scale + Pad * 2 + 34;
	const int32 PerRow = 2, Rows = FMath::DivideAndRoundUp(Layouts.Num(), PerRow);
	FString Svg = FString::Printf(TEXT("<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' font-family='sans-serif' font-size='12'>"),
		int32(CellW * PerRow), int32(CellH * Rows + 40));
	Svg += TEXT("<rect width='100%' height='100%' fill='#1b1b1b'/>");
	Svg += FString::Printf(TEXT("<text x='12' y='%d' fill='#bbb'>blue: spawn  |  green bar: working shutter (opens)  |  red bar: broken shutter  |  square: escalator, green line = side that opens</text>"), int32(CellH * Rows + 24));
	for (int32 L = 0; L < Layouts.Num(); ++L)
	{
		const FShutterLayoutResult& R = Layouts[L];
		const float OX = (L % PerRow) * CellW + Pad, OY = (L / PerRow) * CellH + Pad + 22;
		auto P = [&](const FVector2D& V) { return FVector2D(OX + V.X * Scale, OY + V.Y * Scale); };
		Svg += FString::Printf(TEXT("<text x='%.0f' y='%.0f' fill='#ddd'>seed %d: broken corridors %d, working-shutter corridors %d</text>"),
			OX, OY - 26, R.Seed, R.SealedEdges.Num(), R.DoorEdges.Num());
		for (const FShutterLayoutGraph::FEdge& E : G.Edges)
		{
			const FVector2D A = P(G.Nodes[E.A]), B = P(G.Nodes[E.B]);
			Svg += FString::Printf(TEXT("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='#5e5e5e' stroke-width='12' stroke-linecap='round'/>"), A.X, A.Y, B.X, B.Y);
		}
		for (int32 S = 0; S < G.Slots.Num(); ++S)
		{
			if (R.SlotStates[S] == EShutterSlotState::Raised) continue;
			const FShutterLayoutGraph::FEdge& E = G.Edges[G.Slots[S].Edge];
			const FVector2D A = P(G.Nodes[E.A]), B = P(G.Nodes[E.B]), M = P(G.Slots[S].Location);
			const FVector2D N = (B - A).GetSafeNormal().GetRotated(90.f) * 12.f;
			Svg += FString::Printf(TEXT("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='%s' stroke-width='5'/>"), M.X - N.X, M.Y - N.Y, M.X + N.X, M.Y + N.Y,
				R.SlotStates[S] == EShutterSlotState::Door ? TEXT("#4fbf6a") : TEXT("#e0503c"));
		}
		for (int32 Esc = 0; Esc < G.Escalators.Num(); ++Esc)
		{
			const TArray<int32>& Sides = G.Escalators[Esc].Sides;
			FVector2D Mid = FVector2D::ZeroVector; for (const int32 Side : Sides) Mid += G.Nodes[Side]; Mid /= FMath::Max(1, Sides.Num());
			const FVector2D C = P(Mid) + FVector2D(18, 0);
			for (int32 Side = 0; Side < Sides.Num(); ++Side)
			{
				const FVector2D V = P(G.Nodes[Sides[Side]]);
				Svg += FString::Printf(TEXT("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='%s' stroke-width='4'/>"), C.X, C.Y, V.X, V.Y,
					R.EscalatorSide.IsValidIndex(Esc) && R.EscalatorSide[Esc] == Side ? TEXT("#4fbf6a") : TEXT("#e0503c"));
			}
			Svg += FString::Printf(TEXT("<rect x='%.1f' y='%.1f' width='18' height='18' fill='#c9b25a' stroke='#111'/><text x='%.1f' y='%.1f' fill='#111' font-size='11'>E%d</text>"),
				C.X - 9, C.Y - 9, C.X - 7, C.Y + 4, Esc + 1);
		}
		for (const int32 S : G.Starts)
		{
			const FVector2D V = P(G.Nodes[S]);
			Svg += FString::Printf(TEXT("<circle cx='%.1f' cy='%.1f' r='7' fill='#4a8fe0'/>"), V.X, V.Y);
		}
	}
	Svg += TEXT("</svg>");
	FFileHelper::SaveStringToFile(Svg, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectShutterLayoutRulesTest, "ProjectProject01.ShutterLayout.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectShutterLayoutRulesTest::RunTest(const FString&)
{
	const FShutterLayoutGraph G = MakeSampleFloor();
	const FShutterLayoutSettings S;
	TArray<TSet<int32>> SidesSeen; SidesSeen.SetNum(G.Escalators.Num());
	TSet<FString> Shapes; int32 Short = 0, Mixed = 0, Doors = 0, DoorsOnRoute = 0;
	bool bReach = true, bCount = true, bJunction = true, bSpacing = true, bProtected = true, bDetour = true, bOneWorking = true, bEveryEscalator = true;
	bool bNoAreaCutOff = true; int32 Chokes = 0, ChokesSealed = 0, CorridorMix = 0;
	bool bOpenRowsPassable = true, bLooksMatch = true; int32 SmashedRows = 0, ZonesSeen = 0;
	for (int32 Seed = 1; Seed <= 500; ++Seed)
	{
		const FShutterLayoutResult R = ProjectShutterLayout::Generate(G, S, Seed);
		for (int32 E = 0; E < G.Escalators.Num(); ++E) { SidesSeen[E].Add(R.EscalatorSide[E]); bEveryEscalator &= R.EscalatorSide[E] != INDEX_NONE; }
		Shapes.Add(FString::JoinBy(R.SlotStates, TEXT(""), [](EShutterSlotState V) { return FString::FromInt(int32(V)); }));
		Short += R.Note.IsEmpty() ? 0 : 1;
		Mixed += R.CountSlots(EShutterSlotState::Door) > 0 && R.CountSlots(EShutterSlotState::Sealed) > 0 ? 1 : 0;
		const int32 DoorChokepoints = R.ChokepointEdges - R.SealedChokepoints;
		bCount &= R.SealedEdges.Num() - R.SealedChokepoints <= S.MaxSealed && R.DoorEdges.Num() - DoorChokepoints <= S.MaxDoors && R.ChokepointEdges <= S.MaxChokepoints;
		Chokes += R.ChokepointEdges; ChokesSealed += R.SealedChokepoints;
		{
			// The ruined look never changes what is passable: an open row keeps a wide panel up, a closed one stays closed.
			const FShutterRuinSettings RS;
			const FShutterRuin Ruin = ProjectShutterLayout::RollRuin(G, R, RS, Seed);
			ZonesSeen += Ruin.Zones.Num();
			TMap<int32, TArray<int32>> RowsOf;
			for (int32 Slot = 0; Slot < G.Slots.Num(); ++Slot) RowsOf.FindOrAdd(G.Slots[Slot].Edge).Add(Slot);
			for (const TPair<int32, TArray<int32>>& Row : RowsOf)
			{
				bool bClosed = false, bWideOpen = false, bAnyDown = false;
				for (const int32 Slot : Row.Value)
				{
					const EShutterLook L = Ruin.Slots[Slot].Look;
					bClosed |= R.SlotStates[Slot] != EShutterSlotState::Raised;
					const bool bDown = L == EShutterLook::Down || L == EShutterLook::Jammed;
					bAnyDown |= bDown;
					bWideOpen |= !bDown && L != EShutterLook::Working && G.Slots[Slot].Width >= RS.MinOpenPanel;
					if (R.SlotStates[Slot] == EShutterSlotState::Door) bLooksMatch &= L == EShutterLook::Working;
					if (R.SlotStates[Slot] == EShutterSlotState::Sealed) bLooksMatch &= bDown;
					if (bDown) bLooksMatch &= Ruin.Slots[Slot].Gap <= 120.f;
					if (L == EShutterLook::Hanging) bLooksMatch &= Ruin.Slots[Slot].Gap >= 210.f;
				}
				if (!bClosed) { bOpenRowsPassable &= bWideOpen; SmashedRows += bAnyDown ? 1 : 0; }
			}
		}
		CorridorMix += R.SealedEdges.Num() > R.SealedChokepoints && R.DoorEdges.Num() > DoorChokepoints ? 1 : 0;
		{
			TBitArray<> Cut(false, G.Edges.Num()), None(false, G.Edges.Num());
			for (const int32 E : R.SealedEdges) Cut[E] = true;
			bNoAreaCutOff &= ProjectShutterLayout::CountReachable(G, Cut) == ProjectShutterLayout::CountReachable(G, None);
		}

		TBitArray<> Blocked(false, G.Edges.Num()), DoorBits(false, G.Edges.Num());
		for (const int32 E : R.SealedEdges) Blocked[E] = true;
		for (const int32 E : R.DoorEdges) DoorBits[E] = true;
		TArray<int32> Closed = R.SealedEdges; Closed.Append(R.DoorEdges);
		TMap<int32, int32> PerNode;
		for (const int32 E : Closed)
		{
			const FShutterLayoutGraph::FEdge& Edge = G.Edges[E];
			const int32 AtA = ++PerNode.FindOrAdd(Edge.A), AtB = ++PerNode.FindOrAdd(Edge.B);
			bJunction &= AtA <= 1 && AtB <= 1;
			bProtected &= !G.Starts.Contains(Edge.A) && !G.Starts.Contains(Edge.B) && !R.Targets.Contains(Edge.A) && !R.Targets.Contains(Edge.B);
			for (const int32 Other : Closed)
				if (Other != E)
					for (int32 A = 0; A < G.Slots.Num(); ++A)
						for (int32 B = 0; B < G.Slots.Num(); ++B)
							if (G.Slots[A].Edge == E && G.Slots[B].Edge == Other) bSpacing &= FVector2D::Distance(G.Slots[A].Location, G.Slots[B].Location) >= S.MinSealSpacing;
		}
		for (const int32 E : R.DoorEdges)
		{
			int32 Working = 0;
			for (int32 Slot = 0; Slot < G.Slots.Num(); ++Slot)
				if (G.Slots[Slot].Edge == E) Working += R.SlotStates[Slot] == EShutterSlotState::Door ? 1 : 0;
			bOneWorking &= Working == 1;
		}
		// Recomputed independently of what the generator reported: broken shutters cut, working ones do not.
		const TArray<float> Now = ProjectShutterLayout::PairDistances(G, R.Targets, Blocked, DoorBits, S.DoorCost);
		for (const float D : Now) bReach &= D >= 0.f;
		bDetour &= ProjectShutterLayout::WithinDetour(R.BaseDistances, Now, S);
		const TBitArray<> Route = ProjectShutterLayout::RouteEdges(G, R.Targets, Blocked);
		for (const int32 E : R.DoorEdges) { ++Doors; DoorsOnRoute += Route[E] ? 1 : 0; }
	}
	TestTrue(TEXT("Every escalator has a working side in every layout"), bEveryEscalator);
	for (int32 E = 0; E < SidesSeen.Num(); ++E) TestEqual(TEXT("Each side of an escalator is the working one in some matches"), SidesSeen[E].Num(), 2);
	TestTrue(TEXT("Every spawn reaches every escalator's working side in every layout"), bReach);
	TestTrue(TEXT("No layout makes a long detour"), bDetour);
	TestTrue(TEXT("Never more closed corridors than the maximum"), bCount);
	TestTrue(TEXT("No junction gets two closed corridors"), bJunction);
	TestTrue(TEXT("Closed corridors keep their spacing"), bSpacing);
	TestTrue(TEXT("Corridors next to spawns and escalator entrances stay open"), bProtected);
	TestTrue(TEXT("A closed barrier always has exactly one working shutter"), bOneWorking);
	TestTrue(TEXT("Working and broken shutters are mixed in almost every match"), Mixed >= 475);
	TestTrue(TEXT("No part of the floor is ever cut off"), bNoAreaCutOff);
	TestTrue(TEXT("A smashed open row always leaves a wide panel to pass"), bOpenRowsPassable);
	TestTrue(TEXT("Looks agree with what closes, and heights keep the rules"), bLooksMatch);
	TestTrue(TEXT("Some open rows look smashed"), SmashedRows > 0);
	TestTrue(TEXT("Damage zones appear"), ZonesSeen >= 500);
	TestTrue(TEXT("Some chokepoints are broken through, others have a working shutter"), ChokesSealed > 0 && ChokesSealed < Chokes);
	TestTrue(TEXT("Chokepoints get a working shutter in most matches"), Chokes >= 400);
	TestTrue(TEXT("Corridors get both broken and working shutters in most matches"), CorridorMix >= 400);
	TestTrue(TEXT("Working shutters sit mostly on the way to the escalators"), Doors > 0 && DoorsOnRoute * 10 >= Doors * 6);
	TestTrue(TEXT("Layouts differ from match to match"), Shapes.Num() >= 100);
	TestTrue(TEXT("The sample floor almost always fits the minimum"), Short <= 25);
	TestTrue(TEXT("The same seed gives the same layout"),
		ProjectShutterLayout::Generate(G, S, 77).SlotStates == ProjectShutterLayout::Generate(G, S, 77).SlotStates);
	AddInfo(FString::Printf(TEXT("500 layouts: %d short, %d mixed, %d corridor mixes, %d chokepoints, %d/%d working shutters on route, %d shapes"), Short, Mixed, CorridorMix, Chokes, DoorsOnRoute, Doors, Shapes.Num()));

	TArray<FShutterLayoutResult> Preview;
	for (const int32 Seed : { 3, 11, 25, 42, 58, 90 }) Preview.Add(ProjectShutterLayout::Generate(G, S, Seed));
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("ShutterLayoutQA");
	IFileManager::Get().MakeDirectory(*Dir, true);
	WriteLayoutPreview(G, Preview, Dir / TEXT("SampleFloor.svg"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectShutterLayoutEdgeCasesTest, "ProjectProject01.ShutterLayout.EdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectShutterLayoutEdgeCasesTest::RunTest(const FString&)
{
	FShutterLayoutSettings S; S.bProtectObjectiveEdges = false; S.MinSealSpacing = 0.f; S.MaxSealsPerNode = 4;
	S.MinSealed = 3; S.MaxSealed = 3; S.MinDoors = 0; S.MaxDoors = 0;
	{
		// A single corridor: every link is the only way, so no broken shutter may close it; a working one may.
		FShutterLayoutGraph G;
		for (int32 I = 0; I < 4; ++I) G.AddNode(FVector2D(I * 2000.f, 0));
		for (int32 I = 0; I < 3; ++I) G.AddSlotOnEdge(G.AddEdge(I, I + 1));
		G.Starts = { 0 }; G.Exits = { 3 };
		bool bNeverCut = true;
		for (int32 Seed = 1; Seed <= 50; ++Seed) bNeverCut &= ProjectShutterLayout::Generate(G, S, Seed).SealedEdges.IsEmpty();
		TestTrue(TEXT("A one-way corridor is never sealed by broken shutters"), bNeverCut);
		TestFalse(TEXT("Asking for more than fits says so instead of breaking the floor"), ProjectShutterLayout::Generate(G, S, 1).Note.IsEmpty());
		FShutterLayoutSettings DoorsOnly = S; DoorsOnly.MinSealed = 0; DoorsOnly.MaxSealed = 0; DoorsOnly.MinDoors = 2; DoorsOnly.MaxDoors = 2;
		TestEqual(TEXT("The same corridor may hold working shutters (they open)"), ProjectShutterLayout::Generate(G, DoorsOnly, 1).DoorEdges.Num(), 2);
	}
	{
		// A short way and a very long way around: closing the short one would be a maze-like detour.
		FShutterLayoutGraph G;
		const int32 A = G.AddNode(FVector2D(0, 0)), B = G.AddNode(FVector2D(1000, 0));
		const int32 C = G.AddNode(FVector2D(0, 3000)), D = G.AddNode(FVector2D(1000, 3000));
		const int32 Short = G.AddEdge(A, B); G.AddEdge(A, C); G.AddEdge(C, D); G.AddEdge(D, B);
		G.AddSlotOnEdge(Short);
		G.Starts = { A }; G.Exits = { B };
		S.MinSealed = 1; S.MaxSealed = 1;
		TestTrue(TEXT("A long detour is refused"), ProjectShutterLayout::Generate(G, S, 5).SealedEdges.IsEmpty());
		S.MaxDetourRatio = 8.f;
		TestEqual(TEXT("With a generous limit the same corridor may close"), ProjectShutterLayout::Generate(G, S, 5).SealedEdges.Num(), 1);
	}
	{
		// The exit floor: no escalators, the exit stays reachable.
		FShutterLayoutGraph G = MakeSampleFloor();
		G.Escalators.Reset(); G.Exits = { At(2, 6) };
		const FShutterLayoutResult R = ProjectShutterLayout::Generate(G, FShutterLayoutSettings(), 4);
		TestTrue(TEXT("Exit floor has only the exit as target"), R.Targets.Num() == 1 && R.Targets[0] == At(2, 6));
		for (const float D : R.FinalDistances) TestTrue(TEXT("Exit stays reachable"), D >= 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectShutterLayoutRuntimeTest, "ProjectProject01.ShutterLayout.Runtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectShutterLayoutRuntimeTest::RunTest(const FString&)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("Isolated layout world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->SetGameInstance(NewObject<UGameInstance>());
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// The sample floor as placed actors.
	const FShutterLayoutGraph G = MakeSampleFloor();
	TArray<AProjectRouteNode*> Nodes;
	for (const FVector2D& V : G.Nodes) Nodes.Add(World->SpawnActor<AProjectRouteNode>(AProjectRouteNode::StaticClass(), FTransform(FVector(V, 0)), Spawn));
	for (const FShutterLayoutGraph::FEdge& E : G.Edges) Nodes[E.A]->Links.Add(Nodes[E.B]); // One side is enough.
	for (const int32 S : G.Starts) Nodes[S]->bSurvivorStart = true;

	const FName Ids[] = { TEXT("ES_A"), TEXT("ES_B"), TEXT("ES_C") };
	TArray<TArray<AFireShutterActor*>> EscalatorShutters; EscalatorShutters.SetNum(3);
	TArray<AFireShutterActor*> CorridorShutters;
	for (int32 E = 0; E < 3; ++E)
		for (const int32 Side : G.Escalators[E].Sides)
		{
			// A shutter right in front of its junction, a little toward the opening.
			auto* Shutter = World->SpawnActor<AFireShutterActor>(AFireShutterActor::StaticClass(), FTransform(FVector(G.Nodes[Side] + FVector2D(150, 0), 0)), Spawn);
			Shutter->LayoutRole = EProjectShutterLayoutRole::Escalator; Shutter->EscalatorId = Ids[E];
			Shutter->DispatchBeginPlay(); EscalatorShutters[E].Add(Shutter);
		}
	for (const FShutterLayoutGraph::FSlot& Slot : G.Slots)
	{
		auto* Shutter = World->SpawnActor<AFireShutterActor>(AFireShutterActor::StaticClass(), FTransform(FVector(Slot.Location, 0)), Spawn);
		Shutter->LayoutRole = EProjectShutterLayoutRole::Corridor; Shutter->DispatchBeginPlay(); CorridorShutters.Add(Shutter);
	}
	auto* Store = World->SpawnActor<AFireShutterActor>(AFireShutterActor::StaticClass(), FTransform(FVector(0, 9000, 0)), Spawn);
	Store->LayoutRole = EProjectShutterLayoutRole::Store; Store->DispatchBeginPlay();
	auto* Plain = World->SpawnActor<AFireShutterActor>(AFireShutterActor::StaticClass(), FTransform(FVector(0, -9000, 0)), Spawn);
	Plain->DispatchBeginPlay();
	APawn* Viewer = World->SpawnActor<APawn>(APawn::StaticClass(), FTransform(FVector(0, 0, 500)), Spawn);
	auto* Layout = World->SpawnActor<AProjectShutterLayout>(AProjectShutterLayout::StaticClass(), FTransform::Identity, Spawn);

	Layout->Generate(1234);
	AFireShutterActor* AnyWorking = nullptr; AFireShutterActor* AnyBroken = nullptr;
	for (int32 E = 0; E < 3; ++E)
	{
		int32 Working = 0;
		for (AFireShutterActor* Shutter : EscalatorShutters[E])
		{
			const bool bWorking = Shutter->GetLayoutState() == EProjectShutterLayoutState::Operable;
			Working += bWorking ? 1 : 0;
			TestTrue(TEXT("An escalator shutter is working or broken"), bWorking || Shutter->GetLayoutState() == EProjectShutterLayoutState::Sealed);
			if (bWorking) TestEqual(TEXT("The working escalator shutter starts down"), Shutter->CurrentOpenAlpha, 0.f);
			else TestTrue(TEXT("A broken escalator shutter is down or jammed with a gap too low to pass"), Shutter->CurrentOpenAlpha * Shutter->GetOpenHeight() <= 120.f);
			TestEqual(TEXT("Only the working side can be opened"), Shutter->CanInteract_Implementation(Viewer), bWorking);
			TestTrue(TEXT("Broken: a wall for all; working and down: a wall for mannequins only"), Shutter->SealNavModifier != nullptr &&
				Shutter->SealNavModifier->AreaClass == (bWorking ? UNavArea_ProjectShutterDoor::StaticClass() : UNavArea_Null::StaticClass()));
			(bWorking ? AnyWorking : AnyBroken) = Shutter;
		}
		TestEqual(TEXT("Every escalator has exactly one working side"), Working, 1);
	}

	// The corridors as placed actors: what the actors show must be what a direct roll of the same floor decides.
	FShutterLayoutSettings Settings;
	Settings.MinSealed = Layout->MinSealedCorridors; Settings.MaxSealed = Layout->MaxSealedCorridors;
	Settings.MinDoors = Layout->MinWorkingCorridors; Settings.MaxDoors = Layout->MaxWorkingCorridors + Layout->MaxChokepoints;
	int32 Sealed = 0, Working = 0;
	for (const AFireShutterActor* Shutter : CorridorShutters)
	{
		const EProjectShutterLayoutState State = Shutter->GetLayoutState();
		TestTrue(TEXT("A corridor shutter is broken, working, raised or hanging"),
			State == EProjectShutterLayoutState::Sealed || State == EProjectShutterLayoutState::Operable || State == EProjectShutterLayoutState::Raised || State == EProjectShutterLayoutState::Hanging);
		Sealed += State == EProjectShutterLayoutState::Sealed ? 1 : 0;
		Working += State == EProjectShutterLayoutState::Operable ? 1 : 0;
		const float Gap = Shutter->CurrentOpenAlpha * Shutter->GetOpenHeight();
		switch (State)
		{
		case EProjectShutterLayoutState::Sealed: TestTrue(TEXT("A broken shutter leaves no gap anyone fits through"), Gap <= 120.f); break;
		case EProjectShutterLayoutState::Operable: TestEqual(TEXT("A working corridor shutter starts down"), Shutter->CurrentOpenAlpha, 0.f); break;
		case EProjectShutterLayoutState::Hanging:
			TestTrue(TEXT("A hanging shutter is high enough to walk under"), Gap >= 210.f);
			TestTrue(TEXT("A hanging shutter does not cut pathfinding"), Shutter->SealNavModifier == nullptr);
			break;
		default: TestEqual(TEXT("An unused shutter is tucked away"), Shutter->CurrentOpenAlpha, 1.f); break;
		}
		TestEqual(TEXT("Only working corridor shutters can be opened"), Shutter->CanInteract_Implementation(Viewer), State == EProjectShutterLayoutState::Operable);
	}
	TestTrue(TEXT("Working and broken shutters are both in the corridors"), Sealed >= 1 && Working >= 1);

	// The ruined look over several matches: fully down, jammed with a gap, bent, hanging overhead.
	{
		int32 Down = 0, Jammed = 0, Bent = 0, Hanging = 0;
		for (int32 Seed = 1; Seed <= 30; ++Seed)
		{
			Layout->Generate(Seed);
			for (const auto& Sides : EscalatorShutters)
				for (const AFireShutterActor* S : Sides)
					if (S->GetLayoutState() == EProjectShutterLayoutState::Sealed)
					{
						(S->CurrentOpenAlpha <= 0.f ? Down : Jammed)++;
						Bent += FMath::Abs(S->BrokenTilt) > .5f ? 1 : 0;
					}
			for (const AFireShutterActor* S : CorridorShutters) Hanging += S->GetLayoutState() == EProjectShutterLayoutState::Hanging ? 1 : 0;
		}
		TestTrue(TEXT("Some broken shutters are all the way down"), Down > 0);
		TestTrue(TEXT("Some broken shutters are jammed with a gap"), Jammed > 0);
		TestTrue(TEXT("Broken shutters hang crooked"), Bent > 0);
		TestTrue(TEXT("Some unused shutters hang overhead"), Hanging > 0);
		AddInfo(FString::Printf(TEXT("30 matches: broken escalator shutters %d down, %d jammed, %d bent; %d hanging corridor shutters"), Down, Jammed, Bent, Hanging));
		Layout->Generate(1234);
	}
	TestTrue(TEXT("A few working-shutter corridors"), Working >= Settings.MinDoors && Working <= Settings.MaxDoors);
	TestTrue(TEXT("A store shutter stays operable"), Store->GetLayoutState() == EProjectShutterLayoutState::Operable);
	TestTrue(TEXT("A shutter outside the layout is untouched"), Plain->GetLayoutState() == EProjectShutterLayoutState::Default);
	TestTrue(TEXT("A shutter outside the layout still works as before"), Plain->CanInteract_Implementation(Viewer));

	// Bots cannot press F: walking up to a working shutter opens it; a broken one never opens.
	if (TestNotNull(TEXT("A working escalator shutter"), AnyWorking) && TestNotNull(TEXT("A broken escalator shutter"), AnyBroken))
	{
		AFireShutterActor* Far = nullptr;
		for (const auto& Sides : EscalatorShutters)
			for (AFireShutterActor* S : Sides)
				if (S->GetLayoutState() == EProjectShutterLayoutState::Operable && FVector::Dist2D(S->GetActorLocation(), AnyWorking->GetActorLocation()) > 3000.f) Far = S;
		Layout->OpenWorkingShuttersNear({ AnyWorking->GetActorLocation() + FVector(100, 0, 0), AnyBroken->GetActorLocation() + FVector(100, 0, 0) });
		TestFalse(TEXT("A bot opens the working shutter it reaches"), AnyWorking->IsHeadingDown());
		TestTrue(TEXT("An opening working shutter no longer blocks mannequins"), AnyWorking->SealNavModifier == nullptr);
		TestTrue(TEXT("A bot cannot open a broken shutter"), AnyBroken->IsHeadingDown());
		if (TestNotNull(TEXT("Another working escalator shutter far away"), Far)) TestTrue(TEXT("A working shutter far from any bot stays down"), Far->IsHeadingDown());
	}

	// A second roll with the same seed gives the same floor.
	TArray<EProjectShutterLayoutState> First; for (const AFireShutterActor* S : CorridorShutters) First.Add(S->GetLayoutState());
	Layout->Generate(1234);
	TArray<EProjectShutterLayoutState> Second; for (const AFireShutterActor* S : CorridorShutters) Second.Add(S->GetLayoutState());
	TestTrue(TEXT("Same seed, same layout"), First == Second);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectShutterLayoutHorrorTest, "ProjectProject01.ShutterLayout.Horror",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectShutterLayoutHorrorTest::RunTest(const FString&)
{
	// Mannequins start in the middle lane and the far east; survivors start in the west.
	FShutterLayoutGraph G = MakeSampleFloor();
	G.Threat.Init(0.f, G.Nodes.Num());
	for (const int32 N : { At(1, 3), At(2, 2), At(0, 5), At(2, 6), At(1, 5) }) G.Threat[N] = 1.f;
	const TArray<float> Field = ProjectShutterLayout::ThreatField(G, 1500.f);
	TArray<float> FromSpawns; FromSpawns.Init(-1.f, G.Nodes.Num());
	{
		const TBitArray<> None(false, G.Edges.Num()); TArray<float> D;
		for (const int32 Start : G.Starts)
		{
			ProjectShutterLayout::ShortestDistances(G, None, None, 0.f, Start, D);
			for (int32 N = 0; N < D.Num(); ++N) if (D[N] >= 0.f && (FromSpawns[N] < 0.f || D[N] < FromSpawns[N])) FromSpawns[N] = D[N];
		}
	}
	struct FScore { double BehindDoor = 0; int32 Doors = 0; double Ambush = 0; double FarSide = 0; int32 Sides = 0; bool bFair = true; };
	auto Measure = [&](float Horror)
	{
		FShutterLayoutSettings S; S.HorrorWeight = Horror;
		FScore Out;
		for (int32 Seed = 1; Seed <= 400; ++Seed)
		{
			const FShutterLayoutResult R = ProjectShutterLayout::Generate(G, S, Seed);
			for (const int32 E : R.DoorEdges)
			{
				const int32 Behind = FromSpawns[G.Edges[E].A] > FromSpawns[G.Edges[E].B] ? G.Edges[E].A : G.Edges[E].B;
				Out.BehindDoor += Field[Behind]; ++Out.Doors;
			}
			TBitArray<> Blocked(false, G.Edges.Num()), Doors(false, G.Edges.Num());
			for (const int32 E : R.SealedEdges) Blocked[E] = true;
			for (const int32 E : R.DoorEdges) Doors[E] = true;
			Out.Ambush += ProjectShutterLayout::AmbushJunctions(G, R.Targets, Blocked, Field);
			for (int32 E = 0; E < G.Escalators.Num(); ++E)
			{
				const TArray<int32>& Sides = G.Escalators[E].Sides;
				Out.FarSide += FromSpawns[Sides[R.EscalatorSide[E]]] >= FromSpawns[Sides[1 - R.EscalatorSide[E]]] ? 1 : 0; ++Out.Sides;
			}
			const TArray<float> Now = ProjectShutterLayout::PairDistances(G, R.Targets, Blocked, Doors, S.DoorCost);
			for (const float D : Now) Out.bFair &= D >= 0.f;
			Out.bFair &= ProjectShutterLayout::WithinDetour(R.BaseDistances, Now, S);
		}
		return Out;
	};
	const FScore Plain = Measure(0.f), Horror = Measure(1.f);
	TestTrue(TEXT("Fairness holds with and without the horror weighting"), Plain.bFair && Horror.bFair);
	const double PlainBehind = Plain.BehindDoor / FMath::Max(1, Plain.Doors), HorrorBehind = Horror.BehindDoor / FMath::Max(1, Horror.Doors);
	TestTrue(TEXT("Working shutters open onto mannequin areas more often"), HorrorBehind > PlainBehind * 1.1);
	TestTrue(TEXT("The way passes more ambush junctions"), Horror.Ambush >= Plain.Ambush);
	TestTrue(TEXT("The far side of an escalator opens more often"), Horror.FarSide > Plain.FarSide);
	AddInfo(FString::Printf(TEXT("threat behind working shutters %.2f -> %.2f, ambush junctions %.2f -> %.2f, far escalator side %.0f%% -> %.0f%%"),
		PlainBehind, HorrorBehind, Plain.Ambush / 400, Horror.Ambush / 400, 100 * Plain.FarSide / FMath::Max(1, Plain.Sides), 100 * Horror.FarSide / FMath::Max(1, Horror.Sides)));
	return true;
}
#endif

