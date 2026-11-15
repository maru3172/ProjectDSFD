#include "ProjectShutterLayoutRules.h"

#include "Math/RandomStream.h"

int32 FShutterLayoutGraph::AddEdge(const int32 A, const int32 B)
{
	if (!Nodes.IsValidIndex(A) || !Nodes.IsValidIndex(B) || A == B) return INDEX_NONE;
	for (int32 I = 0; I < Edges.Num(); ++I)
		if ((Edges[I].A == A && Edges[I].B == B) || (Edges[I].A == B && Edges[I].B == A)) return I;
	FEdge Edge; Edge.A = A; Edge.B = B; Edge.Length = FMath::Max(1.f, float(FVector2D::Distance(Nodes[A], Nodes[B])));
	return Edges.Add(Edge);
}

int32 FShutterLayoutGraph::AddSlotOnEdge(const int32 Edge, const float Alpha)
{
	if (!Edges.IsValidIndex(Edge)) return INDEX_NONE;
	FSlot Slot; Slot.Edge = Edge; Slot.Location = FMath::Lerp(Nodes[Edges[Edge].A], Nodes[Edges[Edge].B], FMath::Clamp(Alpha, 0.f, 1.f));
	return Slots.Add(Slot);
}

int32 FShutterLayoutGraph::FindNearestEdge(const FVector2D& Point, const float MaxDistance) const
{
	int32 Best = INDEX_NONE; double BestDistance = double(MaxDistance);
	for (int32 I = 0; I < Edges.Num(); ++I)
	{
		const FVector2D A = Nodes[Edges[I].A], B = Nodes[Edges[I].B];
		const FVector2D AB = B - A; const double LengthSquared = AB.SizeSquared();
		const double T = LengthSquared > UE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSquared, 0., 1.) : 0.;
		const double Distance = FVector2D::Distance(Point, A + AB * T);
		if (Distance <= BestDistance) { BestDistance = Distance; Best = I; }
	}
	return Best;
}

namespace ProjectShutterLayout
{
namespace
{
bool IsSet(const TBitArray<>& Bits, const int32 Index) { return Bits.IsValidIndex(Index) && Bits[Index]; }

/** Node -> (neighbour, edge). Real floors have hundreds of nodes, so pathfinding uses a heap, not a scan. */
TArray<TArray<TPair<int32, int32>>> Adjacency(const FShutterLayoutGraph& Graph)
{
	TArray<TArray<TPair<int32, int32>>> Out; Out.SetNum(Graph.Nodes.Num());
	for (int32 E = 0; E < Graph.Edges.Num(); ++E)
	{
		Out[Graph.Edges[E].A].Emplace(Graph.Edges[E].B, E);
		Out[Graph.Edges[E].B].Emplace(Graph.Edges[E].A, E);
	}
	return Out;
}

/** Uniform shuffle (Weights empty or HorrorWeight 0) or weighted: a heavier item tends to come first, any can. */
TArray<int32> Ordered(const TArray<int32>& Items, const TArray<float>& Weights, FRandomStream& Random)
{
	TArray<TPair<double, int32>> Keyed;
	for (int32 I = 0; I < Items.Num(); ++I)
	{
		const double U = FMath::Max(1e-9, double(Random.FRand()));
		const double W = Weights.IsValidIndex(I) ? FMath::Max(1e-3, double(Weights[I])) : 1.;
		Keyed.Emplace(FMath::Loge(U) / W, Items[I]); // Efraimidis-Spirakis: larger key first.
	}
	Keyed.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key > B.Key; });
	TArray<int32> Out; for (const TPair<double, int32>& K : Keyed) Out.Add(K.Value);
	return Out;
}

/** How far a straight corridor runs through edge E (both ways): long runs are long sightlines. */
float StraightRun(const FShutterLayoutGraph& Graph, const TArray<TArray<TPair<int32, int32>>>& Adj, const int32 E)
{
	const FVector2D Dir = (Graph.Nodes[Graph.Edges[E].B] - Graph.Nodes[Graph.Edges[E].A]).GetSafeNormal();
	float Run = Graph.Edges[E].Length;
	for (int32 Way = 0; Way < 2; ++Way)
	{
		int32 Node = Way == 0 ? Graph.Edges[E].B : Graph.Edges[E].A;
		const FVector2D Forward = Way == 0 ? Dir : -Dir;
		for (int32 Step = 0; Step < 64; ++Step)
		{
			int32 Next = INDEX_NONE, NextEdge = INDEX_NONE;
			for (const TPair<int32, int32>& N : Adj[Node])
				if (FVector2D::DotProduct((Graph.Nodes[N.Key] - Graph.Nodes[Node]).GetSafeNormal(), Forward) > .966f) { Next = N.Key; NextEdge = N.Value; break; }
			if (Next == INDEX_NONE) break;
			Run += Graph.Edges[NextEdge].Length; Node = Next;
		}
	}
	return Run;
}
}

