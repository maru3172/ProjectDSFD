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
    // Habit growth time: 8 min at easy down to 4 min at master (restored from the halved 4..2 min, which let the
    // mannequin read survivors' escape and rest habits within most matches).
    // Balance runs only: EvolutionScale multiplies it (1 = as designed).
    static float& EvolutionScale(){static float Scale=1.f;return Scale;}
    float EvolutionSeconds() const { return (480.f-60.f*(std::max(1,std::min(5,Level))-1))*Clamp(EvolutionScale(),.25f,4.f); }
    float Progress(double MatchSeconds) const { return Finite(MatchSeconds)?Clamp(static_cast<float>(MatchSeconds)/EvolutionSeconds(),0,1):0; }
    // Trust in read habits at match start: 15% for beginners up to 55% at master.
    float InitialTrust() const { return .05f+.1f*std::max(1,std::min(5,Level)); }
    // The longer the match runs, the more evidence the bot trusts, never above its level cap.
    float Evolution(double MatchSeconds) const { return InitialTrust()+(1-InitialTrust())*Progress(MatchSeconds); }
    int UnlockedTraits(double MatchSeconds) const { return std::max(1,std::min(TraitBudget(),1+static_cast<int>(TraitBudget()*Progress(MatchSeconds)))); }
    bool Uses(Trait T,double MatchSeconds) const { return static_cast<int>(T)<UnlockedTraits(MatchSeconds); }
    // Candidate positions judged per tactical re-plan: low levels see fewer environmental options.
    int TacticSamples() const { return 4+2*(std::max(1,std::min(5,Level))-1); }
    // Skilled players vary their approach so they cannot be read: repetition is penalised more at high levels.
    float Variety() const { return .04f+.22f*Skill; }
    static float BaseSkill(int Level){const float T=(std::max(1,std::min(5,Level))-1)/4.f;return T*T;}
    // Per-match condition (-1 bad day .. +1 good day). It moves skill by at most a fifth of the gap to the
    // neighbouring level, so a level never reaches its neighbour. Level-keyed tables do not change.
    static float ConditionSkill(int Level,float Condition)
    {
        const int L=std::max(1,std::min(5,Level));const float C=Finite(Condition)?Clamp(Condition,-1,1):0.f,S=BaseSkill(L);
        const float Up=L<5?BaseSkill(L+1)-S:0.f,Down=L>1?S-BaseSkill(L-1):0.f;
        return Clamp(S+(C>=0?C*.2f*Up:C*.2f*Down),0,1);
    }
    static Difficulty For(int Level,float Condition)
    {
        Difficulty D=For(Level);const float S=ConditionSkill(D.Level,Condition);
        D.Reaction=1.65f-1.25f*S;D.Memory=2.5f+8.5f*S;D.Sharing=.15f+.75f*S;D.Adaptation=.04f+.48f*S;
        D.Coordination=.12f+.78f*S;D.TurnRate=85.f+125.f*S;D.Skill=S;
        // Balance runs only: give a master one value of the expert curve to find what weakens it.
        if(D.Level==5&&Probe()>0)
        {
            const float E=BaseSkill(4);
            switch(Probe()){case 1:D.Reaction=1.65f-1.25f*E;break;case 2:D.Sharing=.15f+.75f*E;break;case 3:D.Coordination=.12f+.78f*E;break;case 4:D.Skill=E;break;default:break;}
        }
        return D;
    }
    static int& Probe(){static int Value=0;return Value;}
    static Difficulty For(int Level)
    {
        Difficulty D;D.Level=std::max(1,std::min(5,Level));
        const float T=(D.Level-1)/4.f;D.Skill=T*T;
        D.Reaction=1.65f-1.25f*D.Skill;
        D.Memory=2.5f+8.5f*D.Skill;
        D.Sharing=.15f+.75f*D.Skill;
        D.Adaptation=.04f+.48f*D.Skill;
        D.Coordination=.12f+.78f*D.Skill;
        D.Opening=D.Level==1?.65f:D.Level==2?.35f:0.f;
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

// Index into a level table. Balance runs only: -BotSimProbe 5..8 gives a master the expert entry of one table group.
inline int ProbeIndex(const Difficulty& D,int ProbeId){const int I=std::max(1,std::min(5,D.Level))-1;return I==4&&Difficulty::Probe()==ProbeId?3:I;}
// How long a survivor keeps one mannequin frozen with a stare before looking away (bot team: 1.45..3.05 s, kept
// short at the top because a long stare lets the other bodies close in from behind;
// beside a human the helper's cover stays short), and how long the look-away lasts: the mannequin's window.
inline double GazeHoldLimit(const Difficulty& D,bool HumanAlly){return HumanAlly?2.+.5*D.Skill:1.45+1.6*D.Skill;}
inline float LookAwaySeconds(const Difficulty& D){return std::max(.9f,D.Reaction*1.7f);}

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

// How strongly situational boldness (endgame, floor depth, exit proximity) shows for this character:
// bold people and pressing styles take more of it, careful people and waiting styles less (0.5..1.4).
// It only scales the push; it never turns one style into another.
inline float BoldnessScale(const Persona& P)
{
    float Style=1.f;
    switch(P.PlayStyle){case 0:case 4:Style=1.15f;break;case 1:case 7:Style=.8f;break;case 3:case 6:Style=.92f;break;default:break;} // Hunter/Runner, Stalker/Sneak, Warden/Supporter.
    return Clamp((.7f+.6f*Clamp(P.Boldness,0,1))*Style,.5f,1.4f);
}

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
    float Tilt=0;   // Shown frustration (Mood::Tilt), set by the adapter: rushes in, waits less.
    float Urgency=0;// Endgame (EndgameUrgency): mannequin goes for the last survivor, the last survivor goes for the exit.
    float Resolve=0;// Floor depth (FloorResolve): fewer long hesitations and fewer handed-out openings.
    float Stealth=0;// Mannequin Shadow plan (MatchScript): lurk and close in quietly rather than charge.
    float Cohesion=0;// Survivor plan: + stay together (Together), - spread out (Split).
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
            const double Limit=GazeHoldLimit(Profile,C.HumanAlly);
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
        // Endgame: the mannequin presses the last survivor; the last survivor stops watching and makes for the exit.
        // Scaled by who the bot is, so a cautious stalker stays a stalker (BoldnessScale).
        if(const float Hurry=Clamp(Urgency*BoldnessScale(Character),0,1);Hurry>0)
        {
            if(C.Mannequin)
            {
                if(Knows)Desired[static_cast<size_t>(Action::Pressure)]+=.3f*Hurry;
                Desired[static_cast<size_t>(Action::Hold)]-=.15f*Hurry;Desired[static_cast<size_t>(Action::Observe)]-=.15f*Hurry;Desired[static_cast<size_t>(Action::Lure)]-=.1f*Hurry;
            }
            else
            {
                Desired[static_cast<size_t>(Action::Search)]+=.5f*Hurry;
                Desired[static_cast<size_t>(Action::Observe)]-=.3f*Hurry;Desired[static_cast<size_t>(Action::Cover)]-=.3f*Hurry;Desired[static_cast<size_t>(Action::Hold)]-=.1f*Hurry;
            }
        }
        // Match plan leans (MatchScript), already scaled by level commitment and personality fit.
        if(const float Quiet=Clamp(Stealth,0,1.2f);Quiet>0&&C.Mannequin)
        {
            if(Knows){Desired[static_cast<size_t>(Action::Observe)]+=.06f*Quiet;Desired[static_cast<size_t>(Action::Pressure)]-=.03f*Quiet;}
            Desired[static_cast<size_t>(Action::Hold)]+=.04f*Quiet;
        }
        if(const float Together=Clamp(Cohesion,-1.2f,1.2f);Together!=0&&!C.Mannequin&&C.HasAlly)
        {
            Desired[static_cast<size_t>(Action::Regroup)]=std::max(0.f,Desired[static_cast<size_t>(Action::Regroup)]+.1f*Together);
            if(Together>0)Desired[static_cast<size_t>(Action::Cover)]+=.04f*Together;
        }
        // Frustration: pushes straight in and stops waiting it out.
        if(const float Tilted=Clamp(Tilt,0,1);Tilted>0&&C.Mannequin)
        {
            if(Knows)Desired[static_cast<size_t>(Action::Pressure)]+=.2f*Tilted;
            Desired[static_cast<size_t>(Action::Hold)]-=.12f*Tilted;Desired[static_cast<size_t>(Action::Observe)]-=.1f*Tilted;
        }
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
        // Deeper floors hand out a third fewer deliberate openings; complacency and relief stay as they are.
        const float Opening=Profile.Opening*(1-.3f*Clamp(Resolve,0,1))+(Complacent(Now)?.3f*MistakeScale(Profile):0.f)+.15f*Clamp(Relief,0,1);
        if(Opening>0&&IsLegal(Action::Search,C,Allowed)&&Random()<Opening)Chosen=Action::Search;
        State Child=State::Engage;
        switch(Chosen){case Action::Search:Child=State::Search;break;case Action::Cover:case Action::Regroup:Child=State::Support;break;
            case Action::Recover:Child=State::Recover;break;case Action::FlankLeft:case Action::FlankRight:case Action::Lure:Child=State::Reposition;break;
            case Action::Hold:Child=State::Waiting;break;default:break;}
        if(!C.Mannequin&&(Chosen==Action::Cover||Chosen==Action::Observe)){if(AllySupportSince<0)AllySupportSince=Now;}
        else AllySupportSince=-1;
        // Human rhythm: usually quick, occasionally a longer think. Long pauses get rarer with level and patience.
        float Rhythm=(.7f+Random()*.5f)*(1+.35f*Clamp(Relief,0,1));
        if(Random()<(.06f+.14f*MistakeScale(Profile))*(1.3f-.6f*Clamp(Character.Patience,0,1))*(1-.5f*Clamp(Resolve,0,1)))Rhythm*=1.6f+Random();
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
    WatchRetreat, // Survivor stops, faces a close mannequin, then backs away watching it. Counter: a second body from the side.
    CoverRun,     // Survivor pair: one stops to freeze the mannequin, the other runs.     Counter: break the watcher (CoverBreak).
    FalseRetreat, // Survivor runs away in plain view, then doubles back to the exit.      Counter: don't chase far, hold the exit.
    NoiseLure,    // Survivor pair: one runs loudly away from its partner, then goes silent. Counter: ignore the noise, find the quiet one.
    RunForIt,     // Survivor: stops and turns to the exit, then sprints all the way (loud).  Counter: hear the run, cut it off at the exit.
    ZigZag,       // Survivor chased: a sharp side-step, then a zigzag run.                 Counter: don't follow each turn, aim where it's heading.
    LeapFrog,     // Survivor pair: one stands facing back while the other moves, then swap. Counter: strike at the swap, or from the side neither watches.
    SplitRun,     // Survivor pair: both stop facing each other, then run apart and meet at the exit. Counter: chase one, guard the exit.
    SearchSweep,  // Mannequin lost you: searches around the last sighting point by point. Counter: leave the area quietly.
    ExitSweep,    // Mannequin walks a round between the exits it knows, pausing at each.   Counter: time the round, use the far exit.
    Herding,      // Mannequin cuts across between you and the exit instead of coming straight. Counter: switch exits or go round.
    FakeRetreat,  // Mannequin released from a stare backs off, then turns and charges.    Counter: keep watching it after it backs off.
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
        {2,false,true ,.5f, 5, 15},  // HideStill
        {3,false,true ,.5f, 4, 12},  // WatchRetreat
        {3,false,true ,.6f, 6, 18},  // CoverRun
        {4,true ,true ,2.f, 6, 35},  // FalseRetreat: the visible run away is the tell.
        {4,true ,true ,1.5f,6, 35},  // NoiseLure: the loud run is the tell.
        {2,false,true ,.6f, 8, 25},  // RunForIt
        {3,false,true ,.4f, 5, 18},  // ZigZag
        {3,false,true ,.6f,12, 30},  // LeapFrog
        {4,true ,true ,.7f, 6, 35},  // SplitRun
        {2,false,true ,.8f,12, 30},  // SearchSweep
        {3,false,true ,1.f,18, 45},  // ExitSweep: the footsteps along the exit route are the tell.
        {3,false,true ,.8f, 7, 22},  // Herding
        {4,true ,true ,1.2f,5, 30}}; // FakeRetreat: the visible backing off is the tell.
    const auto I=static_cast<size_t>(P);return Table[I<PlayCount?I:0];
}
// Which side uses a play. Both sides get the same number of plays at every level (1/4/9/13/14):
// survivor solo (8) and pair (6) plays together match the mannequin repertoire (14).
enum PlaySide:uint8_t { MannequinSide=1,SurvivorSide=2,BothSides=3 };
inline uint8_t SideOf(Play P)
{
    switch(P)
    {
    case Play::Listen:return BothSides;
    case Play::PeekDash:case Play::BackToBack:case Play::BaitRunner:case Play::ExitFeint:case Play::HideStill:
    case Play::WatchRetreat:case Play::CoverRun:case Play::FalseRetreat:case Play::NoiseLure:
    case Play::RunForIt:case Play::ZigZag:case Play::LeapFrog:case Play::SplitRun:return SurvivorSide;
    case Play::None:case Play::Count:return 0;
    default:return MannequinSide;
    }
}
// Pair plays need a bot partner; the rest work alone.
inline bool PairPlay(Play P){return P==Play::BackToBack||P==Play::BaitRunner||P==Play::CoverRun||P==Play::NoiseLure||P==Play::LeapFrog||P==Play::SplitRun;}

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
    static int Concurrency(const Difficulty& D){static constexpr int L[]={1,1,2,2,2};return L[std::max(1,std::min(5,D.Level))-1];}
    static bool Unlocked(Play P,const Difficulty& D){const auto& I=Info(P);return P!=Play::None&&D.Level>=I.MinLevel&&(!I.Psych||D.AllowsDeception());}
    static int UnlockedCount(const Difficulty& D){int N=0;for(size_t I=1;I<PlayCount;++I)N+=Unlocked(static_cast<Play>(I),D);return N;}
    static int UnlockedCount(const Difficulty& D,uint8_t Side){int N=0;for(size_t I=1;I<PlayCount;++I)N+=(SideOf(static_cast<Play>(I))&Side)&&Unlocked(static_cast<Play>(I),D);return N;}
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
    case Style::Hunter:   return Is({Play::Pincer,Play::CoverBreak,Play::FeintFlank,Play::Herding})?1.f:Is({Play::StillAmbush,Play::ExitAmbush,Play::StepApproach,Play::ExitSweep})?.35f:.7f;
    case Style::Stalker:  return Is({Play::StillAmbush,Play::ExitAmbush,Play::Listen,Play::StepApproach,Play::SearchSweep})?1.f:Is({Play::Pincer,Play::FeintFlank})?.35f:.7f;
    case Style::Trickster:return Is({Play::DecoyBody,Play::FeintFlank,Play::HabitFlank,Play::FakeRetreat})?1.f:Is({Play::ExitAmbush})?.35f:.7f;
    case Style::Warden:   return Is({Play::ExitAmbush,Play::GazeRelay,Play::Pincer,Play::ExitSweep})?1.f:Is({Play::StillAmbush})?.35f:.7f;
    case Style::Runner:   return Is({Play::PeekDash,Play::ExitFeint,Play::FalseRetreat,Play::RunForIt,Play::ZigZag})?1.f:Is({Play::BackToBack,Play::HideStill})?.35f:.7f;
    case Style::Scout:    return Is({Play::BackToBack,Play::Listen,Play::PeekDash,Play::WatchRetreat,Play::LeapFrog})?1.f:.7f;
    case Style::Supporter:return Is({Play::BaitRunner,Play::BackToBack,Play::CoverRun,Play::NoiseLure,Play::LeapFrog,Play::SplitRun})?1.f:Is({Play::ExitFeint})?.35f:.7f;
    case Style::Sneak:    return Is({Play::Listen,Play::ExitFeint,Play::HideStill,Play::WatchRetreat})?1.f:Is({Play::PeekDash,Play::BaitRunner,Play::RunForIt})?.35f:.7f;
    default:return .7f;
    }
}

