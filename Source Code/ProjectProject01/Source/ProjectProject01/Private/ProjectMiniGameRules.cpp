#include "ProjectMiniGameRules.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace ProjectMiniGame
{
// ---------------------------------------------------------------- 한 칸씩
bool FStepStage::Parse(const TSharedPtr<FJsonObject>& Json,FStepStage& Out)
{
    if(!Json)return false;
    Out=FStepStage();FString Face;double Shutters=0,Par=0;
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Json->TryGetStringField(TEXT("id"),Out.Id)||!Json->TryGetArrayField(TEXT("map"),Rows)||!Json->TryGetStringField(TEXT("facing"),Face)||Face.Len()!=1)return false;
    Json->TryGetStringField(TEXT("name"),Out.Name);Json->TryGetNumberField(TEXT("shutters"),Shutters);Json->TryGetNumberField(TEXT("par"),Par);
    for(const auto& Row:*Rows)Out.Map.Add(Row->AsString());
    Out.Facing=Face[0];Out.Shutters=int32(Shutters);Out.Par=int32(Par);
    return Out.Map.Num()>2;
}

FStepState::FStepState(const FStepStage& Stage):Grid(Stage.Map),Facing(Stage.Facing),ShuttersLeft(Stage.Shutters)
{
    for(int32 Y=0;Y<Grid.Num();++Y)for(int32 X=0;X<Grid[Y].Len();++X)
    {
        if(Grid[Y][X]==TEXT('S')){Player=FIntPoint(X,Y);Grid[Y][X]=TEXT('.');}
        else if(Grid[Y][X]==TEXT('M')){Mannequins.Add(FIntPoint(X,Y));Grid[Y][X]=TEXT('.');}
    }
}
FIntPoint FStepState::Delta(TCHAR D){return D==TEXT('U')?FIntPoint(0,-1):D==TEXT('D')?FIntPoint(0,1):D==TEXT('L')?FIntPoint(-1,0):FIntPoint(1,0);}
TCHAR FStepState::At(int32 X,int32 Y)const{return Y<0||Y>=Grid.Num()||X<0||X>=Grid[Y].Len()?TEXT('#'):Grid[Y][X];}
int32 FStepState::Watched()const
{
    const FIntPoint D=Delta(Facing);
    for(int32 X=Player.X+D.X,Y=Player.Y+D.Y;!Blocked(X,Y);X+=D.X,Y+=D.Y)if(const int32 M=MannequinAt(X,Y);M!=INDEX_NONE)return M;
    return INDEX_NONE;
}
TArray<FIntPoint> FStepState::SightLine()const
{
    TArray<FIntPoint> Cells;const FIntPoint D=Delta(Facing);
    for(int32 X=Player.X+D.X,Y=Player.Y+D.Y;!Blocked(X,Y);X+=D.X,Y+=D.Y){Cells.Add(FIntPoint(X,Y));if(MannequinAt(X,Y)!=INDEX_NONE)break;}
    return Cells;
}
static bool IsDirection(TCHAR C){return C==TEXT('U')||C==TEXT('D')||C==TEXT('L')||C==TEXT('R');}
EStepResult FStepState::Apply(const FString& Action)
{
    if(Action.Len()==1&&IsDirection(Action[0]))
    {
        const FIntPoint D=Delta(Action[0]);const int32 X=Player.X+D.X,Y=Player.Y+D.Y;
        if(Blocked(X,Y)||MannequinAt(X,Y)!=INDEX_NONE)return EStepResult::Invalid;
        Player=FIntPoint(X,Y);Facing=Action[0];++Actions;
        if(At(X,Y)==TEXT('E'))return EStepResult::Won;
    }
    else if(Action.Len()==2&&Action[0]==TEXT('t')&&IsDirection(Action[1]))
    {
        if(Facing==Action[1])return EStepResult::Invalid;
        Facing=Action[1];return EStepResult::Continue; // free: nothing moves
    }
    else if(Action==TEXT("S"))
    {
        const FIntPoint D=Delta(Facing);const int32 X=Player.X+D.X,Y=Player.Y+D.Y;
        if(ShuttersLeft<=0||At(X,Y)!=TEXT('X')||MannequinAt(X,Y)!=INDEX_NONE)return EStepResult::Invalid;
        Grid[Y][X]=TEXT('C');--ShuttersLeft;++Actions;
    }
    else return EStepResult::Invalid;
    return MoveMannequins();
}
EStepResult FStepState::MoveMannequins()
{
    const int32 Seen=Watched();
    for(int32 I=0;I<Mannequins.Num();++I)
    {
        if(I==Seen)continue;
        const FIntPoint M=Mannequins[I];const int32 DX=Player.X-M.X,DY=Player.Y-M.Y;
        const FIntPoint H(FMath::Sign(DX),0),V(0,FMath::Sign(DY));
        TArray<FIntPoint,TInlineAllocator<2>> Tries;
        if(FMath::Abs(DX)>=FMath::Abs(DY)){if(DX!=0)Tries.Add(H);if(DY!=0)Tries.Add(V);}
        else{if(DY!=0)Tries.Add(V);if(DX!=0)Tries.Add(H);}
        for(const FIntPoint& S:Tries)
        {
            const FIntPoint N=M+S;
            if(Blocked(N.X,N.Y)||MannequinAt(N.X,N.Y)!=INDEX_NONE)continue;
            Mannequins[I]=N;
            if(N==Player)return EStepResult::Caught;
            break;
        }
    }
    return EStepResult::Continue;
}
EStepResult Replay(const FStepStage& Stage,const TArray<FString>& Moves,FStepState& Out)
{
    Out=FStepState(Stage);
    for(const FString& Move:Moves)if(const EStepResult R=Out.Apply(Move);R!=EStepResult::Continue)return R;
    return EStepResult::Continue;
}

