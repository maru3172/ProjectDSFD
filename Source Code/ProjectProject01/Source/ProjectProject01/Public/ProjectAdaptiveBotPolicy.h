#pragma once

// Portable, bounded decision model. No world access, neural network, rewards or training runtime.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
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
inline Vec Flat(Vec A){A.Z=0;return A;}
// Same convention as FVector::RotateAngleAxis(Degrees,UpVector).
inline Vec RotateYaw(Vec A,float Degrees)
{const float R=Degrees*3.14159265f/180.f,C=std::cos(R),S=std::sin(R);return {C*A.X-S*A.Y,S*A.X+C*A.Y,A.Z};}
// Offset from the target toward a flank. TargetToSelf points from the opponent to the bot.
inline Vec FlankDirection(Vec TargetToSelf,bool Left,float Degrees){return RotateYaw(Flat(TargetToSelf).Unit(),Left?-Degrees:Degrees);}

// Observed habits, ordered by how much opponent reading they need. Higher levels read more of them.
enum class Trait:uint8_t { Defensive,Sprinting,Watching,TurnBias,RestSpot,EscapeHeading,Count };

struct Difficulty
{
    int Level=2;
    float Skill=0,Reaction=0,Memory=0,Sharing=0,Adaptation=0,Coordination=0,Opening=0,TurnRate=0;
    bool AllowsDeception() const { return Level>=4; }
    // Levels 1-3 only read openly visible habits; turn/rest/escape reading is psychological play (4-5).
    int TraitBudget() const { static constexpr int Budget[]={1,2,3,5,6};return Budget[std::max(1,std::min(5,Level))-1]; }
    float EvolutionSeconds() const { return 480.f-60.f*(std::max(1,std::min(5,Level))-1); }
    float Progress(double MatchSeconds) const { return Finite(MatchSeconds)?Clamp(static_cast<float>(MatchSeconds)/EvolutionSeconds(),0,1):0; }
    // The longer the match runs, the more evidence the bot trusts, never above its level cap.
    float Evolution(double MatchSeconds) const { return .45f+.55f*Progress(MatchSeconds); }
    int UnlockedTraits(double MatchSeconds) const { return std::max(1,std::min(TraitBudget(),1+static_cast<int>(TraitBudget()*Progress(MatchSeconds)))); }
    bool Uses(Trait T,double MatchSeconds) const { return static_cast<int>(T)<UnlockedTraits(MatchSeconds); }
    // Candidate positions judged per tactical re-plan: low levels see fewer environmental options.
    int TacticSamples() const { return 4+2*(std::max(1,std::min(5,Level))-1); }
    // Skilled players vary their approach so they cannot be read: repetition is penalised more at high levels.
    float Variety() const { return .04f+.22f*Skill; }
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
    // Hover text in the bot match setup.
    static const char* Description(int Level)
    {
        switch(Level)
        {
        case 1:return "쉬움 - 초보자들을 위한 난이도입니다.";
        case 2:return "보통 - 게임에 익숙해진 사람들을 위한 난이도입니다.";
        case 3:return "어려움 - 게임에 적응한 사람들을 위한 난이도입니다.";
        case 4:return "전문가 - 여러분들의 전략을 시험해보세요.";
        case 5:return "마스터 - 행운을 빕니다.";
        default:return "";
        }
    }
};

// How human the mistakes are: frequent and large at easy, rare micro-imperfections at master (never zero).
inline float MistakeScale(const Difficulty& D){return Clamp(1.f-.8f*D.Skill,.2f,1.f);}

// Per-bot personality: consistent quirks a player can come to recognise ("this one is jumpy, likes the right side").
struct Persona
{
    float Boldness=.5f,Curiosity=.5f,Patience=.5f,Steadiness=.5f,StaminaGreed=.5f,Handedness=0,Wobble=.5f,Phase=0;
    uint8_t PlayStyle=255; // Style, assigned by role once the bot's side is known; 255 = neutral.
    static Persona From(uint32_t Seed)
    {
        uint32_t S=Seed?Seed:0x9e3779b9u;
        auto Next=[&S](){S^=S<<13;S^=S>>17;S^=S<<5;return (S>>8)*(1.f/16777216.f);};
        // Average of two draws: most bots are moderate, a few are extreme.
        auto Trait=[&Next](){return (Next()+Next())*.5f;};
        Persona P;P.Boldness=Trait();P.Curiosity=Trait();P.Patience=Trait();P.Steadiness=Trait();P.StaminaGreed=Trait();
        P.Handedness=Trait()*2.f-1.f;P.Wobble=Trait();P.Phase=Next()*6.2831853f;return P;
    }
};

