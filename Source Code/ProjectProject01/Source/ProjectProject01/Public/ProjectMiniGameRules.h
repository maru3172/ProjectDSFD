#pragma once
#include "CoreMinimal.h"

class FJsonObject;

/**
 * Rules of the phone mini games (Docs/MiniGames.md). Pure logic, no Slate, tested directly.
 * 한 칸씩 must match the server's replay (Tools/ProjectProject01Backend/Game/MiniGameRules.cs) move for move.
 */
namespace ProjectMiniGame
{
    // ---- 한 칸씩 (steps) ----
    struct PROJECTPROJECT01_API FStepStage
    {
        FString Id,Name;
        TArray<FString> Map;
        TCHAR Facing=TEXT('U');
        int32 Shutters=0,Par=0;
        static bool Parse(const TSharedPtr<FJsonObject>& Json,FStepStage& Out);
        int32 Stars(int32 Actions)const{return Actions<=Par?3:Actions<=Par+3?2:1;}
    };

    enum class EStepResult:uint8{Continue,Won,Caught,Invalid};

    /**
     * One position of a stage. '#' wall, '.' floor, 'E' exit, 'X' a doorway whose fire shutter can be lowered ('C' once
     * lowered). After every step or shutter the mannequins move one cell toward the player, in reading order, except the
     * first one in a straight line ahead of the player. Turning on the spot is free: nothing moves.
     */
    struct PROJECTPROJECT01_API FStepState
    {
        TArray<FString> Grid;
        FIntPoint Player=FIntPoint::ZeroValue;
        TCHAR Facing=TEXT('U');
        int32 ShuttersLeft=0,Actions=0;
        TArray<FIntPoint> Mannequins;
        explicit FStepState(const FStepStage& Stage);
        FStepState()=default;
        /** "U/D/L/R" step (and face that way), "tU/tD/tL/tR" turn on the spot, "S" lower the shutter just ahead. */
        EStepResult Apply(const FString& Action);
        /** Index of the mannequin the player is looking at (it does not move), or INDEX_NONE. */
        int32 Watched()const;
        /** The cells the player sees, nearest first, up to a wall, a lowered shutter or the first mannequin. */
        TArray<FIntPoint> SightLine()const;
        TCHAR At(int32 X,int32 Y)const;
        bool Blocked(int32 X,int32 Y)const{const TCHAR C=At(X,Y);return C==TEXT('#')||C==TEXT('C');}
        int32 MannequinAt(int32 X,int32 Y)const{return Mannequins.IndexOfByKey(FIntPoint(X,Y));}
        static FIntPoint Delta(TCHAR Direction);
    private:
        EStepResult MoveMannequins();
    };

    /** Plays a move list from the start; stops at the first result that is not Continue. */
    PROJECTPROJECT01_API EStepResult Replay(const FStepStage& Stage,const TArray<FString>& Moves,FStepState& OutState);

    // ---- 움직였다 (spot): the server makes the pictures; the client only shows them. ----
    struct FSpotFigure{int32 X=0,Y=0,Head=0,Arm=0;bool operator==(const FSpotFigure& O)const{return X==O.X&&Y==O.Y&&Head==O.Head&&Arm==O.Arm;}};
    struct PROJECTPROJECT01_API FSpotRound
    {
        int32 Round=1,Cleared=0,Lives=3,Score=0,Changes=1;
        TArray<FSpotFigure> Before,After;
        static bool Parse(const TSharedPtr<FJsonObject>& Json,FSpotRound& Out);
        /** Seconds to look at the picture before the lights go out: longer with more mannequins. */
        float MemorizeSeconds()const{return 3.f+.25f*Before.Num();}
    };

    // ---- 정전 복구 (blackout): real time, on the client only. ----
    struct FBlackoutInput
    {
        FVector2D Move=FVector2D::ZeroVector; // -1..1 each axis
        FVector2D Aim=FVector2D(320,0);       // room point the torch points at
        bool bHoldInstall=false;
        bool bToggleLight=false;              // one press
    };
    struct FBlackoutMannequin{FVector2D Pos;double QuietFor=0;};
    struct FBlackoutPrint{FVector2D Pos;double Age=0;};
    /**
     * A dark electrical room seen from above. Mannequins walk toward the player only while the torch is not on them; the
     * torch drains its battery while on. Fetch three fuses and hold E at the fuse board for each one; while installing,
     * the torch points at the board. Moving mannequins leave footprints that fade (the tell; no flashing).
     */
    struct FBlackoutSim
    {
        static constexpr double Width=640,Height=360,PlayerSpeed=95,MannequinSpeed=36,TorchRange=230,TorchHalfAngle=27.5,
            DrainPerSecond=2.4,InstallSeconds=1.6,CatchDistance=16,PickDistance=18,BoardReach=44,SpawnEvery=14;
        static constexpr int32 FuseCount=3,MaxMannequins=5;
        static const FVector2D Board;
        FVector2D Player=FVector2D(320,320),Aim=FVector2D(320,0);
        double Battery=100,Elapsed=0,InstallProgress=0,NextSpawn=SpawnEvery;
        bool bLight=true,bInstalling=false;
        int32 Carried=0,Installed=0;
        uint32 RandomState=1;
        TArray<FVector2D> Fuses;
        TArray<FBlackoutMannequin> Mannequins;
        TArray<FBlackoutPrint> Prints;
        enum class EOutcome:uint8{Playing,Cleared,Caught};
        EOutcome Outcome=EOutcome::Playing;

        void Start(uint32 Seed);
        EOutcome Tick(double Seconds,const FBlackoutInput& Input);
        /** True when the torch is on and this point is inside its cone. */
        bool Lit(const FVector2D& Point)const;
        FVector2D TorchDirection()const;
        TSharedRef<FJsonObject> Save()const;
        bool Load(const TSharedPtr<FJsonObject>& Json);
    private:
        double Random01();
        FVector2D SpawnPoint();
    };
}