// ---------------------------------------------------------------- 움직였다
static bool ReadFigures(const TSharedPtr<FJsonObject>& Json,const TCHAR* Field,TArray<FSpotFigure>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* List=nullptr;if(!Json->TryGetArrayField(Field,List))return false;
    for(const auto& V:*List)
    {
        const TSharedPtr<FJsonObject>* F=nullptr;if(!V->TryGetObject(F))return false;
        FSpotFigure Figure;double X=0,Y=0,H=0,A=0;
        // The server writes the record's members in PascalCase.
        if(!(*F)->TryGetNumberField(TEXT("X"),X)&&!(*F)->TryGetNumberField(TEXT("x"),X))return false;
        if(!(*F)->TryGetNumberField(TEXT("Y"),Y))(*F)->TryGetNumberField(TEXT("y"),Y);
        if(!(*F)->TryGetNumberField(TEXT("Head"),H))(*F)->TryGetNumberField(TEXT("head"),H);
        if(!(*F)->TryGetNumberField(TEXT("Arm"),A))(*F)->TryGetNumberField(TEXT("arm"),A);
        Figure.X=int32(X);Figure.Y=int32(Y);Figure.Head=int32(H);Figure.Arm=int32(A);Out.Add(Figure);
    }
    return true;
}
bool FSpotRound::Parse(const TSharedPtr<FJsonObject>& Json,FSpotRound& Out)
{
    if(!Json)return false;
    Out=FSpotRound();double Round=1,Cleared=0,Lives=3,Score=0,Changes=1;
    Json->TryGetNumberField(TEXT("round"),Round);Json->TryGetNumberField(TEXT("cleared"),Cleared);Json->TryGetNumberField(TEXT("lives"),Lives);
    Json->TryGetNumberField(TEXT("score"),Score);Json->TryGetNumberField(TEXT("changes"),Changes);
    Out.Round=int32(Round);Out.Cleared=int32(Cleared);Out.Lives=int32(Lives);Out.Score=int32(Score);Out.Changes=int32(Changes);
    return ReadFigures(Json,TEXT("before"),Out.Before)&&ReadFigures(Json,TEXT("after"),Out.After)&&Out.Before.Num()==Out.After.Num()&&Out.Before.Num()>0;
}