void ShortestDistances(const FShutterLayoutGraph& Graph, const TBitArray<>& Blocked, const TBitArray<>& Doors, const float DoorCost, const int32 Start, TArray<float>& OutDistance)
{
	OutDistance.Init(-1.f, Graph.Nodes.Num());
	if (!Graph.Nodes.IsValidIndex(Start)) return;
	const TArray<TArray<TPair<int32, int32>>> Adj = Adjacency(Graph);
	TArray<TPair<float, int32>> Heap;
	auto Less = [](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; };
	OutDistance[Start] = 0.f; Heap.HeapPush(TPair<float, int32>(0.f, Start), Less);
	while (!Heap.IsEmpty())
	{
		TPair<float, int32> Top; Heap.HeapPop(Top, Less);
		if (Top.Key > OutDistance[Top.Value]) continue;
		for (const TPair<int32, int32>& N : Adj[Top.Value])
		{
			if (IsSet(Blocked, N.Value)) continue;
			const float Candidate = Top.Key + Graph.Edges[N.Value].Length + (IsSet(Doors, N.Value) ? FMath::Max(0.f, DoorCost) : 0.f);
			if (OutDistance[N.Key] < 0.f || Candidate < OutDistance[N.Key]) { OutDistance[N.Key] = Candidate; Heap.HeapPush(TPair<float, int32>(Candidate, N.Key), Less); }
		}
	}
}

TArray<float> PairDistances(const FShutterLayoutGraph& Graph, const TArray<int32>& Targets, const TBitArray<>& Blocked, const TBitArray<>& Doors, const float DoorCost)
{
	TArray<float> Pairs; Pairs.Reserve(Graph.Starts.Num() * Targets.Num());
	TArray<float> Distance;
	for (const int32 Start : Graph.Starts)
	{
		ShortestDistances(Graph, Blocked, Doors, DoorCost, Start, Distance);
		for (const int32 Target : Targets) Pairs.Add(Distance.IsValidIndex(Target) ? Distance[Target] : -1.f);
	}
	return Pairs;
}

int32 CountReachable(const FShutterLayoutGraph& Graph, const TBitArray<>& Blocked)
{
	const TArray<TArray<TPair<int32, int32>>> Adj = Adjacency(Graph);
	TBitArray<> Seen(false, Graph.Nodes.Num());
	TArray<int32> Stack;
	for (const int32 Start : Graph.Starts) if (Graph.Nodes.IsValidIndex(Start) && !Seen[Start]) { Seen[Start] = true; Stack.Add(Start); }
	int32 Count = Stack.Num();
	while (!Stack.IsEmpty())
	{
		const int32 U = Stack.Pop(EAllowShrinking::No);
		for (const TPair<int32, int32>& N : Adj[U])
			if (!IsSet(Blocked, N.Value) && !Seen[N.Key]) { Seen[N.Key] = true; Stack.Add(N.Key); ++Count; }
	}
	return Count;
}

bool WithinDetour(const TArray<float>& Base, const TArray<float>& Now, const FShutterLayoutSettings& Settings)
{
	if (Base.Num() != Now.Num()) return false;
	for (int32 I = 0; I < Base.Num(); ++I)
	{
		if (Base[I] < 0.f) continue; // Unreachable without any shutter: the map's own problem, not ours to judge.
		if (Now[I] < 0.f || Now[I] > Base[I] * FMath::Max(1.f, Settings.MaxDetourRatio) + FMath::Max(0.f, Settings.DetourSlack)) return false;
	}
	return true;
}