// ---------------------------------------------------------------------------------------------
// Survivor craft. The game's core rule: a mannequin freezes while a survivor looks at it. Skilled
// survivors keep threats in view while they move, back away from danger, route around mannequins
// and split the watching with a partner. These scale with level the same way the mannequin does.
// ---------------------------------------------------------------------------------------------
// Chance to keep discipline (watch while moving, retreat instead of blundering) per decision window.
inline float WatchDiscipline(const Difficulty& D){static constexpr float L[]={.25f,.49f,.68f,.86f,.95f};return L[ProbeIndex(D,5)];}
// Deliberate lapses even when disciplined: a look-away the mannequin can punish. Rare and short at master.
inline float LapsePeriod(const Difficulty& D){return 2.f+6.f*D.Skill;}
inline float LapseLength(const Difficulty& D){return .5f-.3f*D.Skill;}
inline bool InWatchLapse(double Now,float Phase,const Difficulty& D)
{
    if(!Finite(Now))return false;
    const double Period=LapsePeriod(D),Offset=Clamp(Phase,0,6.3f)/6.2831853*Period;
    return std::fmod(Now+Offset,Period)<LapseLength(D);
}
struct ThreatView { Vec Position{};float Distance=0;bool CoveredByPartner=false; };
// Which mannequin to keep in view: the nearest one nobody on the team is already freezing.
inline int PickWatchTarget(const ThreatView* Threats,size_t Count,float Range)
{
    int Best=-1,Covered=-1;float BestD=Range,CoveredD=Range;
    for(size_t I=0;I<Count&&Threats;++I)
    {
        const float D=Threats[I].Distance;if(!Finite(D)||D>Range)continue;
        if(!Threats[I].CoveredByPartner){if(D<BestD){BestD=D;Best=static_cast<int>(I);}}
        else if(D<CoveredD){CoveredD=D;Covered=static_cast<int>(I);}
    }
    // Everything nearby is covered: still watch the closest one if it is really close (partner may look away).
    return Best>=0?Best:(Covered>=0&&CoveredD<500.f?Covered:-1);
}
// Direction to back away: away from nearby threats (closer weighs more), bent toward the objective
// whenever that does not lead back into them. Zero when nothing is close.
inline Vec RetreatDirection(Vec Self,const ThreatView* Threats,size_t Count,float DangerRadius,Vec Objective)
{
    Vec Push{};
    for(size_t I=0;I<Count&&Threats;++I)
    {
        const float D=Threats[I].Distance;if(!Finite(D)||D>DangerRadius||D<1.f)continue;
        const Vec Away=Flat(Self-Threats[I].Position).Unit();
        Push=Push+Away*((DangerRadius-D)/DangerRadius);
    }
    if(Push.Length()<.001f)return {};
    Push=Push.Unit();
    const Vec ToGoal=Flat(Objective-Self).Unit();
    if(ToGoal.Length()>.5f&&Dot(ToGoal,Push)>-.2f)Push=(Push*.65f+ToGoal*.35f).Unit();
    return Push;
}
// Fleeing must not run into another mannequin: keep the wanted heading if no known threat lies ahead
// within Range, else try 45/90/135 degrees either side, else the heading with the most clearance.
inline Vec ClearHeading(Vec Self,Vec Wanted,const ThreatView* Threats,size_t Count,float Range)
{
    Wanted=Flat(Wanted).Unit();if(Wanted.Length()<.5f)return {};
    auto Clearance=[&](Vec Heading){float Worst=Range;
        for(size_t I=0;I<Count&&Threats;++I)
        {
            const Vec To=Flat(Threats[I].Position-Self);const float D=To.Length();
            if(!Finite(D)||D>Range||D<1.f||Dot(Heading,To.Unit())<.55f)continue;
            Worst=std::min(Worst,D);
        }
        return Worst;};
    static constexpr float Offsets[]={0,45,-45,90,-90,135,-135};
    Vec Best=Wanted;float BestClear=-1;
    for(float Offset:Offsets)
    {
        const Vec Heading=RotateYaw(Wanted,Offset);const float Clear=Clearance(Heading);
        if(Clear>=Range)return Heading;
        if(Clear>BestClear){BestClear=Clear;Best=Heading;}
    }
    return Best;
}

