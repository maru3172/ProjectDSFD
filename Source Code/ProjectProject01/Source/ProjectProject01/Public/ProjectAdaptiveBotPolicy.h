#pragma once

// Portable, bounded decision model. No world access, neural network, rewards or training runtime.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ProjectBots
{
inline float Clamp(float V,float Lo,float Hi){return std::max(Lo,std::min(Hi,V));}
inline bool Finite(double V){return std::isfinite(V);}
struct Vec
{
    float X=0,Y=0,Z=0;
    Vec operator+(Vec B)const{return {X+B.X,Y+B.Y,Z+B.Z};}
    Vec operator-(Vec B)const{return {X-B.X,Y-B.Y,Z-B.Z};}
    Vec operator*(float S)const{return {X*S,Y*S,Z*S};}
    float Length()const{return std::sqrt(X*X+Y*Y+Z*Z);}
    Vec Unit()const{float D=Length();return D>.001f?*this*(1.f/D):Vec{};}
    bool Valid()const{return Finite(X)&&Finite(Y)&&Finite(Z);}
};
inline float Dot(Vec A,Vec B){return A.X*B.X+A.Y*B.Y+A.Z*B.Z;}

struct Difficulty
{
    int Level=2;
    float Skill=0,Reaction=0,Memory=0,Sharing=0,Adaptation=0,Coordination=0,Opening=0,TurnRate=0;
    bool AllowsDeception() const { return Level>=4; }
    static Difficulty For(int Level)
    {
        Difficulty D;D.Level=std::max(1,std::min(5,Level));
        const float T=(D.Level-1)/4.f;D.Skill=T*T;
        D.Reaction=1.65f-1.25f*D.Skill;
        D.Memory=2.5f+5.5f*D.Skill;
        D.Sharing=.15f+.75f*D.Skill;
        D.Adaptation=.04f+.48f*D.Skill;
        D.Coordination=.12f+.78f*D.Skill;
        D.Opening=D.Level==1?.24f:D.Level==2?.12f:0.f;
        D.TurnRate=85.f+125.f*D.Skill;
        return D;
    }
    static const char* Name(int Level)
    {
        switch(Level){case 1:return "쉬움";case 2:return "보통";case 3:return "어려움";case 4:return "전문가";case 5:return "마스터";default:return "보통";}
    }
};

struct Pattern
{
    struct Bucket { int64_t Epoch=-1;float Seen=0,Still=0,Sprint=0,Watching=0,Left=0,Right=0; };
    std::array<Bucket,12> Bins{}; // Twelve 5-second bins: constant storage, recent 60 seconds only.
    double LastObservation=-1;
    uint32_t Count=0;
    bool Observe(double Now,float Seconds,bool Still,bool Sprint,bool Watching,float Turn)
    {
        if(!Finite(Now)||Now<0||Now>1.e12||!Finite(Seconds)||Seconds<=0||!Finite(Turn))return false;
        // One shared sample per opponent/time slice: ten bodies cannot inflate confidence tenfold.
        if(LastObservation>=0 && Now-LastObservation<.19)return false;
        const double Gap=LastObservation>=0?Now-LastObservation:Seconds;
        const float Duration=Clamp(static_cast<float>(Gap<=.5?Gap:Seconds),0,.25f);
        const auto Epoch=static_cast<int64_t>(std::floor(Now/5.));
        auto& B=Bins[static_cast<size_t>(Epoch%12)];
        if(B.Epoch!=Epoch)B=Bucket{Epoch};
        B.Seen+=Duration;B.Still+=Still?Duration:0;B.Sprint+=Sprint?Duration:0;B.Watching+=Watching?Duration:0;
        B.Left+=Turn<-.1f?Duration:0;B.Right+=Turn>.1f?Duration:0;
        LastObservation=Now;if(Count<std::numeric_limits<uint32_t>::max())++Count;return true;
    }
    struct Summary { float Confidence=0,Defensive=0,Sprinting=0,Watching=0,LeftBias=0,ObservedSeconds=0; };
    Summary Read(double Now)const
    {
        Summary S;if(!Finite(Now)||Now<0||Now>1.e12)return S;
        float Still=0,Sprint=0,Watching=0,Left=0,Right=0;
        const auto Epoch=static_cast<int64_t>(std::floor(Now/5.));
        for(const auto& B:Bins)if(B.Epoch>=0&&B.Epoch<=Epoch&&Epoch-B.Epoch<12)
        {
            // Recent bins matter more. Blind time never contributes stationary/camping evidence.
            const float Weight=std::exp(-static_cast<float>(Epoch-B.Epoch)*5.f/30.f);
            S.ObservedSeconds+=B.Seen*Weight;Still+=B.Still*Weight;Sprint+=B.Sprint*Weight;
            Watching+=B.Watching*Weight;Left+=B.Left*Weight;Right+=B.Right*Weight;
        }
        if(S.ObservedSeconds<.001f)return S;
        S.Confidence=Clamp((S.ObservedSeconds-2.f)/14.f,0,1);
        S.Defensive=Clamp(Still/S.ObservedSeconds,0,1);
        S.Sprinting=Clamp(Sprint/S.ObservedSeconds,0,1);
        S.Watching=Clamp(Watching/S.ObservedSeconds,0,1);
        S.LeftBias=Clamp((Left-Right)/S.ObservedSeconds,-1,1);
        return S;
    }
};

struct SharedSenses
{
    struct Report { uint32_t Team=0,Source=0,Target=0;Vec Position{},Velocity{};double At=-1; };
    struct Entry { Report Seen{};Pattern Statistics{}; };
    std::array<Entry,16> Entries{};
    // Only perception adapters that have passed range, cone and LOS checks may publish.
    bool Publish(const Report& R,bool ActuallyVisible,float Seconds,bool Still,bool Sprint,bool Watching,float Turn)
    {
        if(!ActuallyVisible||!R.Source||!R.Target||!Finite(R.At)||R.At<0||R.At>1.e12||!R.Position.Valid()||!R.Velocity.Valid())return false;
        Entry* Slot=nullptr;
        for(auto& E:Entries)if(E.Seen.Team==R.Team&&E.Seen.Target==R.Target){Slot=&E;break;}
        if(!Slot)for(auto& E:Entries)if(!E.Seen.Target){Slot=&E;break;}
        if(!Slot){Slot=&*std::min_element(Entries.begin(),Entries.end(),[](const Entry& A,const Entry& B){return A.Seen.At<B.Seen.At;});*Slot=Entry{};}
        if(R.At<Slot->Seen.At)return false;
        Slot->Statistics.Observe(R.At,Seconds,Still,Sprint,Watching,Turn);Slot->Seen=R;return true;
    }
    const Entry* Find(uint32_t Team,uint32_t Target,double Now,float Lifetime)const
    {
        if(!Finite(Now)||!Finite(Lifetime)||Lifetime<=0)return nullptr;
        for(const auto& E:Entries)if(E.Seen.Target==Target&&E.Seen.Team==Team&&E.Seen.At<=Now&&Now-E.Seen.At<=std::min(Lifetime,8.f))return &E;
        return nullptr;
    }
};

enum class Action:uint8_t { Search,Pressure,FlankLeft,FlankRight,Lure,Cover,Regroup,Recover,Observe,Hold,Count };
enum class Root:uint8_t { Suspended,Active,Finished };
enum class State:uint8_t { Waiting,Terminal,Frozen,Search,Engage,Reposition,Support,Recover };
constexpr size_t ActionCount=static_cast<size_t>(Action::Count);
struct Context
{
    bool Playing=false,Ended=false,Frozen=false,Mannequin=false,Visible=false,Remembered=false,HasAlly=false,HumanAlly=false;
    float Range=1,Stamina=1,Health=1,AllyDistance=0,AllyThreat=0;
    Pattern::Summary Opponent{};
};
struct Decision { Root Parent=Root::Suspended;State Child=State::Waiting;Action Tactic=Action::Hold; };
struct Policy
{
    Difficulty Profile=Difficulty::For(2);
    Decision Current{};
    std::array<float,ActionCount> Weights{};
    double NextDecision=0,LastUpdate=-1;
    double AllySupportSince=-1,AllySupportAvailableAt=0;
    uint32_t RandomState=17;
    void Reset(int Level,uint32_t Seed){*this=Policy{};Profile=Difficulty::For(Level);RandomState=Seed?Seed:17;Weights.fill(0);}
    float Random(){RandomState=1664525u*RandomState+1013904223u;return (RandomState>>8)*(1.f/16777216.f);}
    bool IsLegal(Action A,const Context& C,uint16_t Allowed) const
    {
        const auto I=static_cast<size_t>(A);
        if(I>=ActionCount || !(Allowed&(1<<I)))return false;
        if(A==Action::Lure && (!Profile.AllowsDeception()||C.HumanAlly))return false;
        const bool Knows=C.Visible||C.Remembered;
        if(!Knows && (A==Action::Pressure||A==Action::FlankLeft||A==Action::FlankRight||A==Action::Lure||A==Action::Observe))return false;
        return C.HasAlly || (A!=Action::Cover && A!=Action::Regroup);
    }
    Decision Tick(const Context& C,double Now,uint16_t Allowed=0x3ff)
    {
        if(C.Ended){Current={Root::Finished,State::Terminal,Action::Hold};return Current;}
        if(!C.Playing||!Finite(Now)||Now<0){Current={};return Current;}
        if(C.Frozen){Current={Root::Active,State::Frozen,Action::Hold};NextDecision=Now;return Current;}
        if(C.HumanAlly)
        {
            // A helper offers brief cover, not an indefinitely perfect gaze lock or a solo tactical solution.
            const bool Supporting=Current.Tactic==Action::Cover||Current.Tactic==Action::Observe;
            if(Supporting && AllySupportSince>=0 && Now-AllySupportSince>=2.+.5*Profile.Skill)
            {AllySupportAvailableAt=Now+std::max(1.f,Profile.Reaction*2.5f);AllySupportSince=-1;NextDecision=Now;}
            // Never manufacture an opening by forbidding emergency self-protection at point-blank range.
            if(Now<AllySupportAvailableAt&&C.Range>.15f)
                Allowed=static_cast<uint16_t>(Allowed&~((1<<static_cast<int>(Action::Cover))|(1<<static_cast<int>(Action::Observe))));
        }
        else{AllySupportSince=-1;AllySupportAvailableAt=0;}
        // Expired knowledge and difficulty restrictions override an outstanding tactical timer.
        if(Now<NextDecision && Current.Parent==Root::Active && IsLegal(Current.Tactic,C,Allowed))return Current;
        const bool Knows=C.Visible||C.Remembered;
        auto Score=[&](Action A,float Value){return Weights[static_cast<size_t>(A)]=Value;};
        std::array<float,ActionCount> Desired{};
        Desired[static_cast<size_t>(Action::Search)]=Knows?.12f:.8f;
        Desired[static_cast<size_t>(Action::Pressure)]=Knows?(C.Mannequin?.64f+.12f*(1-Clamp(C.Range,0,1)):.24f):0;
        Desired[static_cast<size_t>(Action::FlankLeft)]=Knows?.30f:0;
        Desired[static_cast<size_t>(Action::FlankRight)]=Knows?.30f:0;
        Desired[static_cast<size_t>(Action::Lure)]=Knows?.25f:0;
        Desired[static_cast<size_t>(Action::Cover)]=C.HasAlly?(.15f+C.AllyThreat*.55f)*Profile.Coordination:0;
        Desired[static_cast<size_t>(Action::Regroup)]=C.HasAlly?Clamp(C.AllyDistance,0,1)*Profile.Coordination:0;
        Desired[static_cast<size_t>(Action::Recover)]=C.Mannequin?.02f:(1-Clamp(C.Stamina,0,1))*.9f;
        if(!C.Mannequin)Desired[static_cast<size_t>(Action::Regroup)]+=C.HasAlly?(1-Clamp(C.Health,0,1))*.25f:0;
        Desired[static_cast<size_t>(Action::Observe)]=Knows?(C.Mannequin?.08f:.75f):.10f;
        Desired[static_cast<size_t>(Action::Hold)]=.04f;
        const float Adapt=Profile.Adaptation*Clamp(C.Opponent.Confidence,0,1);
        const float Defence=Clamp(C.Opponent.Defensive,0,1);
        Desired[static_cast<size_t>(Action::Pressure)]-=Adapt*Defence*.55f;
        // Levels 1-3 can respond to visible danger, but never deliberately bait or exploit a turn habit.
        const float Deception=Profile.AllowsDeception()?Adapt:0;
        Desired[static_cast<size_t>(Action::Lure)]+=Deception*Defence;
        Desired[static_cast<size_t>(Action::FlankLeft)]+=Deception*(Defence+.3f*std::max(0.f,-C.Opponent.LeftBias));
        Desired[static_cast<size_t>(Action::FlankRight)]+=Deception*(Defence+.3f*std::max(0.f,C.Opponent.LeftBias));
        Desired[static_cast<size_t>(Action::Observe)]+=Knows&&!C.Mannequin?Adapt*Clamp(C.Opponent.Sprinting,0,1)*.15f:0;
        if(C.HumanAlly)
        {
            Desired[static_cast<size_t>(Action::Cover)]=std::min(.36f,Desired[static_cast<size_t>(Action::Cover)]);
            Desired[static_cast<size_t>(Action::Observe)]*=.7f;
            Desired[static_cast<size_t>(Action::Regroup)]+=C.HasAlly?.10f:0;
        }
        // Exponential smoothing: observations cannot instantly jump a score from 0.3 to 0.9.
        const float Dt=LastUpdate>=0?Clamp(static_cast<float>(Now-LastUpdate),0,3):0;
        const float Alpha=LastUpdate<0?1.f:1-std::exp(-Dt/2.f);LastUpdate=Now;
        float Total=0;std::array<float,ActionCount> Chances{};
        for(size_t I=0;I<ActionCount;++I)
        {
            const float Target=Finite(Desired[I])?Clamp(Desired[I],0,1.1f):0;
            Score(static_cast<Action>(I),Clamp(Weights[I]+Alpha*(Target-Weights[I]),0,1.1f));
            const bool Legal=IsLegal(static_cast<Action>(I),C,Allowed);
            if(Legal){Chances[I]=std::pow(std::max(.001f,Weights[I]),2.8f+2.2f*Profile.Skill);Total+=Chances[I];}
        }
        if(Total<=0){Current={Root::Active,State::Waiting,Action::Hold};return Current;}
        float Pick=Random()*Total;Action Chosen=Action::Hold;
        for(size_t I=0;I<ActionCount;++I){Pick-=Chances[I];if(Pick<=0){Chosen=static_cast<Action>(I);break;}}
        if(Profile.Opening>0&&IsLegal(Action::Search,C,Allowed)&&Random()<Profile.Opening)Chosen=Action::Search; // Natural disengagement, not suicide or frozen controls.
        State Child=State::Engage;
        switch(Chosen){case Action::Search:Child=State::Search;break;case Action::Cover:case Action::Regroup:Child=State::Support;break;
            case Action::Recover:Child=State::Recover;break;case Action::FlankLeft:case Action::FlankRight:case Action::Lure:Child=State::Reposition;break;
            case Action::Hold:Child=State::Waiting;break;default:break;}
        if(C.HumanAlly&&(Chosen==Action::Cover||Chosen==Action::Observe)){if(AllySupportSince<0)AllySupportSince=Now;}
        else AllySupportSince=-1;
        Current={Root::Active,Child,Chosen};NextDecision=Now+Profile.Reaction*(C.HumanAlly?1.25f:1.f)*(.85f+Random()*.4f);return Current;
    }
};

inline Vec Steer(Vec Position,Vec PathDirection,const Vec* Neighbours,size_t Count,float Separation,float Speed,float Remaining)
{
    if(!Position.Valid()||!PathDirection.Valid()||!Finite(Separation)||!Finite(Speed)||!Finite(Remaining)||Speed<=0)return {};
    PathDirection.Z=0;const Vec Forward=PathDirection.Unit();if(Forward.Length()<.5f)return {};
    Vec Push{};
    for(size_t I=0;I<std::min(Count,size_t(12));++I)if(Neighbours&&Neighbours[I].Valid())
    {
        Vec Away=Position-Neighbours[I];Away.Z=0;const float D=Away.Length();
        if(D>.01f&&D<Separation)Push=Push+Away.Unit()*((Separation-D)/std::max(1.f,Separation));
    }
    // Bounded lateral correction: cannot reverse the path, or replace navigation with flocking.
    Push=Push-Forward*Dot(Push,Forward);
    if(Push.Length()>.30f)Push=Push.Unit()*.30f;
    const float Slow=Clamp(Remaining/120.f,.15f,1.f);
    return (Forward+Push).Unit()*(Speed*Slow);
}
}