TBitArray<> RouteEdges(const FShutterLayoutGraph& Graph, const TArray<int32>& Targets, const TBitArray<>& Blocked)
{
	TBitArray<> OnRoute(false, Graph.Edges.Num());
	const TBitArray<> NoDoors(false, Graph.Edges.Num());
	TArray<TArray<float>> FromTarget; FromTarget.SetNum(Targets.Num());
	for (int32 T = 0; T < Targets.Num(); ++T) ShortestDistances(Graph, Blocked, NoDoors, 0.f, Targets[T], FromTarget[T]);
	TArray<float> FromStart;
	for (const int32 Start : Graph.Starts)
	{
		ShortestDistances(Graph, Blocked, NoDoors, 0.f, Start, FromStart);
		for (int32 T = 0; T < Targets.Num(); ++T)
		{
			const float Best = FromStart.IsValidIndex(Targets[T]) ? FromStart[Targets[T]] : -1.f;
			if (Best < 0.f) continue;
			for (int32 E = 0; E < Graph.Edges.Num(); ++E)
			{
				if (IsSet(Blocked, E)) continue;
				const FShutterLayoutGraph::FEdge& Edge = Graph.Edges[E];
				for (int32 Way = 0; Way < 2; ++Way)
				{
					const int32 U = Way == 0 ? Edge.A : Edge.B, V = Way == 0 ? Edge.B : Edge.A;
					if (FromStart[U] >= 0.f && FromTarget[T][V] >= 0.f && FMath::IsNearlyEqual(FromStart[U] + Edge.Length + FromTarget[T][V], Best, 1.f))
						OnRoute[E] = true;
				}
			}
		}
	}
	return OnRoute;
}

TArray<float> ThreatField(const FShutterLayoutGraph& Graph, const float Radius)
{
	TArray<float> Field; Field.Init(0.f, Graph.Nodes.Num());
	if (Graph.Threat.Num() != Graph.Nodes.Num()) return Field;
	// Where mannequins start (strong early) ...
	TArray<float> Spawn; Spawn.Init(0.f, Graph.Nodes.Num());
	float MaxSpawn = 0.f;
	for (int32 N = 0; N < Graph.Nodes.Num(); ++N)
	{
		for (int32 M = 0; M < Graph.Nodes.Num(); ++M)
			if (Graph.Threat[M] > 0.f && FVector2D::Distance(Graph.Nodes[N], Graph.Nodes[M]) <= Radius) Spawn[N] += Graph.Threat[M];
		MaxSpawn = FMath::Max(MaxSpawn, Spawn[N]);
	}
	// ... and where anyone walking the floor passes most (mannequins patrol and chase all match long): how many
	// shortest ways between sampled places run through each node. Fixed sample, so the same floor gives the same field.
	TArray<float> Traffic; Traffic.Init(0.f, Graph.Nodes.Num());
	float MaxTraffic = 0.f;
	if (Graph.Nodes.Num() > 2)
	{
		FRandomStream Sample(0x5EED);
		const TBitArray<> None(false, Graph.Edges.Num());
		TArray<float> FromA, FromB;
		for (int32 K = 0; K < 32; ++K)
		{
			const int32 A = Sample.RandRange(0, Graph.Nodes.Num() - 1), B = Sample.RandRange(0, Graph.Nodes.Num() - 1);
			if (A == B) continue;
			ShortestDistances(Graph, None, None, 0.f, A, FromA);
			if (FromA[B] < 0.f) continue;
			ShortestDistances(Graph, None, None, 0.f, B, FromB);
			for (int32 N = 0; N < Graph.Nodes.Num(); ++N)
				if (FromA[N] >= 0.f && FromB[N] >= 0.f && FromA[N] + FromB[N] <= FromA[B] + 1.f) Traffic[N] += 1.f;
		}
		for (const float T : Traffic) MaxTraffic = FMath::Max(MaxTraffic, T);
	}
	float Max = 0.f;
	for (int32 N = 0; N < Graph.Nodes.Num(); ++N)
	{
		Field[N] = (MaxSpawn > 0.f ? Spawn[N] / MaxSpawn : 0.f) * .5f + (MaxTraffic > 0.f ? Traffic[N] / MaxTraffic : 0.f) * .5f;
		Max = FMath::Max(Max, Field[N]);
	}
	if (Max > 0.f) for (float& F : Field) F /= Max;
	return Field;
}

