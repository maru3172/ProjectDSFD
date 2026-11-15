#pragma once

#include "CoreMinimal.h"

/**
 * Per-match fire shutter layout (Docs/FireShutterLayout.md).
 *
 * A floor is a small route graph: nodes at corridor junctions, edges along corridors. Survivor spawns are start nodes.
 * Every floor hole is a working escalator, closed in by shutters: one side is a working (openable) shutter, the
 * other sides are broken. Each match:
 *  - every escalator gets one working side, chosen at random;
 *  - a few corridors are sealed by broken shutters (only where every start still reaches every escalator's working
 *    side and the exit, without a long detour);
 *  - a few corridors, mostly on the way to the escalators, are closed by shutters of which exactly one works (a door);
 *  - no junction gets more than one closed corridor, and closed corridors keep their distance.
 * Plain data, so the rules are checked without a world.
 */
struct PROJECTPROJECT01_API FShutterLayoutGraph
{
	struct FEdge { int32 A = INDEX_NONE; int32 B = INDEX_NONE; float Length = 0.f; };
	/** A corridor shutter lying on an edge. Several slots on one edge form one barrier across that corridor. */
	/**
	 * A corridor shutter lying on an edge. Several slots on one edge form one barrier across that corridor.
	 * Crosses: every other link the shutter physically stands across (wide corridors on a fine graph have parallel
	 * links); they close and open together with Edge.
	 */
	struct FSlot { int32 Edge = INDEX_NONE; FVector2D Location = FVector2D::ZeroVector; TArray<int32> Crosses; bool bChokepoint = false; float Width = 300.f; int32 Line = INDEX_NONE; };
	/** One escalator: the junction in front of each of its shutters (one entry per side shutter). */
	struct FEscalator { TArray<int32> Sides; };

	TArray<FVector2D> Nodes;
	TArray<FEdge> Edges;
	TArray<FSlot> Slots;
	TArray<FEscalator> Escalators;
	/** Survivor spawn nodes. Every one of them must reach every escalator and exit. */
	TArray<int32> Starts;
	/** Exit nodes (1st floor). */
	TArray<int32> Exits;
	/** Optional, per node: how many mannequins start around it. Drives the horror weighting (Docs/FireShutterLayout.md). */
	TArray<float> Threat;

	int32 AddNode(const FVector2D& Location) { return Nodes.Add(Location); }
	int32 AddEdge(int32 A, int32 B);
	int32 AddSlotOnEdge(int32 Edge, float Alpha = .5f);
	/** Nearest edge to a point within MaxDistance, or INDEX_NONE. */
	int32 FindNearestEdge(const FVector2D& Point, float MaxDistance) const;
};

struct PROJECTPROJECT01_API FShutterLayoutSettings
{
	/** Corridors sealed by broken shutters (rolled between the two each match). */
	int32 MinSealed = 1;
	int32 MaxSealed = 2;
	/** Corridors closed by a working shutter (rolled between the two each match). */
	int32 MinDoors = 1;
	int32 MaxDoors = 2;
	/** Chokepoints (narrow passages between areas) closed each match: always with one working shutter in the row. */
	int32 MinChokepoints = 1;
	int32 MaxChokepoints = 2;
	/**
	 * Rows wider than this (main mall corridors) never close: a department store corridor stays open, its shutters only
	 * show as smashed or hanging rows. Closing happens in narrow corridors and chokepoints.
	 */
	float MaxClosableRowWidth = 1000.f;
	/** Chance a whole fire compartment line (slots sharing Line) stays open this match: no shutter down along it. */
	float OpenLineChance = .35f;
	/** Chance a closed chokepoint is broken shutters only (no way through there); otherwise one working shutter in the row. */
	float ChokepointSealedChance = .4f;
	/** Extra distance counted for opening a working shutter on the way. */
	float DoorCost = 300.f;
	/** A target may get at most Ratio x the original distance + Slack away from a start. Keeps it from turning into a maze. */
	float MaxDetourRatio = 1.5f;
	float DetourSlack = 1000.f;
	/** Closed corridors keep at least this far apart (cm). */
	float MinSealSpacing = 1500.f;
	/** At most this many closed corridors (sealed or door) touch one junction. */
	int32 MaxSealsPerNode = 1;
	/** Never close a corridor right next to a spawn, an escalator's working side or the exit. */
	bool bProtectObjectiveEdges = true;
	/**
	 * Horror weighting of the random picks (0 = plain uniform). Fairness rules above always win; this only changes
	 * which allowed spot is tried first:
	 *  - working shutters: on the way, with mannequins likely waiting behind them;
	 *  - broken shutters: cutting long straight sightlines, and sending the way through junctions mannequins can
	 *    reach from several sides (surrounded, not stuck);
	 *  - escalator working side: the far side, so survivors walk around the opening first.
	 */
	float HorrorWeight = 1.f;
	/** Mannequins within this distance of a node count as waiting there. */
	float ThreatRadius = 1500.f;
};


enum class EShutterSlotState : uint8 { Raised, Sealed, Door };