// Play style per bot, derived from its personality: the same level plays differently per character.
//  Mannequin: Hunter (presses), Stalker (waits and ambushes), Trickster (feints and baits), Warden (holds exits, coordinates).
//  Survivor:  Runner (dashes, fewer stops), Scout (watches, listens), Supporter (covers, regroups), Sneak (walks quietly, hides).
enum class Style:uint8_t { Hunter,Stalker,Trickster,Warden,Runner,Scout,Supporter,Sneak };
inline Style StyleFor(const Persona& P,bool Mannequin)
{
    const float Bold=Clamp(P.Boldness,0,1),Curious=Clamp(P.Curiosity,0,1),Patient=Clamp(P.Patience,0,1);
    if(Mannequin)
    {
        if(Bold>=.6f)return Style::Hunter;
        if(Patient>=.6f&&Bold<.5f)return Style::Stalker;
        if(Curious>=.6f)return Style::Trickster;
        return Style::Warden;
    }
    if(Bold>=.6f&&P.StaminaGreed>=.5f)return Style::Runner;
    if(Curious>=.6f)return Style::Scout;
    if(Patient>=.6f)return Style::Supporter;
    return Bold<.45f?Style::Sneak:Style::Runner;
}
inline const char* StyleName(Style S)
{
    switch(S){case Style::Hunter:return "사냥꾼";case Style::Stalker:return "매복꾼";case Style::Trickster:return "책략가";case Style::Warden:return "수문장";
        case Style::Runner:return "질주형";case Style::Scout:return "정찰형";case Style::Supporter:return "지원형";default:return "은신형";}
}

// Startle freeze when an opponent suddenly appears close: scaled by level and nerve, zero beyond 9 m.
inline float StartleSeconds(const Difficulty& D,const Persona& P,float Distance)
{
    if(!Finite(Distance)||Distance<0||Distance>=900)return 0;
    return (.1f+.4f*MistakeScale(D))*(1.25f-.5f*Clamp(P.Boldness,0,1))*(1-Distance/900.f);
}