int32 AmbushJunctions(const FShutterLayoutGraph& Graph, const TArray<int32>& Targets, const TBitArray<>& Blocked, const TArray<float>& Field)
{
	const TBitArray<> Route = RouteEdges(Graph, Targets, Blocked);
	TArray<int32> OpenDegree; OpenDegree.Init(0, Graph.Nodes.Num());
	TBitArray<> OnWay(false, Graph.Nodes.Num());
	for (int32 E = 0; E < Graph.Edges.Num(); ++E)
	{
		if (IsSet(Blocked, E)) continue;
		++OpenDegree[Graph.Edges[E].A]; ++OpenDegree[Graph.Edges[E].B];
		if (Route[E]) { OnWay[Graph.Edges[E].A] = true; OnWay[Graph.Edges[E].B] = true; }
	}
	// A junction is where a narrow way opens into others: 3+ open links, at least one of them leading to a node
	// with only 2 (a corridor mouth). Inside an open hall every node has 3-4 links, and those are not junctions.
	const TArray<TArray<TPair<int32, int32>>> Adj = Adjacency(Graph);
	int32 Count = 0;
	for (int32 N = 0; N < Graph.Nodes.Num(); ++N)
	{
		if (!OnWay[N] || OpenDegree[N] < 3 || !Field.IsValidIndex(N) || Field[N] < .2f) continue;
		bool bMouth = false;
		for (const TPair<int32, int32>& Next : Adj[N]) bMouth |= !IsSet(Blocked, Next.Value) && OpenDegree[Next.Key] <= 2;
		Count += bMouth ? 1 : 0;
	}
	return Count;
}