// Self-made patterns. A bot remembers which play it ran right after another and whether the pair
// worked; a pair that keeps working becomes its own combo and is reached for again. Every step is
// still an existing play, so the combo keeps each step's tell, cooldown and counter, and the opener's
// tell announces it. Level controls how many combos a bot keeps, how fast it learns and how strongly
// it reaches for them.
struct ComboBook
{
    struct Link { Play From=Play::None,To=Play::None;float Score=0; };
    std::array<Link,4> Links{};
    Play Last=Play::None;double LastEnded=-100;
    static int Capacity(const Difficulty& D){static constexpr int C[]={1,1,2,3,4};return C[ProbeIndex(D,6)];}
    static float Needed(const Difficulty& D){static constexpr float N[]={4,3,3,2,2};return N[ProbeIndex(D,6)];}
    static float Bias(const Difficulty& D){static constexpr float B[]={.15f,.25f,.35f,.5f,.65f};return B[ProbeIndex(D,6)];}
    static constexpr float Window=6.f; // Seconds after the opener in which the follow-up counts as one combo.
    // A play ended; Worked = it ended without a setback for this bot.
    void Ended(Play P,bool Worked,double Now,const Difficulty& D)
    {
        if(P==Play::None||!Finite(Now))return;
        if(Last!=Play::None&&Last!=P&&Now>=LastEnded&&Now-LastEnded<=Window)
        {
            Link* Slot=nullptr;
            for(auto& L:Links)if(L.From==Last&&L.To==P){Slot=&L;break;}
            if(!Slot&&Worked)
            {
                // Only the level's capacity is kept; a new idea replaces the weakest one.
                const int Cap=std::min(static_cast<int>(Links.size()),Capacity(D));
                for(int I=0;I<Cap;++I)if(!Slot||Links[I].Score<Slot->Score)Slot=&Links[I];
                if(Slot&&Slot->Score>=Needed(D))Slot=nullptr; // Never overwrite an established combo.
                if(Slot)*Slot={Last,P,0};
            }
            if(Slot){Slot->Score=Worked?std::min(6.f,Slot->Score+1):Slot->Score-1.5f;if(Slot->Score<=0)*Slot={};}
        }
        Last=P;LastEnded=Now;
    }
    bool Established(Play From,Play To,const Difficulty& D)const
    {const int Cap=std::min(static_cast<int>(Links.size()),Capacity(D));for(int I=0;I<Cap;++I)if(Links[I].From==From&&Links[I].To==To&&Links[I].Score>=Needed(D))return true;return false;}
    int Count(const Difficulty& D)const{int N=0;const int Cap=std::min(static_cast<int>(Links.size()),Capacity(D));for(int I=0;I<Cap;++I)N+=Links[I].From!=Play::None&&Links[I].Score>=Needed(D);return N;}
    // Extra eagerness for Next right after the opener of an established combo.
    float FollowUp(Play Next,double Now,const Difficulty& D)const
    {return Finite(Now)&&Now>=LastEnded&&Now-LastEnded<=Window&&Established(Last,Next,D)?Bias(D):0.f;}
    void Rebase(){Last=Play::None;LastEnded=-100;} // Next floor: keep the combos, not the timing.
    // Next match: a carried combo stays within this level's capacity and is only just established,
    // so two or three failures in the new match are enough to drop it.
    // Level cap: links past this level's capacity are dropped, scores stay just above "established".
    void Cap(const Difficulty& D,float Fade=0)
    {
        const int Limit=std::min(static_cast<int>(Links.size()),Capacity(D));
        for(int I=0;I<static_cast<int>(Links.size());++I)
        {
            auto& L=Links[I];
            const bool Valid=I<Limit&&L.From!=Play::None&&L.To!=Play::None&&L.From!=L.To&&L.From<Play::Count&&L.To<Play::Count&&Finite(L.Score);
            if(!Valid){L={};continue;}
            L.Score=std::min(L.Score-Fade,Needed(D)+1);if(L.Score<=0)L={};
        }
    }
    void CarryOver(const Difficulty& D){Rebase();Cap(D,.5f);}
};

