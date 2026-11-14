#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ProjectShutterLayout.h"
#include "ProjectShutterLayoutRules.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
/** The real floor seen from above with a few rolled layouts (Saved/ShutterLayoutQA/<Map>_Layouts.svg). */
void WriteFloorPreview(const FShutterLayoutGraph& G, const TArray<FShutterLayoutResult>& Layouts, const TArray<FShutterRuin>& Ruins, const FString& File)
{
	FBox2D Bounds(ForceInit); for (const FVector2D& N : G.Nodes) Bounds += N;
	const float Scale = 600.f / FMath::Max(1.0, Bounds.GetSize().X), Pad = 20.f;
	const float CellW = Bounds.GetSize().X * Scale + Pad * 2, CellH = Bounds.GetSize().Y * Scale + Pad * 2 + 24;
	FString Svg = FString::Printf(TEXT("<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' font-family='sans-serif' font-size='12'><rect width='100%%' height='100%%' fill='#1b1b1b'/>"),
		int32(CellW * 2), int32(CellH * FMath::DivideAndRoundUp(Layouts.Num(), 2) + 30));
	Svg += FString::Printf(TEXT("<text x='10' y='%d' fill='#bbb'>blue: start | red dot: mannequin spawn | green: working | red: broken, all the way down | orange: broken, jammed with a gap | grey: hanging overhead | pink: damage zone | yellow: escalator</text>"),
		int32(CellH * FMath::DivideAndRoundUp(Layouts.Num(), 2) + 18));
	for (int32 L = 0; L < Layouts.Num(); ++L)
	{
		const FShutterLayoutResult& R = Layouts[L];
		const float OX = (L % 2) * CellW + Pad, OY = (L / 2) * CellH + Pad + 18;
		auto P = [&](const FVector2D& V) { return FVector2D(OX + (V.X - Bounds.Min.X) * Scale, OY + (V.Y - Bounds.Min.Y) * Scale); };
		Svg += FString::Printf(TEXT("<text x='%.0f' y='%.0f' fill='#ddd'>seed %d: broken %d, working %d</text>"), OX, OY - 6, R.Seed, R.SealedEdges.Num(), R.DoorEdges.Num());
		for (const FShutterLayoutGraph::FEdge& E : G.Edges)
		{
			const FVector2D A = P(G.Nodes[E.A]), B = P(G.Nodes[E.B]);
			Svg += FString::Printf(TEXT("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='#4a4a4a' stroke-width='3'/>"), A.X, A.Y, B.X, B.Y);
		}
		for (int32 N = 0; N < G.Nodes.Num(); ++N)
			if (G.Threat.IsValidIndex(N) && G.Threat[N] > 0.f)
			{
				const FVector2D V = P(G.Nodes[N]);
				Svg += FString::Printf(TEXT("<circle cx='%.1f' cy='%.1f' r='3.5' fill='#c43a3a'/>"), V.X, V.Y);
			}
		if (Ruins.IsValidIndex(L))
			for (const FVector2D& Z : Ruins[L].Zones)
			{
				const FVector2D C = P(Z);
				Svg += FString::Printf(TEXT("<circle cx='%.1f' cy='%.1f' r='%.1f' fill='#c0407a' fill-opacity='.22'/>"), C.X, C.Y, 2500.f * Scale);
			}
		for (int32 S = 0; S < G.Slots.Num(); ++S)
		{
			const EShutterLook Look = Ruins.IsValidIndex(L) && Ruins[L].Slots.IsValidIndex(S) ? Ruins[L].Slots[S].Look : EShutterLook::Tucked;
			if (Look == EShutterLook::Tucked) continue;
			const FShutterLayoutGraph::FEdge& E = G.Edges[G.Slots[S].Edge];
			const FVector2D A = P(G.Nodes[E.A]), B = P(G.Nodes[E.B]), M = P(G.Slots[S].Location);
			const FVector2D N = (B - A).GetSafeNormal().GetRotated(90.f) * 7.f;
			Svg += FString::Printf(TEXT("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='%s' stroke-width='4'/>"), M.X - N.X, M.Y - N.Y, M.X + N.X, M.Y + N.Y,
				Look == EShutterLook::Working ? TEXT("#4fbf6a") : Look == EShutterLook::Down ? TEXT("#e0503c") : Look == EShutterLook::Jammed ? TEXT("#f0a030") : TEXT("#9a9a9a"));
		}
		for (int32 Esc = 0; Esc < G.Escalators.Num(); ++Esc)
		{
			const TArray<int32>& Sides = G.Escalators[Esc].Sides;
			if (Sides.IsEmpty()) continue;
			FVector2D Mid = FVector2D::ZeroVector; for (const int32 Side : Sides) Mid += G.Nodes[Side]; Mid /= Sides.Num();
			const FVector2D C = P(Mid);
			if (R.EscalatorSide.IsValidIndex(Esc) && Sides.IsValidIndex(R.EscalatorSide[Esc]))
			{
				const FVector2D V = P(G.Nodes[Sides[R.EscalatorSide[Esc]]]);
				Svg += FString::Printf(TEXT("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='#4fbf6a' stroke-width='3'/>"), C.X, C.Y, V.X, V.Y);
			}
			Svg += FString::Printf(TEXT("<rect x='%.1f' y='%.1f' width='12' height='12' fill='#c9b25a'/>"), C.X - 6, C.Y - 6);
		}
		for (const int32 S : G.Starts)
		{
			const FVector2D V = P(G.Nodes[S]);
			Svg += FString::Printf(TEXT("<circle cx='%.1f' cy='%.1f' r='5' fill='#4a8fe0'/>"), V.X, V.Y);
		}
	}
	Svg += TEXT("</svg>");
	FFileHelper::SaveStringToFile(Svg, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectShutterLayoutMapChecks, "ProjectProject01.Editor.ShutterLayoutOnMaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectShutterLayoutMapChecks::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsPlaySessionInProgress()) return false;
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("ShutterLayoutQA");
	IFileManager::Get().MakeDirectory(*Dir, true);
	for (const TCHAR* Level : { TEXT("MultiplayTest"), TEXT("MultiplayTest1"), TEXT("MultiplayTest2") })
	{
		const FString Package = TEXT("/Game/MyProject/Level/") + FString(Level);
		if (!FPackageName::DoesPackageExist(Package)) continue;
		FAutomationEditorCommonUtils::LoadMap(Package);
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!TestTrue(Package + TEXT(" loaded"), World && World->GetOutermost()->GetName() == Package)) continue;
		AProjectShutterLayout* Layout = nullptr;
		for (TActorIterator<AProjectShutterLayout> It(World); It; ++It) Layout = *It;
		if (!Layout) { AddWarning(FString::Printf(TEXT("%s: no ProjectShutterLayout placed yet"), Level)); continue; }

		// Rolls the real floor with the rules only: the placed actors are not touched.
		const AProjectShutterLayout::FPlacement Placement = Layout->Collect();
		const FShutterLayoutGraph& G = Placement.Graph;
		TestTrue(FString::Printf(TEXT("%s: has route nodes, spawns and targets"), Level),
			G.Nodes.Num() > 0 && G.Starts.Num() > 0 && (G.Escalators.Num() > 0 || G.Exits.Num() > 0));
		int32 Short = 0, Mixed = 0; bool bReach = true, bDetour = true, bOneSide = true, bNoAreaCutOff = true;
		const int32 WholeFloor = ProjectShutterLayout::CountReachable(G, TBitArray<>(false, G.Edges.Num()));
		TArray<FShutterLayoutResult> Preview; TArray<FShutterRuin> PreviewRuins;
		for (int32 Seed = 1; Seed <= 200; ++Seed)
		{
			const FShutterLayoutResult R = ProjectShutterLayout::Generate(G, Placement.Settings, Seed);
			TBitArray<> Blocked(false, G.Edges.Num()), Doors(false, G.Edges.Num());
			// Recomputed from the slots themselves: every link a closed shutter stands across is closed.
			for (int32 Slot = 0; Slot < G.Slots.Num(); ++Slot)
			{
				if (R.SlotStates[Slot] == EShutterSlotState::Raised) continue;
				TBitArray<>& Bits = R.SealedEdges.Contains(G.Slots[Slot].Edge) ? Blocked : Doors;
				Bits[G.Slots[Slot].Edge] = true;
				for (const int32 X : G.Slots[Slot].Crosses) Bits[X] = true;
			}
			const TArray<float> Now = ProjectShutterLayout::PairDistances(G, R.Targets, Blocked, Doors, Placement.Settings.DoorCost);
			for (int32 I = 0; I < Now.Num(); ++I) bReach &= R.BaseDistances[I] < 0.f || Now[I] >= 0.f;
			bDetour &= ProjectShutterLayout::WithinDetour(R.BaseDistances, Now, Placement.Settings);
			bNoAreaCutOff &= ProjectShutterLayout::CountReachable(G, Blocked) == WholeFloor;
			for (int32 E = 0; E < G.Escalators.Num(); ++E) bOneSide &= G.Escalators[E].Sides.IsValidIndex(R.EscalatorSide[E]);
			Short += R.Note.IsEmpty() ? 0 : 1;
			Mixed += R.CountSlots(EShutterSlotState::Door) > 0 && R.CountSlots(EShutterSlotState::Sealed) > 0 ? 1 : 0;
			if (Seed <= 4) { Preview.Add(R); PreviewRuins.Add(ProjectShutterLayout::RollRuin(G, R, Layout->RuinSettings(), Seed)); }
		}
		int32 UnreachableBase = 0;
		for (const float D : ProjectShutterLayout::Generate(G, Placement.Settings, 1).BaseDistances) UnreachableBase += D < 0.f ? 1 : 0;
		TestEqual(FString::Printf(TEXT("%s: every escalator/exit is reachable from every spawn on the bare floor"), Level), UnreachableBase, 0);
		TestTrue(FString::Printf(TEXT("%s: shutters never cut anyone off"), Level), bReach);
		TestTrue(FString::Printf(TEXT("%s: no long detours"), Level), bDetour);
		TestTrue(FString::Printf(TEXT("%s: no part of the floor is ever cut off"), Level), bNoAreaCutOff);
		TestTrue(FString::Printf(TEXT("%s: every escalator gets a working side"), Level), bOneSide);
		AddInfo(FString::Printf(TEXT("%s: nodes %d links %d escalators %d corridor slots %d starts %d | 200 rolls: %d short, %d mixed"),
			Level, G.Nodes.Num(), G.Edges.Num(), G.Escalators.Num(), G.Slots.Num(), G.Starts.Num(), Short, Mixed));
		// Horror weighting on the real floor: the same 200 seeds with and without it.
		{
			const TArray<float> Field = ProjectShutterLayout::ThreatField(G, Placement.Settings.ThreatRadius);
			TArray<float> FromSpawns; FromSpawns.Init(-1.f, G.Nodes.Num());
			{
				const TBitArray<> None(false, G.Edges.Num()); TArray<float> D;
				for (const int32 Start : G.Starts)
				{
					ProjectShutterLayout::ShortestDistances(G, None, None, 0.f, Start, D);
					for (int32 N = 0; N < D.Num(); ++N) if (D[N] >= 0.f && (FromSpawns[N] < 0.f || D[N] < FromSpawns[N])) FromSpawns[N] = D[N];
				}
			}
			auto Measure = [&](float Horror, double& Behind, double& Ambush, double& Far)
			{
				FShutterLayoutSettings S = Placement.Settings; S.HorrorWeight = Horror;
				int32 Doors = 0, Sides = 0; Behind = Ambush = Far = 0;
				for (int32 Seed = 1; Seed <= 200; ++Seed)
				{
					const FShutterLayoutResult R = ProjectShutterLayout::Generate(G, S, Seed);
					for (const int32 E : R.DoorEdges)
					{
						const int32 Back = FromSpawns[G.Edges[E].A] > FromSpawns[G.Edges[E].B] ? G.Edges[E].A : G.Edges[E].B;
						Behind += Field[Back]; ++Doors;
					}
					TBitArray<> Blocked(false, G.Edges.Num());
					for (const int32 E : R.SealedEdges) Blocked[E] = true;
					Ambush += ProjectShutterLayout::AmbushJunctions(G, R.Targets, Blocked, Field);
					for (int32 E = 0; E < G.Escalators.Num(); ++E)
					{
						const TArray<int32>& Sides_ = G.Escalators[E].Sides;
						if (!Sides_.IsValidIndex(R.EscalatorSide[E])) continue;
						float Mean = 0; for (const int32 Side : Sides_) Mean += FMath::Max(0.f, FromSpawns[Side]); Mean /= Sides_.Num();
						Far += FromSpawns[Sides_[R.EscalatorSide[E]]] > Mean ? 1 : 0; ++Sides;
					}
				}
				Behind /= FMath::Max(1, Doors); Ambush /= 200; Far = Sides ? 100 * Far / Sides : 0;
			};
			double B0, A0, F0, B1, A1, F1;
			Measure(0.f, B0, A0, F0); Measure(Placement.Settings.HorrorWeight, B1, A1, F1);
			AddInfo(FString::Printf(TEXT("%s horror weighting off -> on: mannequins behind working shutters %.2f -> %.2f, ambush junctions on the way %.2f -> %.2f, far escalator side %.0f%% -> %.0f%%"),
				Level, B0, B1, A0, A1, F0, F1));
		}
		WriteFloorPreview(G, Preview, PreviewRuins, Dir / (FString(Level) + TEXT("_Layouts.svg")));
	}
	return true;
}
#endif