FShutterLayoutResult Generate(const FShutterLayoutGraph& Graph, const FShutterLayoutSettings& Settings, const int32 Seed)
{
	FShutterLayoutResult Result; Result.Seed = Seed;
	FRandomStream Random(Seed);
	const float Horror = FMath::Max(0.f, Settings.HorrorWeight);
	const TArray<float> Field = ThreatField(Graph, Settings.ThreatRadius);
	const TArray<TArray<TPair<int32, int32>>> Adj = Adjacency(Graph);
	const TBitArray<> None(false, Graph.Edges.Num());

	// How far each node is from the nearest spawn (no shutters): "behind" a corridor means further from the spawns.
	TArray<float> FromSpawns; FromSpawns.Init(-1.f, Graph.Nodes.Num());
	{
		TArray<float> D;
		for (const int32 Start : Graph.Starts)
		{
			ShortestDistances(Graph, None, None, 0.f, Start, D);
			for (int32 N = 0; N < D.Num(); ++N) if (D[N] >= 0.f && (FromSpawns[N] < 0.f || D[N] < FromSpawns[N])) FromSpawns[N] = D[N];
		}
	}

	// 1. Each escalator's working side. Horror: the far side, so survivors walk around the opening first.
	for (const FShutterLayoutGraph::FEscalator& Escalator : Graph.Escalators)
	{
		int32 Side = INDEX_NONE;
		if (!Escalator.Sides.IsEmpty())
		{
			float Far = 1.f; for (const int32 S : Escalator.Sides) if (FromSpawns.IsValidIndex(S)) Far = FMath::Max(Far, FromSpawns[S]);
			TArray<int32> Index; TArray<float> Weight;
			for (int32 I = 0; I < Escalator.Sides.Num(); ++I)
			{
				const int32 Node = Escalator.Sides[I];
				Index.Add(I); Weight.Add(FMath::Exp(Horror * 2.f * (FromSpawns.IsValidIndex(Node) ? FMath::Max(0.f, FromSpawns[Node]) / Far : 0.f)));
			}
			Side = Ordered(Index, Weight, Random)[0];
		}
		Result.EscalatorSide.Add(Side);
		if (Side != INDEX_NONE && Graph.Nodes.IsValidIndex(Escalator.Sides[Side])) Result.Targets.AddUnique(Escalator.Sides[Side]);
	}
	for (const int32 Exit : Graph.Exits) if (Graph.Nodes.IsValidIndex(Exit)) Result.Targets.AddUnique(Exit);

	TBitArray<> Blocked(false, Graph.Edges.Num()), Doors(false, Graph.Edges.Num());
	Result.BaseDistances = PairDistances(Graph, Result.Targets, Blocked, Doors, Settings.DoorCost);
	Result.FinalDistances = Result.BaseDistances;
	Result.SlotStates.Init(EShutterSlotState::Raised, Graph.Slots.Num());
	if (Graph.Slots.IsEmpty()) return Result;
	if (Graph.Starts.IsEmpty() || Result.Targets.IsEmpty())
	{
		Result.Note = TEXT("no spawn or target nodes: corridor shutters stay open");
		return Result;
	}

	TMap<int32, TArray<int32>> EdgeSlots;
	for (int32 S = 0; S < Graph.Slots.Num(); ++S) if (Graph.Edges.IsValidIndex(Graph.Slots[S].Edge)) EdgeSlots.FindOrAdd(Graph.Slots[S].Edge).Add(S);
	TArray<int32> Candidates; EdgeSlots.GetKeys(Candidates); Candidates.Sort();
	// Chokepoint slots are the openings of fire compartment lines (Docs/FireShutterLayout.md): they close together along
	// a line, so the spacing and per-junction limits (meant to keep scattered corridor shutters apart) do not apply.
	auto IsCompartment = [&](const int32 E) { for (const int32 S : EdgeSlots[E]) if (Graph.Slots[S].bChokepoint) return true; return false; };
	auto RowWidth = [&](const int32 E) { float W = 0.f; for (const int32 S : EdgeSlots[E]) W += Graph.Slots[S].Width; return W; };
	// Main corridors stay open unless they are a compartment line: wide scattered rows never close (only look smashed).
	Candidates.RemoveAll([&](const int32 E) { return !IsCompartment(E) && RowWidth(E) > Settings.MaxClosableRowWidth; });
	// Some compartment lines stay open this match (no shutters along them): zones that are simply not blocked.
	{
		TSet<int32> Lines; for (const FShutterLayoutGraph::FSlot& Slot : Graph.Slots) if (Slot.Line != INDEX_NONE) Lines.Add(Slot.Line);
		TArray<int32> Sorted = Lines.Array(); Sorted.Sort();
		for (const int32 Line : Sorted)
			if (Random.FRand() < Settings.OpenLineChance) Result.OpenLines.Add(Line);
		Candidates.RemoveAll([&](const int32 E) { for (const int32 S : EdgeSlots[E]) if (Result.OpenLines.Contains(Graph.Slots[S].Line)) return true; return false; });
	}

	TSet<int32> Protected;
	if (Settings.bProtectObjectiveEdges) { Protected.Append(Graph.Starts); Protected.Append(Result.Targets); }
	TArray<int32> ClosedAtNode; ClosedAtNode.Init(0, Graph.Nodes.Num());
	TArray<FVector2D> ClosedAt;
	// Chokepoints: not next to a spawn/escalator side/exit, one closed corridor per junction, closed corridors spread out.
	auto MayClose = [&](const int32 E)
	{
		const FShutterLayoutGraph::FEdge& Edge = Graph.Edges[E];
		if (Blocked[E] || Doors[E] || Protected.Contains(Edge.A) || Protected.Contains(Edge.B)) return false;
		if (IsCompartment(E)) return true;
		if (ClosedAtNode[Edge.A] >= Settings.MaxSealsPerNode || ClosedAtNode[Edge.B] >= Settings.MaxSealsPerNode) return false;
		for (const int32 S : EdgeSlots[E])
			for (const FVector2D& Other : ClosedAt)
				if (FVector2D::Distance(Other, Graph.Slots[S].Location) < Settings.MinSealSpacing) return false;
		return true;
	};
	// Closing a corridor closes every link its shutters stand across. Returns what changed, so a try can be undone exactly.
	auto Cut = [&](TBitArray<>& Bits, const int32 E)
	{
		TArray<int32> Changed;
		auto Set = [&](const int32 X) { if (Bits.IsValidIndex(X) && !Bits[X]) { Bits[X] = true; Changed.Add(X); } };
		Set(E);
		for (const int32 S : EdgeSlots[E]) for (const int32 X : Graph.Slots[S].Crosses) Set(X);
		return Changed;
	};
	auto Uncut = [](TBitArray<>& Bits, const TArray<int32>& Changed) { for (const int32 X : Changed) Bits[X] = false; };
	auto Close = [&](const int32 E)
	{
		++ClosedAtNode[Graph.Edges[E].A]; ++ClosedAtNode[Graph.Edges[E].B];
		for (const int32 S : EdgeSlots[E]) ClosedAt.Add(Graph.Slots[S].Location);
	};
	// Every spawn must still reach every escalator's working side / the exit, without a long detour, and no part of the
	// floor that was reachable may be cut off (working shutters open, so only broken ones count).
	const int32 BaseReach = CountReachable(Graph, Blocked);
	auto StillFine = [&]
	{
		if (CountReachable(Graph, Blocked) < BaseReach) return false;
		const TArray<float> Now = PairDistances(Graph, Result.Targets, Blocked, Doors, Settings.DoorCost);
		if (!WithinDetour(Result.BaseDistances, Now, Settings)) return false;
		Result.FinalDistances = Now;
		return true;
	};

	// 2. Broken shutters. Horror: cut long sightlines, and send the way through junctions mannequins reach from several sides.
	TArray<float> SealWeight;
	if (Horror > 0.f)
	{
		const int32 AmbushBefore = AmbushJunctions(Graph, Result.Targets, Blocked, Field);
		TArray<float> Gain; float MaxGain = 0.f;
		for (const int32 E : Candidates)
		{
			float G = 0.f;
			if (MayClose(E))
			{
				const TArray<int32> Changed = Cut(Blocked, E);
				G = float(FMath::Max(0, AmbushJunctions(Graph, Result.Targets, Blocked, Field) - AmbushBefore));
				Uncut(Blocked, Changed);
			}
			Gain.Add(G); MaxGain = FMath::Max(MaxGain, G);
		}
		for (int32 I = 0; I < Candidates.Num(); ++I)
		{
			const float Sight = FMath::Clamp(StraightRun(Graph, Adj, Candidates[I]) / 3000.f, 0.f, 1.f);
			SealWeight.Add(FMath::Exp(Horror * (2.f * Sight + 1.5f * (MaxGain > 0.f ? Gain[I] / MaxGain : 0.f))));
		}
	}
	const int32 MinSealed = FMath::Clamp(Settings.MinSealed, 0, FMath::Max(0, Settings.MaxSealed));
	const int32 WantedSealed = Random.RandRange(MinSealed, FMath::Max(MinSealed, Settings.MaxSealed));
	auto IsChokepoint = [&](const int32 E) { for (const int32 S : EdgeSlots[E]) if (Graph.Slots[S].bChokepoint) return true; return false; };
	for (const int32 E : Ordered(Candidates, SealWeight, Random))
	{
		if (Result.SealedEdges.Num() >= WantedSealed) break;
		if (IsChokepoint(E) || !MayClose(E)) continue; // A chokepoint is never closed by broken shutters only.
		const TArray<int32> Changed = Cut(Blocked, E);
		if (!StillFine()) { Uncut(Blocked, Changed); continue; }
		Result.SealedEdges.Add(E); Close(E);
		for (const int32 S : EdgeSlots[E]) Result.SlotStates[S] = EShutterSlotState::Sealed;
	}

	// 3. Working shutters mixed in, mostly on the way to the escalators: a closed corridor, and exactly one of its
	//    shutters opens. Horror: what lies behind (further from the spawns) is where mannequins are likely to wait.
	const int32 MinDoors = FMath::Clamp(Settings.MinDoors, 0, FMath::Max(0, Settings.MaxDoors));
	const int32 WantedDoors = Random.RandRange(MinDoors, FMath::Max(MinDoors, Settings.MaxDoors));
	const TBitArray<> OnRoute = RouteEdges(Graph, Result.Targets, Blocked);
	TArray<float> DoorWeight;
	for (const int32 E : Candidates)
	{
		const FShutterLayoutGraph::FEdge& Edge = Graph.Edges[E];
		const int32 Behind = FromSpawns[Edge.A] > FromSpawns[Edge.B] ? Edge.A : Edge.B;
		DoorWeight.Add((OnRoute[E] ? 1.f : .15f) * FMath::Exp(Horror * 3.f * (Field.IsValidIndex(Behind) ? Field[Behind] : 0.f)));
	}
	// Chokepoints first (their own count), then corridors: both kinds get working shutters each match.
	const int32 MinChoke = FMath::Clamp(Settings.MinChokepoints, 0, FMath::Max(0, Settings.MaxChokepoints));
	const int32 WantedChoke = Random.RandRange(MinChoke, FMath::Max(MinChoke, Settings.MaxChokepoints));
	int32 ChokeCandidates = 0, CorridorCandidates = 0, Chokepoints = 0, CorridorDoors = 0;
	for (const int32 E : Candidates) (IsChokepoint(E) ? ChokeCandidates : CorridorCandidates)++;
	const TArray<int32> DoorOrder = Ordered(Candidates, DoorWeight, Random);
	for (int32 Pass = 0; Pass < 2; ++Pass)
		for (const int32 E : DoorOrder)
		{
			const bool bChoke = IsChokepoint(E);
			if (bChoke != (Pass == 0)) continue;
			if (bChoke ? Chokepoints >= WantedChoke : CorridorDoors >= WantedDoors) continue;
			if (!MayClose(E)) continue;
			// Some chokepoints are broken shutters only: no way through there (the rules above keep another way).
			if (bChoke && Random.FRand() < Settings.ChokepointSealedChance)
			{
				const TArray<int32> Sealed = Cut(Blocked, E);
				if (StillFine())
				{
					Result.SealedEdges.Add(E); Close(E); ++Chokepoints; ++Result.SealedChokepoints;
					for (const int32 S : EdgeSlots[E]) Result.SlotStates[S] = EShutterSlotState::Sealed;
					continue;
				}
				Uncut(Blocked, Sealed);
			}
			const TArray<int32> Changed = Cut(Doors, E);
			if (!StillFine()) { Uncut(Doors, Changed); continue; }
			Result.DoorEdges.Add(E); Close(E);
			(bChoke ? Chokepoints : CorridorDoors)++;
			const TArray<int32>& Barrier = EdgeSlots[E];
			// One working shutter per row, or one per 9 m on a wide compartment row (a wide corridor has several doors).
			TArray<int32> Order = Barrier;
			for (int32 I = Order.Num() - 1; I > 0; --I) Order.Swap(I, Random.RandRange(0, I));
			const int32 Doorways = FMath::Clamp(FMath::RoundToInt(RowWidth(E) / 900.f), 1, Order.Num());
			for (const int32 S : Barrier) Result.SlotStates[S] = EShutterSlotState::Sealed;
			for (int32 I = 0; I < Doorways; ++I) Result.SlotStates[Order[I]] = EShutterSlotState::Door;
		}
	Result.ChokepointEdges = Chokepoints;

	// Only a shortfall the floor could have avoided is worth a note (a floor without chokepoints simply has none).
	const bool bShortSealed = Result.SealedEdges.Num() - Result.SealedChokepoints < FMath::Min(MinSealed, CorridorCandidates);
	const bool bShortDoors = CorridorDoors < FMath::Min(MinDoors, CorridorCandidates);
	const bool bShortChoke = Chokepoints < FMath::Min(MinChoke, ChokeCandidates);
	if (bShortSealed || bShortDoors || bShortChoke)
		Result.Note = FString::Printf(TEXT("fit %d/%d sealed, %d/%d working-shutter corridors, %d/%d chokepoints"),
			Result.SealedEdges.Num(), MinSealed, CorridorDoors, MinDoors, Chokepoints, MinChoke);
	return Result;
}
}