// Self-tuning. Every play has two knobs the bot adjusts from its own results: how long it keeps the
// play going (Duration) and its main distance (Reach: how far it runs, backs off or engages from).
// Each run tries a nearby variant; variants that worked pull the knob their way. The tell (signal
// time) never changes. Level caps how far the knobs may drift and how fast they move.
struct PlayTuning
{
    struct Knob { float Duration=1,Reach=1,TrialDuration=1,TrialReach=1;bool Trial=false; };
    std::array<Knob,PlayCount> Knobs{};
    static float Range(const Difficulty& D){static constexpr float R[]={.08f,.12f,.18f,.24f,.3f};return R[ProbeIndex(D,7)];}
    static float Rate(const Difficulty& D){static constexpr float R[]={.2f,.25f,.3f,.35f,.4f};return R[ProbeIndex(D,7)];}
    static size_t Index(Play P){const auto I=static_cast<size_t>(P);return I>0&&I<PlayCount?I:0;}
    // A play started: try a variant around the current knob (U1/U2 uniform in [0,1)).
    void Begin(Play P,const Difficulty& D,float U1,float U2)
    {
        const size_t I=Index(P);if(!I||Knobs[I].Trial)return;auto& K=Knobs[I];const float R=Range(D),Step=R*.5f;
        K.TrialDuration=Clamp(K.Duration+(Clamp(U1,0,1)*2-1)*Step,1-R,1+R);
        K.TrialReach=Clamp(K.Reach+(Clamp(U2,0,1)*2-1)*Step,1-R,1+R);K.Trial=true;
    }
    // A play ended: move toward a variant that worked, a little away from one that did not.
    void End(Play P,bool Worked,const Difficulty& D)
    {
        const size_t I=Index(P);if(!I||!Knobs[I].Trial)return;auto& K=Knobs[I];const float R=Range(D),Step=Rate(D)*(Worked?1.f:-.5f);
        K.Duration=Clamp(K.Duration+(K.TrialDuration-K.Duration)*Step,1-R,1+R);
        K.Reach=Clamp(K.Reach+(K.TrialReach-K.Reach)*Step,1-R,1+R);K.Trial=false;
    }
    float DurationScale(Play P)const{const size_t I=Index(P);return !I?1.f:Knobs[I].Trial?Knobs[I].TrialDuration:Knobs[I].Duration;}
    float ReachScale(Play P)const{const size_t I=Index(P);return !I?1.f:Knobs[I].Trial?Knobs[I].TrialReach:Knobs[I].Reach;}
    // Next match: back inside this level's range and eased 20% toward the default, so nothing hardens.
    void CarryOver(const Difficulty& D)
    {
        const float R=Range(D);
        for(auto& K:Knobs)
        {
            const float Dur=Finite(K.Duration)?K.Duration:1.f,Rch=Finite(K.Reach)?K.Reach:1.f;
            K={Clamp(1+(Dur-1)*.8f,1-R,1+R),Clamp(1+(Rch-1)*.8f,1-R,1+R),1,1,false};
            K.TrialDuration=K.Duration;K.TrialReach=K.Reach;
        }
        Knobs[0]={};
    }
};