// ---------------------------------------------------------------- 정전 복구
const FVector2D FBlackoutSim::Board(320,22);
double FBlackoutSim::Random01(){RandomState=RandomState*1664525u+1013904223u;return double(RandomState>>8)/double(1u<<24);}
FVector2D FBlackoutSim::SpawnPoint()
{
    // The room's four doorways: both sides and both bottom corners.
    static const FVector2D Doors[]={FVector2D(10,Height*.5),FVector2D(Width-10,Height*.5),FVector2D(28,Height-14),FVector2D(Width-28,Height-14)};
    int32 Best=0;double Far=-1;
    for(int32 Try=0;Try<3;++Try){const int32 I=int32(Random01()*4)%4;const double D=FVector2D::Distance(Doors[I],Player);if(D>Far){Far=D;Best=I;}}
    return Doors[Best];
}
void FBlackoutSim::Start(uint32 Seed)
{
    *this=FBlackoutSim();RandomState=Seed?Seed:1;
    for(int32 I=0;I<FuseCount;++I)
    {
        FVector2D P;
        for(int32 Try=0;Try<50;++Try)
        {
            P=FVector2D(30+Random01()*(Width-60),70+Random01()*(Height-150));
            if(FVector2D::Distance(P,Player)>120&&FVector2D::Distance(P,Board)>70&&Fuses.FindByPredicate([&](const FVector2D& O){return FVector2D::Distance(O,P)<70;})==nullptr)break;
        }
        Fuses.Add(P);
    }
    for(int32 I=0;I<2;++I)Mannequins.Add({SpawnPoint(),0});
}
FVector2D FBlackoutSim::TorchDirection()const
{
    const FVector2D Target=bInstalling?Board:Aim;const FVector2D D=Target-Player;
    return D.IsNearlyZero()?FVector2D(0,-1):D.GetSafeNormal();
}
bool FBlackoutSim::Lit(const FVector2D& Point)const
{
    if(!bLight||Battery<=0)return false;
    const FVector2D D=Point-Player;const double Distance=D.Size();
    if(Distance>TorchRange)return false;if(Distance<12)return true;
    return FVector2D::DotProduct(D/Distance,TorchDirection())>=FMath::Cos(FMath::DegreesToRadians(TorchHalfAngle));
}
FBlackoutSim::EOutcome FBlackoutSim::Tick(double Seconds,const FBlackoutInput& In)
{
    if(Outcome!=EOutcome::Playing||Seconds<=0)return Outcome;
    Seconds=FMath::Min(Seconds,.1);Elapsed+=Seconds;Aim=In.Aim;
    if(In.bToggleLight&&Battery>0)bLight=!bLight;
    // Installing: stand at the board, hold E; the torch is pinned to the board meanwhile.
    bInstalling=In.bHoldInstall&&Carried>0&&FVector2D::Distance(Player,Board)<=BoardReach;
    if(bInstalling)
    {
        InstallProgress+=Seconds;
        if(InstallProgress>=InstallSeconds){InstallProgress=0;--Carried;++Installed;if(Installed>=FuseCount)return Outcome=EOutcome::Cleared;}
    }
    else
    {
        InstallProgress=0;
        FVector2D Move=In.Move;if(Move.SizeSquared()>1)Move.Normalize();
        Player+=Move*PlayerSpeed*Seconds;
        Player.X=FMath::Clamp(Player.X,12.,Width-12);Player.Y=FMath::Clamp(Player.Y,30.,Height-12);
    }
    for(int32 I=Fuses.Num()-1;I>=0;--I)if(FVector2D::Distance(Fuses[I],Player)<=PickDistance){Fuses.RemoveAt(I);++Carried;}
    if(bLight&&Battery>0){Battery=FMath::Max(0.,Battery-DrainPerSecond*Seconds);if(Battery<=0)bLight=false;}
    for(auto& M:Mannequins)
    {
        if(Lit(M.Pos)){M.QuietFor+=Seconds;continue;}
        const FVector2D To=Player-M.Pos;const double D=To.Size();
        M.Pos+=To/FMath::Max(D,1.)*FMath::Min(D,MannequinSpeed*Seconds);
        if(M.QuietFor>=.55||Prints.IsEmpty()||FVector2D::Distance(Prints.Last().Pos,M.Pos)>22){Prints.Add({M.Pos,0});M.QuietFor=0;}else M.QuietFor+=Seconds;
        if(FVector2D::Distance(M.Pos,Player)<=CatchDistance)return Outcome=EOutcome::Caught;
    }
    for(auto& P:Prints)P.Age+=Seconds;
    Prints.RemoveAll([](const FBlackoutPrint& P){return P.Age>2.5;});
    NextSpawn-=Seconds;
    if(NextSpawn<=0){NextSpawn=SpawnEvery;if(Mannequins.Num()<MaxMannequins)Mannequins.Add({SpawnPoint(),0});}
    return Outcome;
}
static TArray<TSharedPtr<FJsonValue>> Points(const TArray<FVector2D>& List)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for(const auto& P:List){TArray<TSharedPtr<FJsonValue>> XY{MakeShared<FJsonValueNumber>(FMath::RoundToInt(P.X*10)/10.),MakeShared<FJsonValueNumber>(FMath::RoundToInt(P.Y*10)/10.)};Out.Add(MakeShared<FJsonValueArray>(XY));}
    return Out;
}
static bool ReadPoints(const TSharedPtr<FJsonObject>& Json,const TCHAR* Field,TArray<FVector2D>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* List=nullptr;if(!Json->TryGetArrayField(Field,List))return false;
    for(const auto& V:*List){const auto& XY=V->AsArray();if(XY.Num()!=2)return false;Out.Add(FVector2D(XY[0]->AsNumber(),XY[1]->AsNumber()));}
    return true;
}
TSharedRef<FJsonObject> FBlackoutSim::Save()const
{
    auto J=MakeShared<FJsonObject>();
    J->SetArrayField(TEXT("player"),Points({Player}));J->SetArrayField(TEXT("fuses"),Points(Fuses));
    TArray<FVector2D> M;for(const auto& Mq:Mannequins)M.Add(Mq.Pos);J->SetArrayField(TEXT("mannequins"),Points(M));
    J->SetNumberField(TEXT("battery"),Battery);J->SetNumberField(TEXT("elapsed"),Elapsed);J->SetNumberField(TEXT("nextSpawn"),NextSpawn);
    J->SetNumberField(TEXT("carried"),Carried);J->SetNumberField(TEXT("installed"),Installed);J->SetBoolField(TEXT("light"),bLight);
    J->SetNumberField(TEXT("random"),double(RandomState));
    return J;
}
bool FBlackoutSim::Load(const TSharedPtr<FJsonObject>& J)
{
    if(!J)return false;
    FBlackoutSim S;TArray<FVector2D> P,M;double Random=1,Carried_=0,Installed_=0;
    if(!ReadPoints(J,TEXT("player"),P)||P.Num()!=1||!ReadPoints(J,TEXT("fuses"),S.Fuses)||!ReadPoints(J,TEXT("mannequins"),M))return false;
    if(!J->TryGetNumberField(TEXT("battery"),S.Battery)||!J->TryGetNumberField(TEXT("elapsed"),S.Elapsed))return false;
    J->TryGetNumberField(TEXT("nextSpawn"),S.NextSpawn);J->TryGetNumberField(TEXT("carried"),Carried_);J->TryGetNumberField(TEXT("installed"),Installed_);
    J->TryGetBoolField(TEXT("light"),S.bLight);J->TryGetNumberField(TEXT("random"),Random);
    S.Player=P[0];S.Carried=int32(Carried_);S.Installed=int32(Installed_);S.RandomState=uint32(Random);
    for(const auto& Pos:M)S.Mannequins.Add({Pos,0});
    S.Battery=FMath::Clamp(S.Battery,0.,100.);
    *this=S;return true;
}
}