namespace ProjectShutterLayout
{
FShutterRuin RollRuin(const FShutterLayoutGraph& Graph, const FShutterLayoutResult& Layout, const FShutterRuinSettings& Settings, const int32 Seed)
{
	FShutterRuin Out;
	FRandomStream Random(Seed ^ 0x7A11);

	// Damage zones: where the floor took the worst of it this match. Mostly where mannequins are, never all in one place.
	const TArray<float> Field = ThreatField(Graph, 1500.f);
	const int32 MinZones = FMath::Max(0, Settings.MinZones);
	const int32 Zones = Random.RandRange(MinZones, FMath::Max(MinZones, Settings.MaxZones));
	{
		TArray<int32> Index; TArray<float> Weight;
		for (int32 N = 0; N < Graph.Nodes.Num(); ++N) { Index.Add(N); Weight.Add(.01f + 4.f * FMath::Square(Field.IsValidIndex(N) ? Field[N] : 0.f)); } // busy, mannequin-heavy places; rarely an empty car park
		for (const int32 N : Ordered(Index, Weight, Random))
		{
			if (Out.Zones.Num() >= Zones) break;
			bool bApart = true;
			for (const FVector2D& Z : Out.Zones) bApart &= FVector2D::Distance(Z, Graph.Nodes[N]) >= Settings.ZoneRadius * 1.6f;
			if (bApart) Out.Zones.Add(Graph.Nodes[N]);
		}
	}
	auto InZone = [&](const FVector2D& P) { for (const FVector2D& Z : Out.Zones) if (FVector2D::Distance(Z, P) <= Settings.ZoneRadius) return true; return false; };
	auto Broken = [&](const bool bIn)
	{
		FShutterLook L;
		if (Random.FRand() < (bIn ? Settings.JamChanceIn : Settings.JamChanceOut))
		{
			L.Look = EShutterLook::Jammed;
			L.Gap = Random.FRandRange(Settings.JamGapMin, FMath::Clamp(Settings.JamGapMax, Settings.JamGapMin, 120.f));
		}
		else L.Look = EShutterLook::Down;
		L.Tilt = Random.FRandRange(-1.f, 1.f) * (bIn ? Settings.TiltIn : Settings.TiltOut);
		return L;
	};
	auto Up = [&](const bool bIn)
	{
		FShutterLook L;
		if (Random.FRand() < (bIn ? Settings.HangChanceIn : Settings.HangChanceOut))
		{
			L.Look = EShutterLook::Hanging;
			L.Gap = Random.FRandRange(FMath::Max(210.f, Settings.HangGapMin), FMath::Max(Settings.HangGapMin, Settings.HangGapMax));
			L.Tilt = Random.FRandRange(-1.f, 1.f) * (bIn ? Settings.TiltIn : Settings.TiltOut) * 1.3f;
		}
		return L;
	};

	// Corridor and chokepoint rows (slots sharing a link form one row).
	Out.Slots.SetNum(Graph.Slots.Num());
	TMap<int32, TArray<int32>> Rows;
	for (int32 S = 0; S < Graph.Slots.Num(); ++S) Rows.FindOrAdd(Graph.Slots[S].Edge).Add(S);
	TArray<int32> Keys; Rows.GetKeys(Keys); Keys.Sort();
	for (const int32 E : Keys)
	{
		const TArray<int32>& Row = Rows[E];
		FVector2D Mid = FVector2D::ZeroVector; for (const int32 S : Row) Mid += Graph.Slots[S].Location; Mid /= Row.Num();
		const bool bIn = InZone(Mid);
		bool bClosed = false;
		for (const int32 S : Row) bClosed |= Layout.SlotStates.IsValidIndex(S) && Layout.SlotStates[S] != EShutterSlotState::Raised;
		if (bClosed)
		{
			for (const int32 S : Row)
			{
				if (Layout.SlotStates[S] == EShutterSlotState::Door) { Out.Slots[S].Look = EShutterLook::Working; continue; }
				Out.Slots[S] = Broken(bIn);
			}
			continue;
		}
		// Open this match. Sometimes smashed: some panels down, at least one wide panel left open (still a way through).
		TArray<int32> Wide; for (const int32 S : Row) if (Graph.Slots[S].Width >= Settings.MinOpenPanel) Wide.Add(S);
		float RowWidth = 0.f; for (const int32 S : Row) RowWidth += Graph.Slots[S].Width;
		const bool bMainCorridor = RowWidth > Settings.WideRowWidth;
		const float Partial = bMainCorridor ? (bIn ? Settings.WidePartialRowChanceIn : Settings.WidePartialRowChanceOut)
			: (bIn ? Settings.PartialRowChanceIn : Settings.PartialRowChanceOut);
		if (Row.Num() >= 2 && !Wide.IsEmpty() && Random.FRand() < Partial)
		{
			const int32 KeepOpen = Wide[Random.RandRange(0, Wide.Num() - 1)];
			bool bAnyDown = false;
			TArray<bool> Down; Down.Init(false, Row.Num());
			for (int32 I = 0; I < Row.Num(); ++I) if (Row[I] != KeepOpen && Random.FRand() < .65f) { Down[I] = true; bAnyDown = true; }
			if (!bAnyDown) for (int32 I = 0; I < Row.Num(); ++I) if (Row[I] != KeepOpen) { Down[I] = true; break; }
			for (int32 I = 0; I < Row.Num(); ++I) Out.Slots[Row[I]] = Down[I] ? Broken(bIn) : Up(bIn);
			continue;
		}
		for (const int32 S : Row) Out.Slots[S] = Up(bIn);
	}

	// Escalator gates: the working side, the rest broken.
	Out.EscalatorSides.SetNum(Graph.Escalators.Num());
	for (int32 E = 0; E < Graph.Escalators.Num(); ++E)
		for (int32 Side = 0; Side < Graph.Escalators[E].Sides.Num(); ++Side)
		{
			const int32 Node = Graph.Escalators[E].Sides[Side];
			FShutterLook L;
			if (Layout.EscalatorSide.IsValidIndex(E) && Layout.EscalatorSide[E] == Side) L.Look = EShutterLook::Working;
			else L = Broken(Graph.Nodes.IsValidIndex(Node) && InZone(Graph.Nodes[Node]));
			Out.EscalatorSides[E].Add(L);
		}
	return Out;
}
}