// ---------------------------------------------------------------------------------------------
// Human layer II: what a person knows, feels, says and does while waiting.
// ---------------------------------------------------------------------------------------------
// Map familiarity: chance a bot already knows an exit when a floor begins. The rest it finds by
// seeing it, a teammate's word or a ping. Masters know the building; beginners explore.
inline float MapFamiliarity(const Difficulty& D){static constexpr float F[]={.1f,.25f,.45f,.7f,.9f};return F[ProbeIndex(D,8)];}
inline bool KnowsExitAtStart(const Difficulty& D,uint32_t Hash){return static_cast<float>(Hash%1000u)/1000.f<MapFamiliarity(D);}

// Emotions. Fear jumps on a near miss and fades in seconds; frustration builds on lost or frozen
// chases and fades slower. Composure (level, plus the bot's steadiness) decides how much shows.
struct Mood
{
    float Fear=0,Frustration=0;double At=-1;
    static float Composure(const Difficulty& D,const Persona& P)
    {static constexpr float C[]={.1f,.25f,.45f,.65f,.8f};return Clamp(C[ProbeIndex(D,8)]+.2f*(Clamp(P.Steadiness,0,1)-.5f),0,.95f);}
    void Decay(double Now)
    {
        if(!Finite(Now))return;
        if(At>=0&&Now>At){const float Dt=static_cast<float>(Now-At);Fear*=std::exp(-Dt/4.f);Frustration*=std::exp(-Dt/9.f);}
        At=Now;
    }
    void Startle(double Now,float Amount){Decay(Now);if(Finite(Amount))Fear=Clamp(Fear+Amount,0,1);}
    void Frustrate(double Now,float Amount){Decay(Now);if(Finite(Amount))Frustration=Clamp(Frustration+Amount,0,1);}
    float Panic(const Difficulty& D,const Persona& P)const{return Fear*(1-Composure(D,P));}
    float Tilt(const Difficulty& D,const Persona& P)const{return Frustration*(1-Composure(D,P));}
};