struct PROJECTPROJECT01_API FShutterLayoutResult
{
	int32 Seed = 0;
	/** Per escalator: index into its Sides of the working shutter. */
	TArray<int32> EscalatorSide;
	/** Per corridor slot. */
	TArray<EShutterSlotState> SlotStates;
	TArray<int32> SealedEdges;
	TArray<int32> DoorEdges;
	/** Closed chokepoints this match (in SealedEdges when broken through, in DoorEdges when one shutter works). */
	int32 ChokepointEdges = 0;
	int32 SealedChokepoints = 0;
	/** Compartment lines left open this match. */
	TArray<int32> OpenLines;
	/** The nodes that must stay reachable (escalator working sides, then exits). */
	TArray<int32> Targets;
	/** Shortest start->target distances before and after, [Start * Targets.Num() + Target]; < 0 = unreachable. */
	TArray<float> BaseDistances;
	TArray<float> FinalDistances;
	/** Empty when the floor got everything it asked for; otherwise why it got less (never a broken floor). */
	FString Note;

	int32 CountSlots(EShutterSlotState State) const { int32 N = 0; for (const EShutterSlotState S : SlotStates) N += S == State ? 1 : 0; return N; }
};

namespace ProjectShutterLayout
{
	/** Shortest distance from Start to every node. Blocked edges are skipped, Door edges cost DoorCost more. < 0 = unreachable. */
	PROJECTPROJECT01_API void ShortestDistances(const FShutterLayoutGraph& Graph, const TBitArray<>& Blocked, const TBitArray<>& Doors, float DoorCost, int32 Start, TArray<float>& OutDistance);
	/** Start->target distances for every start/target pair ([Start * Targets.Num() + Target]). */
	PROJECTPROJECT01_API TArray<float> PairDistances(const FShutterLayoutGraph& Graph, const TArray<int32>& Targets, const TBitArray<>& Blocked, const TBitArray<>& Doors, float DoorCost);
	/** How many nodes any start reaches (broken shutters cut, working ones do not). No area may ever be cut off. */
	PROJECTPROJECT01_API int32 CountReachable(const FShutterLayoutGraph& Graph, const TBitArray<>& Blocked);
	/** True when every pair is reachable and within the detour limit of Base. */
	PROJECTPROJECT01_API bool WithinDetour(const TArray<float>& Base, const TArray<float>& Now, const FShutterLayoutSettings& Settings);
	/** Edges on some shortest start->target way (ties included). */
	PROJECTPROJECT01_API TBitArray<> RouteEdges(const FShutterLayoutGraph& Graph, const TArray<int32>& Targets, const TBitArray<>& Blocked);
	/** Per node, 0..1: how much mannequin presence is around it (Threat summed within Radius, then normalised). */
	PROJECTPROJECT01_API TArray<float> ThreatField(const FShutterLayoutGraph& Graph, float Radius);
	/** Junctions on the way (shortest start->target routes) that have 3+ open corridors and mannequins around: ambush spots. */
	PROJECTPROJECT01_API int32 AmbushJunctions(const FShutterLayoutGraph& Graph, const TArray<int32>& Targets, const TBitArray<>& Blocked, const TArray<float>& Field);
	PROJECTPROJECT01_API FShutterLayoutResult Generate(const FShutterLayoutGraph& Graph, const FShutterLayoutSettings& Settings, int32 Seed);
}

/** How a shutter looks this match (Docs/FireShutterLayout.md, ruined look). Never changes whether a way is open. */
enum class EShutterLook : uint8
{
	Tucked,  // up out of sight
	Down,    // broken, all the way down
	Jammed,  // broken, stuck with a gap at the floor too low for anyone
	Hanging, // broken, stuck high overhead: walk under it
	Working  // working shutter, down until someone raises it
};

struct PROJECTPROJECT01_API FShutterLook
{
	EShutterLook Look = EShutterLook::Tucked;
	float Gap = 0.f;   // cm between the floor and the shutter's bottom edge
	float Tilt = 0.f;  // degrees, in its own plane
};

struct PROJECTPROJECT01_API FShutterRuinSettings
{
	int32 MinZones = 1, MaxZones = 3;
	float ZoneRadius = 2500.f;
	/** Inside a damage zone / elsewhere. */
	float JamChanceIn = .6f, JamChanceOut = .3f;
	float HangChanceIn = .55f, HangChanceOut = .1f;
	/** An open row with some panels down and a gap left (looks smashed, still passable). */
	float PartialRowChanceIn = .7f, PartialRowChanceOut = .15f;
	/** Wide rows (main corridors) never close, so they show their shutters this way more often. */
	float WideRowWidth = 1000.f;
	float WidePartialRowChanceIn = .85f, WidePartialRowChanceOut = .45f;
	float TiltIn = 8.f, TiltOut = 3.f;
	float JamGapMin = 30.f, JamGapMax = 90.f;
	float HangGapMin = 220.f, HangGapMax = 250.f;
	/** A panel left open in a partial row must be at least this wide so people (and pathfinding) still get through. */
	float MinOpenPanel = 220.f;
};

struct PROJECTPROJECT01_API FShutterRuin
{
	TArray<FVector2D> Zones;
	TArray<FShutterLook> Slots;                // per Graph.Slots
	TArray<TArray<FShutterLook>> EscalatorSides; // per escalator, per side
};

namespace ProjectShutterLayout
{
	/** The ruined look for a rolled layout: damage zones, then each shutter's look. Pure, same seed same look. */
	PROJECTPROJECT01_API FShutterRuin RollRuin(const FShutterLayoutGraph& Graph, const FShutterLayoutResult& Layout, const FShutterRuinSettings& Settings, int32 Seed);
}