struct Pattern
{
    struct Bucket { int64_t Epoch=-1;float Seen=0,Still=0,Sprint=0,Watching=0,Left=0,Right=0; };
    std::array<Bucket,12> Bins{}; // Twelve 5-second bins: constant storage, recent 60 seconds only.
    // Longer-lived but still expiring habits: where the opponent stops, and which way it breaks when fleeing.
    static constexpr float HabitHalfLife=90.f,HabitLifetime=180.f,SpotRadius=300.f;
    struct Spot { Vec Position{};float Weight=0;double At=-1; };
    std::array<Spot,6> Spots{};
    float FleeLeft=0,FleeRight=0,FleeStraight=0;double FleeAt=-1;
    double LastObservation=-1;
    uint32_t Count=0;
    static float Decay(double From,double Now){return From<0||Now<From||Now-From>HabitLifetime?0.f:std::exp2(-static_cast<float>(Now-From)/HabitHalfLife);}
    // Position/Velocity/Observer are optional: without real geometry no spatial habit is learned.
    bool Observe(double Now,float Seconds,bool Still,bool Sprint,bool Watching,float Turn,const Vec* Position=nullptr,const Vec* Velocity=nullptr,const Vec* Observer=nullptr)
    {
        if(!Finite(Now)||Now<0||Now>1.e12||!Finite(Seconds)||Seconds<=0||!Finite(Turn))return false;
        Now=std::max(0.,Now+Clock);
        // One shared sample per opponent/time slice: ten bodies cannot inflate confidence tenfold.
        if(LastObservation>=0 && Now-LastObservation<.19)return false;
        const double Gap=LastObservation>=0?Now-LastObservation:Seconds;
        const float Duration=Clamp(static_cast<float>(Gap<=.5?Gap:Seconds),0,.25f);
        const auto Epoch=static_cast<int64_t>(std::floor(Now/5.));
        auto& B=Bins[static_cast<size_t>(Epoch%12)];
        if(B.Epoch!=Epoch)B=Bucket{Epoch};
        B.Seen+=Duration;B.Still+=Still?Duration:0;B.Sprint+=Sprint?Duration:0;B.Watching+=Watching?Duration:0;
        B.Left+=Turn<-.1f?Duration:0;B.Right+=Turn>.1f?Duration:0;
        if(Position&&Position->Valid()&&Still)
        {
            Spot* Slot=nullptr;
            for(auto& S:Spots)if(S.At>=0&&Flat(S.Position-*Position).Length()<SpotRadius){Slot=&S;break;}
            if(!Slot){Slot=&*std::min_element(Spots.begin(),Spots.end(),[Now](const Spot& A,const Spot& C){return A.Weight*Decay(A.At,Now)<C.Weight*Decay(C.At,Now);});*Slot=Spot{*Position,0,Now};}
            Slot->Weight=std::min(30.f,Slot->Weight*Decay(Slot->At,Now)+Duration);Slot->At=Now;
        }
        if(Position&&Velocity&&Observer&&Position->Valid()&&Velocity->Valid()&&Observer->Valid()&&Sprint)
        {
            const Vec Away=Flat(*Position-*Observer).Unit(),Run=Flat(*Velocity).Unit();
            if(Away.Length()>.5f&&Run.Length()>.5f&&Dot(Away,Run)>.2f)
            {
                const float Keep=Decay(FleeAt,Now),Side=Away.X*Run.Y-Away.Y*Run.X;
                FleeLeft*=Keep;FleeRight*=Keep;FleeStraight*=Keep;FleeAt=Now;
                (Side>.2f?FleeRight:Side<-.2f?FleeLeft:FleeStraight)+=Duration;
            }
        }
        LastObservation=Now;if(Count<std::numeric_limits<uint32_t>::max())++Count;return true;
    }
    // Carry the learned habits to a new map whose clock started over. Offset = new time - old time for the
    // same instant. Map-specific knowledge (rest spot positions) is dropped; behaviour habits are kept.
    // Stored times stay untouched (shifting them toward a fresh clock near 0 would go negative); instead the
    // pattern keeps reading on its original timeline through Clock.
    void Rebase(double Offset)
    {
        if(!Finite(Offset))return;
        Clock-=Offset;
        Spots={};
    }
    double Clock=0; // Added to the caller's time: maps a new map's clock onto the timeline these habits were learned on.
    struct Summary
    {
        float Confidence=0,Defensive=0,Sprinting=0,Watching=0,LeftBias=0,ObservedSeconds=0;
        Vec RestSpot{};float RestConfidence=0;
        // +1: breaks toward positive yaw of the observer->opponent line, -1: negative yaw.
        float EscapeSide=0,EscapeConfidence=0;
    };
    Summary Read(double Now)const
    {
        Summary S;if(!Finite(Now)||Now<0||Now>1.e12)return S;
        Now=std::max(0.,Now+Clock);
        float BestSpot=0;
        for(const auto& Spot:Spots)if(const float W=Spot.Weight*Decay(Spot.At,Now);W>BestSpot){BestSpot=W;S.RestSpot=Spot.Position;}
        S.RestConfidence=Clamp((BestSpot-1.f)/5.f,0,1);
        const float Keep=Decay(FleeAt,Now),Fled=(FleeLeft+FleeRight+FleeStraight)*Keep;
        if(Fled>.001f){S.EscapeSide=Clamp((FleeRight-FleeLeft)*Keep/Fled,-1,1);S.EscapeConfidence=Clamp((Fled-1.f)/5.f,0,1);}
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
    bool Publish(const Report& R,bool ActuallyVisible,float Seconds,bool Still,bool Sprint,bool Watching,float Turn,const Vec* Observer=nullptr)
    {
        if(!ActuallyVisible||!R.Source||!R.Target||!Finite(R.At)||R.At<0||R.At>1.e12||!R.Position.Valid()||!R.Velocity.Valid())return false;
        Entry* Slot=nullptr;
        for(auto& E:Entries)if(E.Seen.Team==R.Team&&E.Seen.Target==R.Target){Slot=&E;break;}
        if(!Slot)for(auto& E:Entries)if(!E.Seen.Target){Slot=&E;break;}
        if(!Slot){Slot=&*std::min_element(Entries.begin(),Entries.end(),[](const Entry& A,const Entry& B){return A.Seen.At<B.Seen.At;});*Slot=Entry{};}
        if(R.At<Slot->Seen.At)return false;
        Slot->Statistics.Observe(R.At,Seconds,Still,Sprint,Watching,Turn,&R.Position,&R.Velocity,Observer);Slot->Seen=R;return true;
    }
    // Habit statistics outlive the short position memory; their own decay still expires them.
    const Pattern* FindHabits(uint32_t Team,uint32_t Target)const
    {
        for(const auto& E:Entries)if(E.Seen.Target==Target&&E.Seen.Team==Team&&Target)return &E.Statistics;
        return nullptr;
    }
    // Restore habits learned on an earlier floor for a (new) target id. No position is known yet:
    // Find() stays empty until the opponent is actually seen again; only the habits come back.
    bool SeedHabits(uint32_t Team,uint32_t Target,const Pattern& Habits)
    {
        if(!Target)return false;
        for(const auto& E:Entries)if(E.Seen.Team==Team&&E.Seen.Target==Target)return false; // Already learning anew.
        Entry* Slot=nullptr;
        for(auto& E:Entries)if(!E.Seen.Target){Slot=&E;break;}
        if(!Slot)return false;
        Slot->Seen=Report{Team,0,Target,{},{},-1};Slot->Statistics=Habits;return true;
    }
    const Entry* Find(uint32_t Team,uint32_t Target,double Now,float Lifetime)const
    {
        if(!Finite(Now)||!Finite(Lifetime)||Lifetime<=0)return nullptr;
        // At<0: habits restored without a sighting yet (SeedHabits) carry no position.
        for(const auto& E:Entries)if(E.Seen.Target==Target&&E.Seen.Team==Team&&E.Seen.At>=0&&E.Seen.At<=Now&&Now-E.Seen.At<=std::min(Lifetime,8.f))return &E;
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
    double MatchSeconds=0;
    Pattern::Summary Opponent{};
};
struct Decision { Root Parent=Root::Suspended;State Child=State::Waiting;Action Tactic=Action::Hold; };
struct Policy
{
    Difficulty Profile=Difficulty::For(2);
    Persona Character{};
    Decision Current{};
    std::array<float,ActionCount> Weights{};
    std::array<float,ActionCount> Recent{}; // Decaying count of recently chosen tactics.
    double NextDecision=0,LastUpdate=-1,ComplacentUntil=-1,RecentAt=-1;
    float Relief=0; // In-match relief (0..Relief::Cap), set by the adapter each tick.
    double AllySupportSince=-1,AllySupportAvailableAt=0;
    uint32_t RandomState=17;
    void Reset(int Level,uint32_t Seed){*this=Policy{};Profile=Difficulty::For(Level);RandomState=Seed?Seed:17;Weights.fill(0);}
    // After a success people relax for a moment; lower levels and bolder characters relax longer.
    void Relax(double Now,float Seconds){if(Finite(Now)&&Finite(Seconds)&&Seconds>0)ComplacentUntil=std::max(ComplacentUntil,Now+std::min(Seconds,8.f));}
    bool Complacent(double Now)const{return Finite(Now)&&Now<ComplacentUntil;}
    float Random(){RandomState=1664525u*RandomState+1013904223u;return (RandomState>>8)*(1.f/16777216.f);}
    // What this level has learned to read so far: locked habits are invisible to it, not just down-weighted.
    Pattern::Summary Perceived(const Pattern::Summary& Raw,double MatchSeconds)const
    {
        Pattern::Summary S=Raw;
        if(!Profile.Uses(Trait::Defensive,MatchSeconds))S.Defensive=0;
        if(!Profile.Uses(Trait::Sprinting,MatchSeconds))S.Sprinting=0;
        if(!Profile.Uses(Trait::Watching,MatchSeconds))S.Watching=0;
        if(!Profile.Uses(Trait::TurnBias,MatchSeconds))S.LeftBias=0;
        if(!Profile.Uses(Trait::RestSpot,MatchSeconds)){S.RestConfidence=0;S.RestSpot={};}
        if(!Profile.Uses(Trait::EscapeHeading,MatchSeconds)){S.EscapeConfidence=0;S.EscapeSide=0;}
        return S;
    }
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
        if(!C.Mannequin)
        {
            // Gaze lock is a limited resource. A helper beside a human offers brief cover; a bot-only
            // team holds longer at higher level but still looks away, so the mannequin always gets a window.
            const bool Supporting=Current.Tactic==Action::Cover||Current.Tactic==Action::Observe;
            const double Limit=C.HumanAlly?2.+.5*Profile.Skill:1.5+3.*Profile.Skill;
            if(Supporting && AllySupportSince>=0 && Now-AllySupportSince>=Limit)
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
        const auto Seen=Perceived(C.Opponent,C.MatchSeconds);
        const float Adapt=Profile.Adaptation*Profile.Evolution(C.MatchSeconds)*Clamp(Seen.Confidence,0,1);
        const float Defence=Clamp(Seen.Defensive,0,1);
        Desired[static_cast<size_t>(Action::Pressure)]-=Adapt*Defence*.55f;
        // Levels 1-3 can respond to visible danger, but never deliberately bait or exploit a turn habit.
        const float Deception=Profile.AllowsDeception()?Adapt:0;
        Desired[static_cast<size_t>(Action::Lure)]+=Deception*Defence;
        Desired[static_cast<size_t>(Action::FlankLeft)]+=Deception*(Defence+.3f*std::max(0.f,-Seen.LeftBias));
        Desired[static_cast<size_t>(Action::FlankRight)]+=Deception*(Defence+.3f*std::max(0.f,Seen.LeftBias));
        Desired[static_cast<size_t>(Action::Observe)]+=Knows&&!C.Mannequin?Adapt*Clamp(Seen.Sprinting,0,1)*.15f:0;
        if(Knows&&C.Mannequin)
        {
            // Openly visible habits (level 2-3+): avoid a watching gaze, cut a runner off instead of tailing it.
            const float Watch=Clamp(Seen.Watching,0,1),Run=Clamp(Seen.Sprinting,0,1);
            Desired[static_cast<size_t>(Action::Pressure)]-=Adapt*Watch*.25f;
            Desired[static_cast<size_t>(Action::FlankLeft)]+=Adapt*(Watch*.15f+Run*.10f);
            Desired[static_cast<size_t>(Action::FlankRight)]+=Adapt*(Watch*.15f+Run*.10f);
            // Master only: anticipate the habitual break side. FlankLeft lands on the positive-yaw escape side.
            const float Escape=Deception*Clamp(Seen.EscapeConfidence,0,1)*.4f;
            Desired[static_cast<size_t>(Action::FlankLeft)]+=Escape*std::max(0.f,Seen.EscapeSide);
            Desired[static_cast<size_t>(Action::FlankRight)]+=Escape*std::max(0.f,-Seen.EscapeSide);
        }
        // Personality quirks: small but consistent, so a bot reads as somebody rather than an optimiser.
        const float Bold=Clamp(Character.Boldness,0,1)-.5f,Hand=Clamp(Character.Handedness,-1,1);
        if(Knows&&C.Mannequin)Desired[static_cast<size_t>(Action::Pressure)]+=Bold*.16f;
        Desired[static_cast<size_t>(Action::Recover)]+=C.Mannequin?0:std::max(0.f,-Bold)*.12f;
        if(Knows){Desired[static_cast<size_t>(Action::FlankLeft)]+=std::max(0.f,-Hand)*.06f;Desired[static_cast<size_t>(Action::FlankRight)]+=std::max(0.f,Hand)*.06f;}
        if(Complacent(Now)&&Knows)Desired[static_cast<size_t>(Action::Pressure)]-=.15f*MistakeScale(Profile);
        // Play style: a consistent lean, small enough that the level still decides how good the play is.
        auto Lean=[&](Action A,float V){Desired[static_cast<size_t>(A)]+=V;};
        switch(static_cast<Style>(Character.PlayStyle))
        {
        case Style::Hunter:   if(Knows)Lean(Action::Pressure,.08f);break;
        case Style::Stalker:  if(Knows){Lean(Action::Pressure,-.06f);Lean(Action::Observe,.06f);}Lean(Action::Hold,.04f);break;
        case Style::Trickster:if(Knows){Lean(Action::Lure,.06f);Lean(Action::FlankLeft,.04f);Lean(Action::FlankRight,.04f);}break;
        case Style::Warden:   Lean(Action::Regroup,C.HasAlly?.05f:0.f);Lean(Action::Cover,C.HasAlly?.05f:0.f);break;
        case Style::Runner:   Lean(Action::Recover,-.05f);if(Knows)Lean(Action::Observe,-.08f);break;
        case Style::Scout:    if(Knows)Lean(Action::Observe,.06f);break;
        case Style::Supporter:Lean(Action::Cover,C.HasAlly?.08f:0.f);Lean(Action::Regroup,C.HasAlly?.06f:0.f);break;
        case Style::Sneak:    Lean(Action::Recover,.06f);Lean(Action::Hold,.04f);break;
        default:break;
        }
        const float Keep=RecentAt>=0?std::exp(-static_cast<float>(std::max(0.,Now-RecentAt))/20.f):1.f;
        for(auto& R:Recent)R*=Keep;RecentAt=Now;
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
            const Action A=static_cast<Action>(I);const bool Legal=IsLegal(A,C,Allowed);
            // Fresh approaches: an attack used repeatedly in the last ~20 s is picked less, more so by experts.
            // Selection only; the learned utility itself stays intact.
            const bool Attack=A==Action::Pressure||A==Action::FlankLeft||A==Action::FlankRight||A==Action::Lure;
            const float Stale=Attack&&Knows?Profile.Variety()*std::min(1.f,Recent[I]/3.f):0.f;
            if(Legal){Chances[I]=std::pow(std::max(.001f,Weights[I]-Stale),2.8f+2.2f*Profile.Skill);Total+=Chances[I];}
        }
        if(Total<=0){Current={Root::Active,State::Waiting,Action::Hold};return Current;}
        float Pick=Random()*Total;Action Chosen=Action::Hold;
        for(size_t I=0;I<ActionCount;++I){Pick-=Chances[I];if(Pick<=0){Chosen=static_cast<Action>(I);break;}}
        // Natural disengagement, not suicide or frozen controls. A relaxed winner also drifts off briefly.
        const float Opening=Profile.Opening+(Complacent(Now)?.3f*MistakeScale(Profile):0.f)+.15f*Clamp(Relief,0,1);
        if(Opening>0&&IsLegal(Action::Search,C,Allowed)&&Random()<Opening)Chosen=Action::Search;
        State Child=State::Engage;
        switch(Chosen){case Action::Search:Child=State::Search;break;case Action::Cover:case Action::Regroup:Child=State::Support;break;
            case Action::Recover:Child=State::Recover;break;case Action::FlankLeft:case Action::FlankRight:case Action::Lure:Child=State::Reposition;break;
            case Action::Hold:Child=State::Waiting;break;default:break;}
        if(!C.Mannequin&&(Chosen==Action::Cover||Chosen==Action::Observe)){if(AllySupportSince<0)AllySupportSince=Now;}
        else AllySupportSince=-1;
        // Human rhythm: usually quick, occasionally a longer think. Long pauses get rarer with level and patience.
        float Rhythm=(.7f+Random()*.5f)*(1+.35f*Clamp(Relief,0,1));
        if(Random()<(.06f+.14f*MistakeScale(Profile))*(1.3f-.6f*Clamp(Character.Patience,0,1)))Rhythm*=1.6f+Random();
        Recent[static_cast<size_t>(Chosen)]=std::min(10.f,Recent[static_cast<size_t>(Chosen)]+1);
        Current={Root::Active,Child,Chosen};NextDecision=Now+Profile.Reaction*(C.HumanAlly?1.25f:1.f)*Rhythm;return Current;
    }
};

// Expected break direction of a fleeing opponent from the observer's side; zero when no habit is known.
inline Vec PredictFlee(Vec Observer,Vec Opponent,const Pattern::Summary& S)
{
    const Vec Away=Flat(Opponent-Observer).Unit();
    if(Away.Length()<.5f||S.EscapeConfidence<=0||!Finite(S.EscapeSide))return {};
    return RotateYaw(Away,60.f*Clamp(S.EscapeSide,-1,1))*Clamp(S.EscapeConfidence,0,1);
}

// Environment-aware tactical point: the adapter samples real navmesh points and visibility,
// this scores them. Origin is the target for a mannequin approach, the bot itself for a survivor.
struct TacticQuery
{
    Action Tactic=Action::Hold;
    Vec Self{},Target{},Origin{},Preferred{};
    float Radius=400,FlankDegrees=85;
    bool WantsSight=false; // Survivors freeze mannequins by keeping them in view.
};
struct TacticCandidate { Vec Point{};bool Reachable=false,Hidden=false; };
inline float ScoreTacticPoint(const TacticQuery& Q,const TacticCandidate& C,const Difficulty& D)
{
    if(!C.Reachable||!C.Point.Valid()||!Q.Origin.Valid()||!Finite(Q.Radius)||Q.Radius<=1)return -1;
    const Vec Offset=Flat(C.Point-Q.Origin);const float Distance=Offset.Length();
    const Vec Direction=Offset.Unit();Vec Side=Flat(Q.Self-Q.Target).Unit();if(Side.Length()<.5f)Side={1,0,0};
    const float Fit=1-Clamp(std::abs(Distance-Q.Radius)/Q.Radius,0,1);
    const bool Flank=Q.Tactic==Action::FlankLeft||Q.Tactic==Action::FlankRight;
    // Hold = play-driven point: only the preferred direction steers it, not the bot's own side.
    const float Lane=Q.Tactic==Action::Hold?0.f:Dot(Direction,Flank?FlankDirection(Side,Q.Tactic==Action::FlankLeft,Q.FlankDegrees):Side);
    // Low levels mostly ignore cover; high levels deliberately use corners and occluders.
    const float Cover=(.15f+.45f*D.Skill)*(C.Hidden?1.f:0.f)*(Q.WantsSight?-1.f:1.f);
    const float Anticipate=Q.Preferred.Valid()?.4f*Dot(Direction,Flat(Q.Preferred)):0;
    return 1+.45f*Fit+.35f*Lane+Cover+Anticipate;
}

// ---------------------------------------------------------------------------------------------
// Telegraphed plays. Every special pattern has a readable signal phase, an execution phase,
// a cooldown and a counter the player can learn. Higher levels unlock more plays, signal them
// more briefly (never zero) and reuse them sooner; psychological plays stay at levels 4-5.
// ---------------------------------------------------------------------------------------------
enum class Play:uint8_t
{
    None,
    Listen,       // Heard footsteps: stops, turns to the sound, then checks it.        Counter: walk, don't run.
    ExitAmbush,   // Waits near an exit after losing everyone.                          Counter: scout the exit, pull it away.
    PeekDash,     // Survivor: peeks a corner, then sprints across.                      Counter: wait past the corner.
    GazeRelay,    // Watched body freezes, another unwatched body moves in.              Counter: back to a wall, share views.
    Pincer,       // One body shows from one side, the controlled one closes the other.  Counter: keep both in one view.
    CoverBreak,   // Goes for the survivor who is freezing a mannequin to cover an ally. Counter: cover briefly, then move.
    BackToBack,   // Survivor pair watch opposite directions.                            Counter: split them up.
    StillAmbush,  // Stands like a display piece on your route, strikes once you pass.  Counter: remember positions, look back.
    DecoyBody,    // Exposes one body to hold your gaze while another approaches.        Counter: don't stare, check the sides.
    HabitFlank,   // Reads turn/escape habits, then flanks that side.                    Counter: mix up turns and escape routes.
    BaitRunner,   // Survivor bot baits the mannequin while its partner runs the exit.   Counter: ignore the bait, guard the exit.
    FeintFlank,   // Starts toward one side, then commits to the other.                  Counter: don't over-commit to one side.
    ExitFeint,    // Survivor heads for one exit, then switches.                         Counter: hold between the exits.
    StepApproach, // Creeps a few steps whenever unwatched, then stands still again.     Counter: look back often.
    HideStill,    // Survivor hears footsteps and freezes in place to stay silent.       Counter: search where the steps stopped.
    Count
};
constexpr size_t PlayCount=static_cast<size_t>(Play::Count);
struct PlayInfo { int MinLevel;bool Psych,Body;float Signal,Duration,Cooldown; };
inline const PlayInfo& Info(Play P)
{
    // Body plays take over movement of the controlled body: only one at a time.
    static constexpr PlayInfo Table[PlayCount]={
        {9,false,false,0,0,0},
        {1,false,true ,.7f, 5,  6},  // Listen
        {2,false,true ,1.2f,18,45},  // ExitAmbush
        {2,false,false,.5f, 2,  8},  // PeekDash
        {3,false,true ,.8f, 6, 14},  // GazeRelay
        {3,false,true ,1.f, 8, 25},  // Pincer
        {3,false,true ,.8f, 7, 25},  // CoverBreak
        {3,false,false,.6f,10, 20},  // BackToBack
        {4,true ,true ,.6f,10, 35},  // StillAmbush
        {4,true ,true ,1.f, 8, 30},  // DecoyBody
        {4,true ,false,.6f, 4, 18},  // HabitFlank
        {4,true ,true ,1.f, 8, 35},  // BaitRunner
        {5,true ,true ,.9f, 5, 25},  // FeintFlank
        {5,true ,true ,3.f, 6, 40},  // ExitFeint: the visible run toward the first exit is the tell.
        {2,false,true ,.6f, 8, 20},  // StepApproach
        {2,false,true ,.5f, 5, 15}}; // HideStill
    const auto I=static_cast<size_t>(P);return Table[I<PlayCount?I:0];
}

// In-match relief after the human side suffered several setbacks in a short time.
// Capped per level and almost absent at master.
struct Relief
{
    float Setbacks=0;double At=-1,Clock=0; // Clock: see Pattern::Rebase.
    static float Cap(const Difficulty& D){static constexpr float C[]={1.f,.8f,.5f,.25f,.08f};return C[std::max(1,std::min(5,D.Level))-1];}
    float PendingAt(double T)const{return At<0||!Finite(T)||T<At?Setbacks:Setbacks*std::exp(-static_cast<float>(T-At)/150.f);}
    float Pending(double Now)const{return PendingAt(Now+Clock);}
    void Add(double Now,float Amount){if(!Finite(Now)||!Finite(Amount)||Amount<=0)return;const double T=Now+Clock;Setbacks=std::min(6.f,PendingAt(T)+Amount);At=T;}
    float Value(double Now,const Difficulty& D)const{return Clamp(Pending(Now)/3.f,0,1)*Cap(D);}
    void Rebase(double Offset){if(Finite(Offset))Clock-=Offset;}
};

struct Director
{
    struct Slot { Play Kind=Play::None;double SignalEnds=-1,Ends=-1; };
    std::array<Slot,3> Slots{};
    std::array<double,PlayCount> ReadyAt{};
    static int Concurrency(const Difficulty& D){static constexpr int L[]={1,1,2,2,3};return L[std::max(1,std::min(5,D.Level))-1];}
    static bool Unlocked(Play P,const Difficulty& D){const auto& I=Info(P);return P!=Play::None&&D.Level>=I.MinLevel&&(!I.Psych||D.AllowsDeception());}
    static int UnlockedCount(const Difficulty& D){int N=0;for(size_t I=1;I<PlayCount;++I)N+=Unlocked(static_cast<Play>(I),D);return N;}
    // Shorter tells at high level, never instant; relief lengthens them.
    static float SignalSeconds(Play P,const Difficulty& D,float Relief){return std::max(.35f,Info(P).Signal*(1.3f-.6f*D.Skill)*(1+.6f*Clamp(Relief,0,1)));}
    static float CooldownSeconds(Play P,const Difficulty& D,float Relief){return Info(P).Cooldown*(1.35f-.5f*D.Skill)*(1+Clamp(Relief,0,1));}
    const Slot* Find(Play P)const{for(const auto& S:Slots)if(S.Kind==P)return &S;return nullptr;}
    int ActiveCount()const{int N=0;for(const auto& S:Slots)N+=S.Kind!=Play::None;return N;}
    bool BodyBusy()const{for(const auto& S:Slots)if(S.Kind!=Play::None&&Info(S.Kind).Body)return true;return false;}
    bool Ready(Play P,const Difficulty& D,double Now)const{return Unlocked(P,D)&&Finite(Now)&&Now>=ReadyAt[static_cast<size_t>(P)]&&!Find(P);}
    // TeamActive: plays already running on allied bots, so a team never stacks more than its level allows.
    bool Start(Play P,const Difficulty& D,double Now,float Relief,int TeamActive=0)
    {
        if(!Ready(P,D,Now)||ActiveCount()+std::max(0,TeamActive)>=Concurrency(D)||(Info(P).Body&&BodyBusy()))return false;
        for(auto& S:Slots)if(S.Kind==Play::None)
        {const float Signal=SignalSeconds(P,D,Relief);S={P,Now+Signal,Now+Signal+Info(P).Duration};return true;}
        return false;
    }
    void Finish(Play P,const Difficulty& D,double Now,float Relief)
    {for(auto& S:Slots)if(S.Kind==P){S={};ReadyAt[static_cast<size_t>(P)]=Now+CooldownSeconds(P,D,Relief);}}
    void Update(const Difficulty& D,double Now,float Relief){for(auto& S:Slots)if(S.Kind!=Play::None&&Now>=S.Ends)Finish(S.Kind,D,Now,Relief);}
    bool Signalling(Play P,double Now)const{const Slot* S=Find(P);return S&&Now<S->SignalEnds;}
    bool Executing(Play P,double Now)const{const Slot* S=Find(P);return S&&Now>=S->SignalEnds&&Now<S->Ends;}
    bool Active(Play P)const{return Find(P)!=nullptr;}
};

// How readily a style reaches for a play (start chance per check). Favourites ~1, dislikes ~0.35.
// Only shapes preference: level unlocks, psych gating, tells and cooldowns stay with the Director.
inline float PlayAffinity(uint8_t PlayStyle,Play P)
{
    auto Is=[P](std::initializer_list<Play> L){for(Play X:L)if(X==P)return true;return false;};
    switch(static_cast<Style>(PlayStyle))
    {
    case Style::Hunter:   return Is({Play::Pincer,Play::CoverBreak,Play::FeintFlank})?1.f:Is({Play::StillAmbush,Play::ExitAmbush,Play::StepApproach})?.35f:.7f;
    case Style::Stalker:  return Is({Play::StillAmbush,Play::ExitAmbush,Play::Listen,Play::StepApproach})?1.f:Is({Play::Pincer,Play::FeintFlank})?.35f:.7f;
    case Style::Trickster:return Is({Play::DecoyBody,Play::FeintFlank,Play::HabitFlank})?1.f:Is({Play::ExitAmbush})?.35f:.7f;
    case Style::Warden:   return Is({Play::ExitAmbush,Play::GazeRelay,Play::Pincer})?1.f:Is({Play::StillAmbush})?.35f:.7f;
    case Style::Runner:   return Is({Play::PeekDash,Play::ExitFeint})?1.f:Is({Play::BackToBack,Play::HideStill})?.35f:.7f;
    case Style::Scout:    return Is({Play::BackToBack,Play::Listen,Play::PeekDash})?1.f:.7f;
    case Style::Supporter:return Is({Play::BaitRunner,Play::BackToBack})?1.f:Is({Play::ExitFeint})?.35f:.7f;
    case Style::Sneak:    return Is({Play::Listen,Play::ExitFeint,Play::HideStill})?1.f:Is({Play::PeekDash,Play::BaitRunner})?.35f:.7f;
    default:return .7f;
    }
}

// Footsteps are audible: running carries far, walking barely. Walls muffle (occluded = 60%).
inline float HearingRadius(float Speed2D,bool Occluded)
{
    if(!Finite(Speed2D))return 0;
    const float Radius=Speed2D>350.f?1400.f:Speed2D>150.f?500.f:0.f;
    return Occluded?Radius*.6f:Radius;
}

// Two (or more) survivors in sight: who to go for, by level.
//  1: nearest, but flips between targets (indecision).   2: commits to one until it is lost.
//  3: picks the isolated or exhausted one.                4-5: also punishes whoever is covering an ally.
struct TargetCandidate { float Distance=0,Stamina=1;bool Isolated=false,Covering=false,Current=false; };
inline int ChooseTarget(const Difficulty& D,const TargetCandidate* C,size_t Count,float Random01)
{
    if(!C||Count==0)return -1;
    int Nearest=-1,Current=-1,Other=-1;
    for(size_t I=0;I<Count;++I)
    {
        if(!Finite(C[I].Distance))continue;
        if(Nearest<0||C[I].Distance<C[Nearest].Distance)Nearest=static_cast<int>(I);
        if(C[I].Current)Current=static_cast<int>(I);
    }
    if(Nearest<0)return -1;
    if(D.Level<=1)
    {
        for(size_t I=0;I<Count;++I)if(static_cast<int>(I)!=Nearest&&Finite(C[I].Distance)){Other=static_cast<int>(I);break;}
        return Other>=0&&Random01<.35f?Other:Nearest; // "wait, that one!"
    }
    if(D.Level==2)return Current>=0?Current:Nearest;
    int Best=-1;float BestScore=-1e9f;
    for(size_t I=0;I<Count;++I)
    {
        if(!Finite(C[I].Distance))continue;
        float S=-C[I].Distance/1000.f+(C[I].Isolated?.6f:0.f)+(1-Clamp(C[I].Stamina,0,1))*.5f+(C[I].Current?.4f:0.f);
        if(D.Level>=4&&C[I].Covering)S+=.8f;
        if(S>BestScore){BestScore=S;Best=static_cast<int>(I);}
    }
    return Best;
}

inline float WrapDegrees(float A){A=std::fmod(A+180.f,360.f);if(A<0)A+=360.f;return A-180.f;}

// Human-like gaze: an under-damped spring (slight overshoot, then settle) plus smoothed hand tremor.
// Replaces constant-rate turning, which reads as a turret.
struct Gaze
{
    float Yaw=0,Pitch=0,YawSpeed=0,PitchSpeed=0,TremorYaw=0,TremorPitch=0;
    bool Initialized=false;
    void Snap(float InYaw,float InPitch){Yaw=WrapDegrees(InYaw);Pitch=Clamp(InPitch,-80,80);YawSpeed=PitchSpeed=TremorYaw=TremorPitch=0;Initialized=true;}
    // NoiseYaw/NoisePitch: fresh random samples in [-1,1] supplied by the caller.
    void Step(float TargetYaw,float TargetPitch,float Dt,const Difficulty& D,const Persona& P,float NoiseYaw,float NoisePitch)
    {
        if(!Finite(TargetYaw)||!Finite(TargetPitch)||!Finite(Dt)||Dt<=0)return;
        if(!Initialized){Snap(TargetYaw,TargetPitch);return;}
        Dt=std::min(Dt,.25f);
        const float Steady=Clamp(P.Steadiness,0,1),Mistake=MistakeScale(D);
        const float Omega=D.TurnRate/12.f,Zeta=.5f+.25f*D.Skill+.2f*Steady,MaxSpeed=D.TurnRate*1.5f;
        const float Amp=(.3f+2.2f*Mistake)*(1.3f-.6f*Steady);
        const float Blend=1-std::exp(-Dt*3.f);
        TremorYaw+=(Clamp(Finite(NoiseYaw)?NoiseYaw:0,-1,1)*Amp-TremorYaw)*Blend;
        TremorPitch+=(Clamp(Finite(NoisePitch)?NoisePitch:0,-1,1)*Amp*.5f-TremorPitch)*Blend;
        for(float Left=Dt;Left>0;Left-=1/60.f)
        {
            const float H=std::min(Left,1/60.f);
            const float ErrorYaw=WrapDegrees(TargetYaw+TremorYaw-Yaw),ErrorPitch=Clamp(TargetPitch,-80,80)+TremorPitch-Pitch;
            YawSpeed=Clamp(YawSpeed+(Omega*Omega*ErrorYaw-2*Zeta*Omega*YawSpeed)*H,-MaxSpeed,MaxSpeed);
            PitchSpeed=Clamp(PitchSpeed+(Omega*Omega*ErrorPitch-2*Zeta*Omega*PitchSpeed)*H,-MaxSpeed,MaxSpeed);
            Yaw=WrapDegrees(Yaw+YawSpeed*H);Pitch=Clamp(Pitch+PitchSpeed*H,-80,80);
        }
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