// Talking with pings. How often a bot remembers to call out what it saw, and how late and how far
// off the mark the ping lands. Higher levels and curious characters call out more and more precisely.
inline float PingHabit(const Difficulty& D,const Persona& P)
{static constexpr float H[]={.35f,.5f,.65f,.8f,.9f};return Clamp(H[ProbeIndex(D,8)]*(.8f+.4f*Clamp(P.Curiosity,0,1)),0,1);}
inline float PingDelay(const Difficulty& D,float U){return D.Reaction*(1.f+Clamp(U,0,1))+.3f;}
inline float PingError(const Difficulty& D){return 300.f*MistakeScale(D);}
// Answering a teammate's ping: beginners often miss it.
inline float PingResponse(const Difficulty& D){return .45f+.5f*D.Skill;}

// Per-match condition from the bot and match identity: most matches are ordinary, a few are good or bad
// days. Late in a long match concentration drops a little. Always within ConditionSkill's band.
inline float MatchCondition(uint32_t Seed,double MatchSeconds)
{
    uint32_t S=Seed?Seed:0x2545f491u;
    auto Next=[&S](){S^=S<<13;S^=S>>17;S^=S<<5;return (S>>8)*(1.f/16777216.f);};
    const float Day=(Next()+Next())-1.f; // -1..1, centred
    const float Fatigue=Finite(MatchSeconds)?.3f*Clamp(static_cast<float>(MatchSeconds-600.)/900.f,0,1):0.f;
    return Clamp(Day-Fatigue,-1,1);
}

// Endgame boldness: once only one survivor is left in play (the other caught, escaped or through the exit),
// both sides commit. Stronger at higher level; it is a state for the rest of the floor, not a play.
//  Mannequin tell: louder, straighter, fewer pauses. Counter: it walks into stares and baits more easily.
//  Survivor tell: a constant run for the exits.       Counter: listen for it and guard the exits.
inline float EndgameUrgency(const Difficulty& D,int Survivors,int ActiveSurvivors)
{return Survivors>=2&&ActiveSurvivors==1?.6f+.4f*Clamp(D.Skill,0,1):0.f;}
// The last survivor's way out. Chosen when it is left alone and re-chosen when the situation changes; a state
// for the rest of the floor like the endgame boldness, not a play. Each way has a tell and a weak spot:
//  Dash   - straight for the nearest exit.                 Tell: constant running steps.   Weak: a guarded exit stops it.
//  Lurk   - hide near the exit, wait for the guard to leave. Tell: long silence near exits. Weak: a search sweep finds it; time runs.
//  Detour - quietly round to another exit it knows.          Tell: a wide arc, no rush.     Weak: the long way meets an exit round.
//  Decoy  - run loudly away from the exit, then break for it. Tell: loud steps away from exits. Weak: a mannequin that ignores it.
enum class LoneApproach:uint8_t { Dash,Lurk,Detour,Decoy };
inline LoneApproach ChooseLoneApproach(const Difficulty& D,const Persona& P,bool ExitGuarded,bool OtherExitKnown,float U)
{
    // Style leaning, then the situation; beginners mostly just run for it, experts vary by what they see.
    float W[4]={1.f,.4f,.4f,.4f};
    switch(P.PlayStyle){case 4:W[0]+=.8f;W[3]+=.3f;break;case 5:W[2]+=.6f;W[1]+=.2f;break;case 6:W[3]+=.6f;W[1]+=.2f;break;case 7:W[1]+=.8f;W[2]+=.3f;break;default:break;}
    if(ExitGuarded){W[0]*=.35f;W[1]*=1.6f;W[3]*=1.5f;W[2]*=OtherExitKnown?2.f:1.f;}
    if(!OtherExitKnown)W[2]=0;
    const float Variety=.35f+.65f*Clamp(D.Skill,0,1);for(int I=1;I<4;++I)W[I]*=Variety;
    const float Total=W[0]+W[1]+W[2]+W[3];float Pick=Clamp(U,0,.9999f)*Total;
    for(int I=0;I<4;++I){Pick-=W[I];if(Pick<0)return static_cast<LoneApproach>(I);}
    return LoneApproach::Dash;
}
// How long a lurking survivor is willing to wait before it goes anyway (patient characters and experts wait longer).
inline float LurkPatience(const Difficulty& D,const Persona& P){return 8.f+10.f*Clamp(D.Skill,0,1)+6.f*Clamp(P.Patience,0,1);}

// Bodies a mannequin player of this level leaves parked (the existing Hold command): a beginner does not manage
// all ten bodies. The rest keep their usual AI. The endgame table frees a few more from level 2 up.
// Balance runs only: a value >= 0 replaces the count (0 stands in for a human mannequin, who parks nothing).
inline int& ParkOverride(){static int Value=-1;return Value;}
inline int ParkedBodies(const Difficulty& D,bool Endgame){static constexpr int P[]={9,9,6,5,0},E[]={9,8,5,4,0};if(ParkOverride()>=0)return ParkOverride();const int I=std::max(1,std::min(5,D.Level))-1;return Endgame?E[I]:P[I];}

// Stalemate: no survivor has got closer to an exit and nobody was caught or got out for this long. Then both
// sides break it: survivors drop deliberate holds and move, the mannequin settles on one body and sends the
// rest in. Masters break it sooner. The side that moves first is the side that exposes itself.
inline float StallSeconds(const Difficulty& D){return 25.f-10.f*Clamp(D.Skill,0,1);}

// Floor depth (0 = first floor of the match): each floor down adds firmer judgement, full after four.
// It trims long hesitations and handed-out openings and adds a little steady boldness. The weak spots stay:
// complacency after a catch, gaze lapses, panic, and boldness itself (louder, straighter, easier to bait).
// Match plans. Each match (same id across floors) a team commits to one way of playing, so a run of matches
// lands near the intended spread: the mannequin wins about 4 in 7 (half by open pressure, half by quiet
// pressure); the survivors win about 3 in 7 (one with both out, two with one out). The plan shapes how the
// bots play, never the outcome itself: a player who reads it and answers it wins, one who does not loses
// (and is not let off). Higher levels commit harder, so the plan reads more clearly - and is punished harder
// when it is read. Plans scale with personality fit: a hunter given the quiet plan follows it only lightly.
//  Blitz  (mannequin): bodies converge and go straight in.    Tell: loud, several bodies at once.  Weak: stares and baits.
//  Shadow (mannequin): posts at exits, statues, creeping.     Tell: silence, still figures.         Weak: slow, gaps to run.
//  Together (survivors): stay close, pair plays, both out.    Tell: always in pairs.               Weak: one pincer catches two.
//  Split    (survivors): separate routes, one draws, one runs. Tell: one loud, one gone.           Weak: alone each, easy singles.
enum class MannequinPlan:uint8_t { Blitz,Shadow };
enum class SurvivorPlan:uint8_t { Together,Split };
struct MatchScript
{
    MannequinPlan Mannequin=MannequinPlan::Blitz;
    SurvivorPlan Survivors=SurvivorPlan::Split;
    static MatchScript For(uint32_t Seed)
    {
        uint32_t S=Seed?Seed:0x6a09e667u;auto Next=[&S](){S^=S<<13;S^=S>>17;S^=S<<5;return (S>>8)*(1.f/16777216.f);};
        MatchScript M;M.Mannequin=Next()<.5f?MannequinPlan::Blitz:MannequinPlan::Shadow;
        M.Survivors=Next()<.4f?SurvivorPlan::Together:SurvivorPlan::Split;return M;
    }
    // How firmly a level executes the plan: loosely at easy (mistakes blur it), all-in at master.
    static float Commit(const Difficulty& D){return .35f+.65f*Clamp(D.Skill,0,1);}
    // How well the plan suits this character (0.6..1.2): bold pressing types lean into open plans, careful ones into quiet ones.
    static float Fit(const Persona& P,bool Open){const float B=BoldnessScale(P);return Clamp(Open?B:1.9f-B,.6f,1.2f);}
    static const char* Name(MannequinPlan P){return P==MannequinPlan::Blitz?"Blitz":"Shadow";}
    static const char* Name(SurvivorPlan P){return P==SurvivorPlan::Together?"Together":"Split";}
};
// Play preference under a plan (multiplies the style's own affinity; 1 = unaffected).
inline float PlanAffinity(const MatchScript& M,bool Mannequin,Play P,float Strength)
{
    auto Is=[P](std::initializer_list<Play> L){for(Play X:L)if(X==P)return true;return false;};
    const float S=Clamp(Strength,0,1.2f);
    if(Mannequin)
    {
        // Tuned from bot-vs-bot runs: open pressure was winning every match, quiet pressure too few.
        if(M.Mannequin==MannequinPlan::Blitz)
            return Is({Play::Pincer,Play::CoverBreak,Play::Herding,Play::FakeRetreat,Play::GazeRelay})?1+.15f*S:Is({Play::StillAmbush,Play::ExitSweep,Play::ExitAmbush})?1-.3f*S:1.f;
        return Is({Play::StillAmbush,Play::ExitAmbush,Play::ExitSweep,Play::SearchSweep,Play::StepApproach,Play::DecoyBody})?1+.7f*S:Is({Play::Pincer,Play::FakeRetreat})?1-.3f*S:1.f;
    }
    // Together favours the moving pair plays; standing back to back got both caught at once too often.
    if(M.Survivors==SurvivorPlan::Together)
        return Is({Play::LeapFrog,Play::CoverRun,Play::SplitRun})?1+.4f*S:Is({Play::NoiseLure,Play::BaitRunner,Play::ExitFeint})?1-.2f*S:1.f;
    return Is({Play::NoiseLure,Play::BaitRunner,Play::RunForIt,Play::FalseRetreat,Play::ExitFeint})?1+.4f*S:Is({Play::LeapFrog,Play::BackToBack})?1-.3f*S:1.f;
}

// Exit proximity (0 beyond 25 m, 1 at the exit): survivors commit to the last stretch, mannequins hurry to stop
// a survivor they have seen near one. Weak spots: the survivor gets tunnel vision, the mannequin charges in straight.
inline float ExitProximityBoldness(const Difficulty& D,float Distance)
{return Finite(Distance)?Clamp(1.f-Distance/2500.f,0,1)*(.6f+.4f*Clamp(D.Skill,0,1)):0.f;}
inline float FloorResolve(const Difficulty& D,int Depth){return Clamp(.25f*static_cast<float>(std::max(0,Depth)),0,1)*(.6f+.4f*Clamp(D.Skill,0,1));}

// Fidgets while waiting (elevator, standing guard with nothing in sight): look-around sweeps, a glance
// at the partner, a small shuffle. Curious characters fidget more, patient ones less.
inline float FidgetInterval(const Persona& P,float U){return (1.5f+2.5f*Clamp(U,0,1))*(1.2f-.5f*Clamp(P.Curiosity,0,1)+.3f*Clamp(P.Patience,0,1));}

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
