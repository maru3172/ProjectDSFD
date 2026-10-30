#include "ProjectAdaptiveBotPolicy.h"
#if WITH_DEV_AUTOMATION_TESTS || defined(PROJECT_BOT_STANDALONE)
#include <functional>

static int RunAdaptiveBotChecks(const std::function<void(bool,const char*)>& Verify)
{
    using namespace ProjectBots;int Count=0;
    auto Check=[&](bool Value,const char* Message){++Count;Verify(Value,Message);};
    float Prior=-1,PriorGain=-1;
    for(int L=1;L<=5;++L)
    {
        const auto D=Difficulty::For(L);Check(D.Level==L,"Five exact levels");
        Check(D.Skill>Prior,"Monotonically increasing skill");
        if(L>2)Check(D.Skill-Prior>PriorGain,"Convex, increasingly steep difficulty curve");
        Check(D.Reaction>=.35f&&D.Memory<=11.01f&&D.Adaptation<=.53f,"Hard bounded difficulty profile");
        Check(L<=2||D.Opening==0,"Only easy/normal deliberately create openings");PriorGain=D.Skill-Prior;Prior=D.Skill;
        Check(D.AllowsDeception()==(L>=4),"Deception is exclusive to expert/master");
    }
    Check(Difficulty::For(-100).Level==1&&Difficulty::For(100).Level==5,"Input levels clamped");
    Pattern P;
    for(int I=0;I<300;++I)P.Observe(I*.2,.2f,true,false,true,.5f);
    auto S=P.Read(59.8);Check(S.Confidence>.8f&&S.Defensive>.99f,"Sustained observed defence builds confidence");
    Check(!P.Observe(59.8,.2f,true,false,true,.5f),"Shared observations deduplicated");
    Check(P.Read(125).ObservedSeconds==0,"Expired evidence is forgotten instead of permanent profiling");
    for(int I=300;I<600;++I)P.Observe(I*.2,.2f,false,true,false,-.5f);
    S=P.Read(119.8);Check(S.Sprinting>.99f&&S.Defensive<.01f&&S.LeftBias>.9f,"New behaviour replaces old defence within window");
    Check(!P.Observe(std::numeric_limits<double>::infinity(),.2f,true,true,true,0),"Nonfinite timestamps rejected");
    Pattern Fast,Slow;
    for(int I=0;I<1000;++I)Fast.Observe(I*.01,.01f,true,false,false,0);
    for(int I=0;I<50;++I)Slow.Observe(I*.2,.2f,true,false,false,0);
    Check(std::abs(Fast.Read(9.9).ObservedSeconds-Slow.Read(9.9).ObservedSeconds)<.5f,"Frame frequency does not inflate learning evidence");
    SharedSenses Shared;SharedSenses::Report R{1,7,9,{100,200,0},{5,0,0},10};
    Check(!Shared.Publish(R,false,.2f,true,false,false,0),"Invisible targets cannot publish sensory data");
    Check(Shared.Publish(R,true,.2f,true,false,false,0),"Actually visible target publishes");
    Check(Shared.Find(1,9,11,3)!=nullptr,"Same team receives fresh report");
    Check(Shared.Find(2,9,11,3)==nullptr,"Opponent team cannot read shared senses");
    Check(Shared.Find(1,9,9,3)==nullptr&&Shared.Find(1,9,19,8)==nullptr,"Future and stale reports rejected");
    for(uint32_t I=1;I<100;++I){R.Target=I;R.At=10+I;Shared.Publish(R,true,.2f,false,true,false,0);}
    Check(Shared.Entries.size()==16,"Shared memory capacity remains bounded");
    Policy A;A.Reset(5,31);Context C;C.Playing=true;C.Mannequin=true;C.Visible=true;
    const auto First=A.Tick(C,0);Check(First.Parent==Root::Active,"Active parent state");
    C.Frozen=true;Check(A.Tick(C,.1).Child==State::Frozen&&A.Current.Tactic==Action::Hold,"Vision freeze immediately overrides tactical hold timer");
    C.Ended=true;Check(A.Tick(C,.11).Parent==Root::Finished,"Match end immediately overrides freeze");
    C={};Check(A.Tick(C,.12).Parent==Root::Suspended,"Loading/waiting cannot run tactics");
    Policy Baseline,Adaptive;Baseline.Reset(5,10);Adaptive.Reset(5,10);C.Playing=true;C.Mannequin=true;C.Visible=true;C.MatchSeconds=900;
    Baseline.Tick(C,0);Adaptive.Tick(C,0);C.Opponent.Confidence=1;C.Opponent.Defensive=1;
    for(int I=1;I<=100;++I){Context Plain=C;Plain.Opponent={};Baseline.Tick(Plain,I);Adaptive.Tick(C,I);}
    Check(Adaptive.Weights[4]>Baseline.Weights[4]+.3f,"Observed defence increases lure utility");
    Check(Adaptive.Weights[1]<Baseline.Weights[1]-.15f,"Observed defence reduces direct pressure utility");
    Policy Easy;Easy.Reset(1,10);for(int I=0;I<100;++I)Easy.Tick(C,I);
    Check(Easy.Weights[4]<Adaptive.Weights[4]-.3f,"Easy adaptation cannot become master adaptation");
    Policy Smooth;Smooth.Reset(5,10);Context Plain=C;Plain.Opponent={};Smooth.Tick(Plain,0);
    const float Initial=Smooth.Weights[4];Smooth.NextDecision=0;Smooth.Tick(C,.01);
    Check(Smooth.Weights[4]<Initial+.02f,"New observations cannot abruptly jump tactical weights");
    for(int L=1;L<=5;++L)
    {
        Policy Masked;Masked.Reset(L,19);
        for(int I=0;I<100;++I)Check(Masked.Tick(C,I*2,1<<2).Tactic==Action::FlankLeft,"Difficulty/openings never bypass allowed-action mask");
        Policy Bait;Bait.Reset(L,43);
        Check(Bait.IsLegal(Action::Lure,C,0x3ff)==(L>=4),"Low difficulty cannot enable bait through an action mask");
        for(int I=0;I<100;++I)Check((Bait.Tick(C,I*2).Tactic!=Action::Lure)||L>=4,"Observed habits never unlock low-level deception");
    }
    Policy Lost;Lost.Reset(5,77);Context Known=C;Known.HasAlly=false;
    Check(Lost.Tick(Known,0,1<<2).Tactic==Action::FlankLeft,"Known target allows a flank");
    Known.Visible=Known.Remembered=false;
    const auto Forgotten=Lost.Tick(Known,.01);
    Check(Forgotten.Tactic==Action::Search||Forgotten.Tactic==Action::Hold||Forgotten.Tactic==Action::Recover,"Expired perception interrupts a cached offensive action immediately");
    Context SharedAlly=C;SharedAlly.HasAlly=true;SharedAlly.AllyDistance=1;
    Policy Alone;Alone.Reset(5,22);Alone.Tick(SharedAlly,0,1<<6);SharedAlly.HasAlly=false;
    Check(Alone.Tick(SharedAlly,.01).Tactic!=Action::Regroup,"Lost ally interrupts a cached support action");
    Policy Helper;Helper.Reset(5,31);Context Help=C;Help.Mannequin=false;Help.HumanAlly=true;Help.HasAlly=true;Help.Range=1;
    Check(!Helper.IsLegal(Action::Lure,Help,0x3ff),"A human teammate never delegates deceptive play to a perfect helper");
    Check(Helper.Tick(Help,0,1<<5).Tactic==Action::Cover,"Helper can provide real cover");
    Check(Helper.Tick(Help,3,1<<5).Tactic!=Action::Cover,"Helper cannot sustain perfect cover indefinitely");
    Help.Range=.05f;
    Check(Helper.Tick(Help,3.01,1<<5).Tactic==Action::Cover,"Helper retains emergency self-protection instead of deliberately dying");
    Check(Helper.Profile.Level==5,"Support style does not change the advertised opponent difficulty");
    const Vec Neighbours[]={{0,20,0},{20,30,0},{-10,15,0}};
    Vec V=Steer({}, {1,0,0},Neighbours,3,150,300,500);
    Check(V.X>0&&std::abs(V.Y/V.X)<=.301f&&V.Length()<=300.01f,"Steering preserves forward path and caps lateral correction/speed");
    Check(Steer({}, {},Neighbours,3,150,300,500).Length()==0,"Steering cannot invent a route");
    Check(Steer({}, {1,0,0},nullptr,0,150,300,20).Length()<100,"Arrival deceleration");
    Check(Steer({}, {1,0,0},nullptr,0,150,-1,20).Length()==0,"Invalid movement speed is safe");

    // Match-long evolution and level-scaled pattern repertoire.
    int PriorBudget=0;
    for(int L=1;L<=5;++L)
    {
        const auto D=Difficulty::For(L);
        Check(D.TraitBudget()>PriorBudget,"Higher levels read more opponent patterns");PriorBudget=D.TraitBudget();
        Check(D.UnlockedTraits(0)==1&&D.UnlockedTraits(1.e6)==D.TraitBudget(),"Patterns unlock over the match up to the level cap");
        Check(D.UnlockedTraits(D.EvolutionSeconds()*.5)<=D.UnlockedTraits(D.EvolutionSeconds()),"Unlocks never regress over time");
        Check(D.Evolution(0)<D.Evolution(D.EvolutionSeconds())&&D.Evolution(1.e6)<=1.f,"Evolution grows with match time and stays bounded");
        Check(D.Uses(Trait::TurnBias,1.e6)==(L>=4)&&D.Uses(Trait::RestSpot,1.e6)==(L>=4),"Behaviour reading is expert/master only");
        Check(D.Uses(Trait::EscapeHeading,1.e6)==(L==5),"Escape anticipation is master only");
        Check(D.TacticSamples()==4+2*(L-1),"Higher levels weigh more environmental options");
    }
    Check(Difficulty::For(5).EvolutionSeconds()<Difficulty::For(1).EvolutionSeconds(),"Master evolves faster than easy");
    Check(Difficulty::For(3).Evolution(std::numeric_limits<double>::quiet_NaN())==Difficulty::For(3).Evolution(0),"Invalid match time is safe");
    {
        Context Def=C;Def.Opponent={};Def.Opponent.Confidence=1;Def.Opponent.Defensive=1;
        Policy Early,Late;Early.Reset(5,10);Late.Reset(5,10);
        for(int I=0;I<=100;++I){Def.MatchSeconds=0;Early.Tick(Def,I);Def.MatchSeconds=900;Late.Tick(Def,I);}
        Check(Late.Weights[4]>Early.Weights[4]+.1f,"The same evidence is exploited more as the match goes on");
        Pattern::Summary Raw;Raw.LeftBias=1;Raw.RestConfidence=1;Raw.EscapeConfidence=1;Raw.EscapeSide=1;Raw.Watching=1;
        Policy Hard;Hard.Reset(3,1);const auto Seen3=Hard.Perceived(Raw,1.e6);
        Check(Seen3.LeftBias==0&&Seen3.RestConfidence==0&&Seen3.EscapeConfidence==0&&Seen3.Watching==1,"Level 3 sees open habits but no psychological reads");
        Policy Master;Master.Reset(5,1);
        Check(Master.Perceived(Raw,0).Watching==0&&Master.Perceived(Raw,1.e6).EscapeConfidence==1,"Master starts simple and grows its repertoire");
    }
    {
        Pattern Rest;const Vec Here{500,500,0},Zero{};
        for(int I=0;I<40;++I)Rest.Observe(I*.2,.2f,true,false,false,0,&Here,&Zero,&Zero);
        auto Habit=Rest.Read(8);Check(Habit.RestConfidence>.9f&&Flat(Habit.RestSpot-Here).Length()<1,"Repeated real stops become a rest spot");
        Check(Rest.Read(8+Pattern::HabitLifetime+1).RestConfidence==0,"Rest spots expire");
        Pattern Moving;for(int I=0;I<40;++I)Moving.Observe(I*.2,.2f,false,false,false,0,&Here,&Zero,&Zero);
        Check(Moving.Read(8).RestConfidence==0,"Moving targets do not create rest spots");
        Pattern Blind;for(int I=0;I<40;++I)Blind.Observe(I*.2,.2f,true,false,false,0);
        Check(Blind.Read(8).RestConfidence==0,"No geometry, no spatial habit");
        Pattern Flee;const Vec Watcher{0,0,0},Runner{1000,0,0},Break{250,430,0};
        for(int I=0;I<40;++I)Flee.Observe(I*.2,.2f,false,true,false,0,&Runner,&Break,&Watcher);
        Habit=Flee.Read(8);Check(Habit.EscapeSide>.9f&&Habit.EscapeConfidence>.9f,"Habitual break side is learned from real flight");
        const Vec Predicted=PredictFlee(Watcher,Runner,Habit);
        const Vec TargetToSelf=Watcher-Runner;
        Check(Dot(Predicted,FlankDirection(TargetToSelf,true,85))>Dot(Predicted,FlankDirection(TargetToSelf,false,85)),"Positive break side maps to FlankLeft");
        Check(PredictFlee(Watcher,Runner,Pattern::Summary{}).Length()==0,"No habit, no anticipation");
        Context Esc=C;Esc.Opponent={};Esc.Opponent.Confidence=1;Esc.Opponent.EscapeConfidence=1;Esc.Opponent.EscapeSide=1;
        for(int L:{3,5})
        {
            Policy Cut;Cut.Reset(L,3);for(int I=0;I<=60;++I)Cut.Tick(Esc,I);
            if(L==5)Check(Cut.Weights[2]>Cut.Weights[3]+.1f,"Master cuts off the habitual escape side");
            else Check(std::abs(Cut.Weights[2]-Cut.Weights[3])<.001f,"Lower levels ignore escape habits");
        }
    }
    {
        TacticQuery Q;Q.Tactic=Action::FlankLeft;Q.Self={0,-1000,0};Q.Target={0,0,0};Q.Origin=Q.Target;Q.Radius=400;Q.FlankDegrees=85;
        const Vec LeftLane=FlankDirection(Q.Self-Q.Target,true,85)*400,RightLane=FlankDirection(Q.Self-Q.Target,false,85)*400;
        const auto Novice=Difficulty::For(1),Master=Difficulty::For(5);
        Check(ScoreTacticPoint(Q,{LeftLane,false,false},Master)<0,"Unreachable points are rejected");
        Check(ScoreTacticPoint(Q,{LeftLane,true,false},Master)>ScoreTacticPoint(Q,{RightLane,true,false},Master),"Flank point respects the chosen side");
        const float MasterCover=ScoreTacticPoint(Q,{LeftLane,true,true},Master)-ScoreTacticPoint(Q,{LeftLane,true,false},Master);
        const float EasyCover=ScoreTacticPoint(Q,{LeftLane,true,true},Novice)-ScoreTacticPoint(Q,{LeftLane,true,false},Novice);
        Check(MasterCover>EasyCover&&EasyCover>0,"Higher levels exploit occluding cover more");
        Q.WantsSight=true;
        Check(ScoreTacticPoint(Q,{LeftLane,true,true},Master)<ScoreTacticPoint(Q,{LeftLane,true,false},Master),"Watch positions prefer a clear line of sight");
        Q.WantsSight=false;Q.Preferred=Vec{1,0,0};
        Check(ScoreTacticPoint(Q,{{400,0,0},true,false},Master)>ScoreTacticPoint(Q,{{-400,0,0},true,false},Master),"Anticipated escape direction attracts the cut-off point");
        Q.Radius=std::numeric_limits<float>::quiet_NaN();
        Check(ScoreTacticPoint(Q,{LeftLane,true,false},Master)<0,"Invalid query is safe");
    }

    // Human layer: personality, reasoned mistakes that shrink with level, varied play at high level.
    {
        const auto A1=Persona::From(42),A2=Persona::From(42),B1=Persona::From(43);
        Check(A1.Boldness==A2.Boldness&&A1.Handedness==A2.Handedness,"Same bot keeps the same personality");
        Check(A1.Boldness!=B1.Boldness||A1.Curiosity!=B1.Curiosity,"Different bots get different personalities");
        bool Bounded=true;
        for(uint32_t Seed=0;Seed<500;++Seed){const auto Who=Persona::From(Seed);
            for(float T:{Who.Boldness,Who.Curiosity,Who.Patience,Who.Steadiness,Who.StaminaGreed,Who.Wobble})Bounded&=T>=0&&T<=1;
            Bounded&=Who.Handedness>=-1&&Who.Handedness<=1;}
        Check(Bounded,"Personality traits stay in range");
        float PriorMistake=2;
        for(int L=1;L<=5;++L){const float M=MistakeScale(Difficulty::For(L));Check(M<PriorMistake&&M>=.2f,"Mistakes shrink with level but never vanish");PriorMistake=M;}
        Persona Timid;Timid.Boldness=0;Persona Bold;Bold.Boldness=1;
        Check(StartleSeconds(Difficulty::For(1),Timid,200)>StartleSeconds(Difficulty::For(5),Timid,200),"Beginners startle longer");
        Check(StartleSeconds(Difficulty::For(3),Timid,200)>StartleSeconds(Difficulty::For(3),Bold,200),"Bold characters startle less");
        Check(StartleSeconds(Difficulty::For(1),Timid,950)==0&&StartleSeconds(Difficulty::For(1),Timid,std::numeric_limits<float>::quiet_NaN())==0,"No startle at range or on bad input");
        auto Settle=[](int Level,float Steadiness,float& Peak){
            Gaze G;G.Snap(0,0);Persona Calm;Calm.Steadiness=Steadiness;Peak=0;float T=0;
            for(;T<3.f;T+=1/30.f){G.Step(90,0,1/30.f,Difficulty::For(Level),Calm,0,0);Peak=std::max(Peak,G.Yaw);if(T>.3f&&std::abs(G.Yaw-90)<2&&std::abs(G.YawSpeed)<5)break;}
            return T;};
        float PeakEasy=0,PeakMaster=0;
        const float EasyTime=Settle(1,0,PeakEasy),MasterTime=Settle(5,1,PeakMaster);
        Check(MasterTime<EasyTime&&EasyTime<3.f,"Gaze settles, faster for skilled bots");
        Check(PeakEasy>91.f&&PeakEasy-90>PeakMaster-90,"Unsteady gaze overshoots more than a master's");
        Gaze Wrap;Wrap.Snap(-170,0);for(int I=0;I<5;++I)Wrap.Step(170,0,1/30.f,Difficulty::For(3),Persona{},0,0);
        Check(Wrap.Yaw<-170.f||Wrap.Yaw>170.f,"Gaze turns the short way across 180 degrees");
        Gaze Bad;Bad.Snap(10,0);Bad.Step(std::numeric_limits<float>::infinity(),0,.1f,Difficulty::For(3),Persona{},0,0);
        Check(Bad.Yaw==10,"Invalid gaze target is ignored");
        Policy Relaxed;Relaxed.Reset(2,5);Relaxed.Relax(10,100);
        Check(Relaxed.Complacent(17.9)&&!Relaxed.Complacent(18.1),"Complacency is real but capped");
        auto LongPauses=[](int Level){
            Policy Bot;Bot.Reset(Level,99);Context Busy;Busy.Playing=true;Busy.Visible=true;Busy.Mannequin=true;
            int Long=0;double Now=0;
            for(int I=0;I<600;++I){Bot.Tick(Busy,Now);if(Bot.NextDecision-Now>Bot.Profile.Reaction*1.25)++Long;Now=Bot.NextDecision+.001;}
            return Long;};
        Check(LongPauses(1)>LongPauses(5),"Long hesitations are rarer at high level");
        auto RepeatPicks=[&C](int Level){
            int Picks=0;
            for(uint32_t Seed=1;Seed<=300;++Seed)
            {Policy Bot;Bot.Reset(Level,Seed);Bot.Recent[2]=10;Context K=C;K.Opponent={};if(Bot.Tick(K,0,(1<<2)|(1<<3)).Tactic==Action::FlankLeft)++Picks;}
            return Picks;};
        int Fresh=0;
        for(uint32_t Seed=1;Seed<=300;++Seed){Policy Bot;Bot.Reset(5,Seed);Context K=C;K.Opponent={};if(Bot.Tick(K,0,(1<<2)|(1<<3)).Tactic==Action::FlankLeft)++Fresh;}
        const int MasterRepeat=RepeatPicks(5),EasyRepeat=RepeatPicks(1);
        Check(MasterRepeat<Fresh/2,"A master avoids repeating the same attack");
        Check(MasterRepeat<EasyRepeat,"Variety grows with level");
    }

    // Telegraphed plays: signal -> execute -> cooldown, more plays per level, psychological ones at 4-5.
    {
        int PriorPlays=0;
        for(int L=1;L<=5;++L)
        {
            const auto D=Difficulty::For(L);const int N=Director::UnlockedCount(D);
            Check(N>=PriorPlays&&(L==1||N>PriorPlays),"More plays unlock as the level rises");PriorPlays=N;
            for(size_t I=1;I<PlayCount;++I)
            {
                const auto Kind=static_cast<Play>(I);
                Check(!Info(Kind).Psych||!Director::Unlocked(Kind,D)||L>=4,"Psychological plays stay at levels 4-5");
                Check(Director::SignalSeconds(Kind,D,0)>=.35f,"Every play has a visible tell");
                Check(Director::CooldownSeconds(Kind,D,0)>0,"Every play has a cooldown");
            }
        }
        Check(Director::SignalSeconds(Play::Pincer,Difficulty::For(5),0)<Director::SignalSeconds(Play::Pincer,Difficulty::For(3),0),"Tells are shorter but present at high level");
        const auto Hard=Difficulty::For(3);Director Plan;
        const float Tell=Director::SignalSeconds(Play::Pincer,Hard,0);
        Check(Plan.Start(Play::Pincer,Hard,0,0),"Unlocked play starts");
        Check(Plan.Signalling(Play::Pincer,.1)&&!Plan.Executing(Play::Pincer,.1),"Signal comes before execution");
        Check(Plan.Executing(Play::Pincer,Tell+.01),"Execution follows the signal");
        Check(!Plan.Start(Play::CoverBreak,Hard,.2,0),"Only one body-controlling play at a time");
        Check(Plan.Start(Play::BackToBack,Hard,.2,0),"A non-body play can run alongside within the level limit");
        Check(!Plan.Start(Play::PeekDash,Hard,.3,0),"Level concurrency limit is enforced");
        Plan.Update(Hard,100,0);
        Check(!Plan.Active(Play::Pincer)&&!Plan.Start(Play::Pincer,Hard,101,0),"Finished play enters cooldown");
        Check(Plan.Start(Play::Pincer,Hard,100+Director::CooldownSeconds(Play::Pincer,Hard,0)+.1,0),"Play returns after its cooldown");
        Director Early;
        Check(!Early.Start(Play::StillAmbush,Difficulty::For(3),0,0)&&Early.Start(Play::StillAmbush,Difficulty::For(4),0,0),"Display-piece ambush is expert and up");
        for(Play Basic:{Play::StepApproach,Play::HideStill})
            Check(!Info(Basic).Psych&&!Director::Unlocked(Basic,Difficulty::For(1))&&Director::Unlocked(Basic,Difficulty::For(2)),"Step approach and hide-still are basic plays from level 2");
        Check(Director::UnlockedCount(Difficulty::For(5))==static_cast<int>(PlayCount)-1,"Master has the full, finite repertoire");
        Director Crowded;
        Check(!Crowded.Start(Play::BackToBack,Hard,0,0,2),"Team-wide concurrency is respected");
        // Both sides get the same repertoire size at every level: 1/4/9/13/14.
        static constexpr int Expected[]={1,4,9,13,14};
        for(int L=1;L<=5;++L)
        {
            const auto D=Difficulty::For(L);
            Check(Director::UnlockedCount(D,MannequinSide)==Expected[L-1]&&Director::UnlockedCount(D,SurvivorSide)==Expected[L-1],"Survivor solo+pair plays match the mannequin count per level");
        }
        bool Sided=true;for(size_t I=1;I<PlayCount;++I)Sided&=SideOf(static_cast<Play>(I))!=0;
        Check(Sided,"Every play belongs to a side");
        Check(PairPlay(Play::CoverRun)&&PairPlay(Play::NoiseLure)&&!PairPlay(Play::WatchRetreat)&&!PairPlay(Play::FalseRetreat),"Solo and pair survivor plays are told apart");
        int Solo=0,Pair=0;for(size_t I=1;I<PlayCount;++I){const auto Kind=static_cast<Play>(I);if(SideOf(Kind)&SurvivorSide){Pair+=PairPlay(Kind);Solo+=!PairPlay(Kind);}}
        Check(Solo==8&&Pair==6,"Master survivors: 8 solo and 6 pair plays");
        Check(Director::Unlocked(Play::RunForIt,Difficulty::For(2))&&!Director::Unlocked(Play::ZigZag,Difficulty::For(2))&&Director::Unlocked(Play::LeapFrog,Difficulty::For(3))&&
            !Director::Unlocked(Play::SplitRun,Difficulty::For(3))&&Info(Play::SplitRun).Psych&&Info(Play::FakeRetreat).Psych&&Director::Unlocked(Play::SearchSweep,Difficulty::For(2)),"New plays open at their levels; psychological ones at 4");
        Check(!Director::Unlocked(Play::WatchRetreat,Difficulty::For(2))&&Director::Unlocked(Play::CoverRun,Difficulty::For(3))&&!Director::Unlocked(Play::FalseRetreat,Difficulty::For(3))&&Info(Play::NoiseLure).Psych,"New survivor plays: two at 3, two psychological at 4");
    }
    // Fleeing never heads into another known mannequin.
    {
        const ThreatView Ahead[]={{{1000,0,0},1000,false}};
        const Vec Free=ClearHeading({0,0,0},{1,0,0},Ahead,1,1500.f);
        Check(Dot(Free,{1,0,0})<.8f&&Free.Length()>.5f,"A mannequin ahead turns the run aside");
        const Vec Open=ClearHeading({0,0,0},{0,1,0},Ahead,1,1500.f);
        Check(Dot(Open,{0,1,0})>.99f,"A clear heading is kept");
        const ThreatView Far[]={{{2000,0,0},2000,false}};
        Check(Dot(ClearHeading({0,0,0},{1,0,0},Far,1,1500.f),{1,0,0})>.99f,"Threats past the check range do not bend the run");
        ThreatView Ring[8];for(int I=0;I<8;++I){const Vec At=RotateYaw({600,0,0},45.f*I);Ring[I]={At,600,false};}
        Check(ClearHeading({0,0,0},{1,0,0},Ring,8,1500.f).Length()>.5f,"Surrounded still yields a heading");
    }
    // Self-made patterns: learned from plays that worked together, level-controlled, never above capacity.
    {
        const auto Rookie=Difficulty::For(1),Master=Difficulty::For(5);
        Check(ComboBook::Capacity(Rookie)<ComboBook::Capacity(Master)&&ComboBook::Needed(Rookie)>ComboBook::Needed(Master)&&ComboBook::Bias(Rookie)<ComboBook::Bias(Master),"Combo capacity, learning speed and eagerness rise with level");
        auto Run=[](ComboBook& Book,const Difficulty& D,int Times,bool Worked){double T=0;for(int I=0;I<Times;++I){Book.Ended(Play::PeekDash,true,T,D);Book.Ended(Play::ExitFeint,Worked,T+3,D);T+=60;}};
        ComboBook QuickBook,SlowBook;Run(QuickBook,Master,2,true);Run(SlowBook,Rookie,2,true);
        Check(QuickBook.Established(Play::PeekDash,Play::ExitFeint,Master)&&!SlowBook.Established(Play::PeekDash,Play::ExitFeint,Rookie),"Master invents a combo after two wins, beginners need more");
        QuickBook.Ended(Play::PeekDash,true,1000,Master);
        Check(QuickBook.FollowUp(Play::ExitFeint,1002,Master)==ComboBook::Bias(Master)&&QuickBook.FollowUp(Play::ExitFeint,1000+ComboBook::Window+1,Master)==0&&QuickBook.FollowUp(Play::Listen,1002,Master)==0,"Follow-up only right after its opener");
        ComboBook Unlucky;Run(Unlucky,Master,2,true);Run(Unlucky,Master,3,false);
        Check(!Unlucky.Established(Play::PeekDash,Play::ExitFeint,Master),"A combo that stops working is forgotten");
        ComboBook Gap;Gap.Ended(Play::PeekDash,true,0,Master);Gap.Ended(Play::ExitFeint,true,ComboBook::Window+5,Master);Gap.Ended(Play::PeekDash,true,100,Master);Gap.Ended(Play::ExitFeint,true,100+ComboBook::Window+5,Master);
        Check(Gap.Count(Master)==0,"Plays far apart are not a combo");
        ComboBook Many;double T=0;const Play Kinds[]={Play::Listen,Play::PeekDash,Play::HideStill,Play::WatchRetreat,Play::CoverRun,Play::FalseRetreat};
        for(int Round=0;Round<4;++Round)for(size_t I=0;I+1<6;++I){Many.Ended(Kinds[I],true,T,Rookie);Many.Ended(Kinds[I+1],true,T+1,Rookie);T+=60;}
        Check(Many.Count(Rookie)<=ComboBook::Capacity(Rookie),"Beginners keep at most their level's combos");
        Run(QuickBook,Master,1,true);ComboBook Carried=QuickBook;Carried.Rebase();
        Check(Carried.Established(Play::PeekDash,Play::ExitFeint,Master)&&Carried.FollowUp(Play::ExitFeint,1,Master)==0,"Next floor keeps combos, not the timing");
    }
    // Self-tuning: knobs follow what worked, never leave the level's range, the tell is untouched.
    {
        const auto Rookie=Difficulty::For(1),Master=Difficulty::For(5);
        Check(PlayTuning::Range(Rookie)<PlayTuning::Range(Master)&&PlayTuning::Rate(Rookie)<PlayTuning::Rate(Master),"Tuning range and speed rise with level");
        PlayTuning Up;for(int I=0;I<40;++I){Up.Begin(Play::FalseRetreat,Master,.99f,.99f);Up.End(Play::FalseRetreat,true,Master);}
        Check(Up.ReachScale(Play::FalseRetreat)>1.2f&&Up.ReachScale(Play::FalseRetreat)<=1+PlayTuning::Range(Master)+1e-4f,"Winning longer runs grows the reach up to the master cap");
        PlayTuning Low;for(int I=0;I<40;++I){Low.Begin(Play::FalseRetreat,Rookie,.99f,.99f);Low.End(Play::FalseRetreat,true,Rookie);}
        Check(Low.ReachScale(Play::FalseRetreat)<=1+PlayTuning::Range(Rookie)+1e-4f,"Beginners drift only within their small range");
        PlayTuning Mixed;for(int I=0;I<40;++I){Mixed.Begin(Play::Pincer,Master,.99f,.5f);Mixed.End(Play::Pincer,false,Master);}
        Check(Mixed.DurationScale(Play::Pincer)<=1.f,"A variant that keeps failing is not adopted");
        PlayTuning Once;Once.Begin(Play::Listen,Master,.99f,.99f);const float Trial=Once.ReachScale(Play::Listen);Once.Begin(Play::Listen,Master,0,0);
        Check(Once.ReachScale(Play::Listen)==Trial,"One trial per run");
        Check(PlayTuning::Index(Play::Count)==0&&PlayTuning::Index(Play::None)==0,"Invalid plays are never tuned");
        PlayTuning Carried=Up;Carried.CarryOver(Rookie);
        Check(Carried.ReachScale(Play::FalseRetreat)<=1+PlayTuning::Range(Rookie)+1e-4f&&!Carried.Knobs[static_cast<size_t>(Play::FalseRetreat)].Trial,"Carried tuning is cut back to the receiving level");
        PlayTuning Eased=Up;Eased.CarryOver(Master);
        Check(Eased.ReachScale(Play::FalseRetreat)<Up.ReachScale(Play::FalseRetreat),"Each new match eases knobs toward the default");
        ComboBook Big;for(auto& L:Big.Links)L={Play::PeekDash,Play::ExitFeint,6};Big.Links[1].To=Play::HideStill;Big.Links[2].To=Play::Listen;Big.Links[3].To=Play::BackToBack;
        ComboBook Cut=Big;Cut.Cap(Rookie);
        Check(Cut.Count(Master)<=ComboBook::Capacity(Rookie)&&Cut.Links[0].Score<=ComboBook::Needed(Rookie)+1,"Carried combos obey the receiving level's capacity and score cap");
        ComboBook Next=Big;Next.CarryOver(Master);
        Check(Next.Count(Master)==4&&Next.Links[0].Score<=ComboBook::Needed(Master)+1,"Master keeps its four combos, just established");
    }
    // Human layer II: map knowledge, emotions, pings, fidgets, per-match condition - all level-controlled.
    {
        bool Rising=true;
        for(int L=1;L<5;++L)
        {
            const auto Lo=Difficulty::For(L),Hi=Difficulty::For(L+1);
            Rising&=MapFamiliarity(Lo)<MapFamiliarity(Hi)&&PingHabit(Lo,Persona{})<PingHabit(Hi,Persona{})&&PingError(Lo)>PingError(Hi)&&
                PingResponse(Lo)<PingResponse(Hi)&&Mood::Composure(Lo,Persona{})<Mood::Composure(Hi,Persona{});
            // A good day at L never reaches a bad day at L+1.
            Rising&=Difficulty::For(L,1).Skill<Difficulty::For(L+1,-1).Skill&&Difficulty::For(L,1).Reaction>Difficulty::For(L+1,-1).Reaction;
        }
        Check(Rising,"Map familiarity, ping habits, composure and condition bands all rise with level without overlap");
        Check(Difficulty::For(1,-1).Skill==Difficulty::For(1).Skill&&Difficulty::For(5,1).Skill==Difficulty::For(5).Skill,"Condition never pushes past the ends of the scale");
        Check(Difficulty::For(3,.7f).Level==3&&Difficulty::For(3,.7f).TraitBudget()==Difficulty::For(3).TraitBudget(),"Condition keeps the level and its tables");
        Check(Difficulty::For(3,std::numeric_limits<float>::quiet_NaN()).Skill==Difficulty::For(3).Skill,"Invalid condition is neutral");
        int KnownExits=0;for(uint32_t H=0;H<1000;++H)KnownExits+=KnowsExitAtStart(Difficulty::For(5),H*2654435761u);
        Check(KnownExits>800&&KnownExits<980,"Masters know most exits when a floor starts");
        float Sum=0,Lo=1,Hi=-1;for(uint32_t Seed=1;Seed<=500;++Seed){const float Day=MatchCondition(Seed*747796405u,0);Sum+=Day;Lo=std::min(Lo,Day);Hi=std::max(Hi,Day);}
        Check(Lo>=-1&&Hi<=1&&std::abs(Sum/500)<.1f&&Hi-Lo>1.f,"Condition varies per match around an ordinary day");
        Check(MatchCondition(7,1500)<MatchCondition(7,0)&&MatchCondition(7,300)==MatchCondition(7,0),"Concentration drops only late in a long match");
        Mood Scared;Scared.Startle(0,.8f);const float Early=Scared.Fear;Scared.Decay(8);
        Check(Early>.7f&&Scared.Fear<.15f,"A scare fades within seconds");
        Mood Same;Same.Startle(0,.8f);
        Check(Same.Panic(Difficulty::For(5),Persona{})<Same.Panic(Difficulty::For(1),Persona{})*.4f,"Masters keep their composure; beginners panic");
        Mood Mad;Mad.Frustrate(0,.6f);Mad.Decay(4);
        Check(Mad.Frustration>.3f&&Mad.Tilt(Difficulty::For(1),Persona{})>Mad.Tilt(Difficulty::For(5),Persona{}),"Frustration lingers and shows more at low level");
        Policy Calm,Tilted;Calm.Reset(3,5);Tilted.Reset(3,5);Tilted.Tilt=1;Context Hunt;Hunt.Playing=true;Hunt.Mannequin=true;Hunt.Visible=true;Hunt.Remembered=true;Hunt.Range=.5f;
        int CalmPress=0,TiltPress=0;for(int I=0;I<200;++I){CalmPress+=Calm.Tick(Hunt,I*2.).Tactic==Action::Pressure;TiltPress+=Tilted.Tick(Hunt,I*2.).Tactic==Action::Pressure;}
        Check(TiltPress>CalmPress,"A frustrated mannequin rushes in more often");
        // Endgame: one survivor left in play -> both sides commit; stronger with level, off otherwise.
        Check(EndgameUrgency(Difficulty::For(3),2,2)==0&&EndgameUrgency(Difficulty::For(3),2,1)>0&&EndgameUrgency(Difficulty::For(3),1,1)==0&&
            EndgameUrgency(Difficulty::For(5),2,1)>EndgameUrgency(Difficulty::For(1),2,1),"Endgame boldness only with one survivor left, stronger at master");
        Policy Patient,Hunting;Patient.Reset(3,9);Hunting.Reset(3,9);Hunting.Urgency=1;int PatientPress=0,HuntPress=0;
        for(int I=0;I<200;++I){PatientPress+=Patient.Tick(Hunt,I*2.).Tactic==Action::Pressure;HuntPress+=Hunting.Tick(Hunt,I*2.).Tactic==Action::Pressure;}
        Context Last;Last.Playing=true;Last.Mannequin=false;Last.Visible=true;Last.Remembered=true;Last.Range=.6f;
        Policy Calmer,Rushing;Calmer.Reset(3,11);Rushing.Reset(3,11);Rushing.Urgency=1;int CalmSearch=0,RushSearch=0;
        for(int I=0;I<200;++I){CalmSearch+=Calmer.Tick(Last,I*2.).Tactic==Action::Search;RushSearch+=Rushing.Tick(Last,I*2.).Tactic==Action::Search;}
        Check(HuntPress>PatientPress&&RushSearch>CalmSearch,"Endgame: the mannequin presses, the last survivor heads for the exit");
        // Match plans: about half Blitz / half Shadow, about 40% Together; firmer with level; fit follows character.
        {
            int BlitzCount=0,TogetherCount=0;
            for(uint32_t Seed=1;Seed<=1000;++Seed){const auto M=MatchScript::For(Seed*2246822519u);BlitzCount+=M.Mannequin==MannequinPlan::Blitz;TogetherCount+=M.Survivors==SurvivorPlan::Together;}
            Check(BlitzCount>430&&BlitzCount<570&&TogetherCount>330&&TogetherCount<470,"Plans are drawn at the intended rates");
            Check(MatchScript::For(77).Mannequin==MatchScript::For(77).Mannequin&&MatchScript::For(77).Survivors==MatchScript::For(77).Survivors,"A match keeps its plan on every floor");
            Check(MatchScript::Commit(Difficulty::For(1))<MatchScript::Commit(Difficulty::For(5))&&MatchScript::Commit(Difficulty::For(5))<=1.f,"Higher levels commit to the plan harder");
            Persona Brash,Wary;Brash.Boldness=.8f;Brash.PlayStyle=static_cast<uint8_t>(Style::Hunter);Wary.Boldness=.3f;Wary.PlayStyle=static_cast<uint8_t>(Style::Stalker);
            Check(MatchScript::Fit(Brash,true)>MatchScript::Fit(Wary,true)&&MatchScript::Fit(Wary,false)>MatchScript::Fit(Brash,false),"Each plan suits its own kind of character more");
            MatchScript Quiet;Quiet.Mannequin=MannequinPlan::Shadow;
            Check(PlanAffinity(Quiet,true,Play::StillAmbush,1)>1&&PlanAffinity(Quiet,true,Play::Pincer,1)<1&&PlanAffinity(Quiet,true,Play::Listen,1)==1&&PlanAffinity(Quiet,true,Play::StillAmbush,0)==1,
                "A plan only scales preferences, and nothing at zero strength");
            Policy Open,Lurking;Open.Reset(4,21);Lurking.Reset(4,21);Lurking.Stealth=1;int OpenObserve=0,LurkObserve=0;
            for(int I=0;I<200;++I){OpenObserve+=Open.Tick(Hunt,I*2.).Tactic==Action::Observe;LurkObserve+=Lurking.Tick(Hunt,I*2.).Tactic==Action::Observe;}
            Check(LurkObserve>OpenObserve,"The quiet plan lurks more than the open one");
        }
        // Last survivor's way out: a guarded exit changes the choice; masters vary more; no detour without another exit.
        {
            auto WayCount=[](const Difficulty& D,const Persona& P,bool Guarded,bool Other,LoneApproach Way){int N=0;for(int I=0;I<1000;++I)N+=ChooseLoneApproach(D,P,Guarded,Other,(I+.5f)/1000.f)==Way;return N;};
            const Persona Ordinary;const auto Master=Difficulty::For(5),Rookie=Difficulty::For(1);
            Check(WayCount(Master,Ordinary,true,true,LoneApproach::Dash)<WayCount(Master,Ordinary,false,true,LoneApproach::Dash),"A guarded exit makes the last survivor rethink the straight run");
            Check(WayCount(Rookie,Ordinary,true,true,LoneApproach::Dash)>WayCount(Master,Ordinary,true,true,LoneApproach::Dash),"Beginners mostly just run for it");
            Check(WayCount(Master,Ordinary,true,false,LoneApproach::Detour)==0,"No detour without another known exit");
            Persona Quiet;Quiet.PlayStyle=static_cast<uint8_t>(Style::Sneak);Persona Sprinter;Sprinter.PlayStyle=static_cast<uint8_t>(Style::Runner);
            Check(WayCount(Master,Quiet,true,true,LoneApproach::Lurk)>WayCount(Master,Sprinter,true,true,LoneApproach::Lurk),"Sneaks lurk more than runners");
            Persona Waiter;Waiter.Patience=1;
            Check(LurkPatience(Master,Waiter)>LurkPatience(Rookie,Ordinary),"Patient experts wait longer before going anyway");
        }
        Check(ParkedBodies(Difficulty::For(1),false)>ParkedBodies(Difficulty::For(3),false)&&ParkedBodies(Difficulty::For(5),false)==0&&
            ParkedBodies(Difficulty::For(1),true)<=ParkedBodies(Difficulty::For(1),false)&&ParkedBodies(Difficulty::For(3),true)<ParkedBodies(Difficulty::For(3),false),"Beginners leave more bodies parked; masters none; fewer in the endgame");
        {
            bool Untouched=true,Bounded=true;
            for(int L=1;L<=5;++L)for(bool E:{false,true})
            {
                const auto D=Difficulty::For(L);
                Untouched&=ParkedBodies(D,E,false)==ParkedBodies(D,E)&&SoloAttackScale(D,false)==1.f&&SoloEndgameScale(D,false)==1.f;
                Bounded&=ParkedBodies(D,E,true)>=ParkedBodies(D,E)&&ParkedBodies(D,E,true)<=9&&SoloAttackScale(D,true)>=1.f&&SoloEndgameScale(D,true)>=0.f&&SoloEndgameScale(D,true)<=1.f;
            }
            Check(Untouched&&Bounded,"One-human-with-bot values never touch other teams and only ever ease the mannequin");
        }
        Check(StallSeconds(Difficulty::For(5))<StallSeconds(Difficulty::For(1))&&StallSeconds(Difficulty::For(5))>=15.f,"Masters break a stalemate sooner, never instantly");
        // Floor depth: firmer each floor down, full after four, level-scaled, never negative.
        Check(FloorResolve(Difficulty::For(3),0)==0&&FloorResolve(Difficulty::For(3),1)<FloorResolve(Difficulty::For(3),2)&&
            FloorResolve(Difficulty::For(3),4)==FloorResolve(Difficulty::For(3),9)&&FloorResolve(Difficulty::For(1),4)<FloorResolve(Difficulty::For(5),4)&&
            FloorResolve(Difficulty::For(5),4)<=1&&FloorResolve(Difficulty::For(3),-2)==0,"Floor resolve grows per floor, caps, and scales with level");
        // Boldness follows the character: it scales the push, never swaps one style for another.
        Persona Pressing,Waiting,Neutral;Pressing.Boldness=.8f;Pressing.PlayStyle=static_cast<uint8_t>(Style::Hunter);Waiting.Boldness=.3f;Waiting.PlayStyle=static_cast<uint8_t>(Style::Stalker);
        Check(BoldnessScale(Waiting)<BoldnessScale(Neutral)&&BoldnessScale(Neutral)<BoldnessScale(Pressing)&&BoldnessScale(Waiting)>=.5f&&BoldnessScale(Pressing)<=1.4f,
            "Bold pressing characters show more boldness than careful waiting ones, within bounds");
        Check(PlayAffinity(static_cast<uint8_t>(Style::Stalker),Play::Pincer)*1.4f<PlayAffinity(static_cast<uint8_t>(Style::Stalker),Play::StillAmbush)*1.4f,"A multiplied push keeps the style's own order");
        Check(ExitProximityBoldness(Difficulty::For(3),3000)==0&&ExitProximityBoldness(Difficulty::For(3),500)>ExitProximityBoldness(Difficulty::For(3),1500)&&
            ExitProximityBoldness(Difficulty::For(5),500)>ExitProximityBoldness(Difficulty::For(1),500)&&ExitProximityBoldness(Difficulty::For(5),0)<=1&&
            ExitProximityBoldness(Difficulty::For(3),std::numeric_limits<float>::quiet_NaN())==0,"Boldness rises toward the exit, by level, and is off far away");        Persona Fidgety;Fidgety.Curiosity=1;Fidgety.Patience=0;Persona Still;Still.Curiosity=0;Still.Patience=1;
        Check(FidgetInterval(Fidgety,.5f)<FidgetInterval(Still,.5f)&&FidgetInterval(Still,1)>0,"Curious characters fidget more often than patient ones");
    }
    // Faster growth: half the time, and start trust 15-55% by level.
    {
        static constexpr float Trust[]={.15f,.25f,.35f,.45f,.55f};static constexpr float Seconds[]={480,420,360,300,240};
        bool Ok=true;for(int L=1;L<=5;++L){const auto D=Difficulty::For(L);Ok&=std::abs(D.Evolution(0)-Trust[L-1])<.001f&&std::abs(D.EvolutionSeconds()-Seconds[L-1])<.01f&&std::abs(D.Evolution(1.e6)-1.f)<.001f;}
        Check(Ok,"Evolution takes 8 to 4 minutes and starts at 15/25/35/45/55% trust");
    }
    // In-match relief: real for beginners, almost nothing at master, and it fades.
    {
        Relief Streak;Streak.Add(0,3);
        Check(Streak.Value(0,Difficulty::For(1))>.99f&&Streak.Value(0,Difficulty::For(5))<=.081f,"Relief is capped per level, minimal at master");
        Check(Streak.Value(600,Difficulty::For(1))<.05f,"Relief fades after the losing streak");
        const auto Mid=Difficulty::For(3);
        Check(Director::CooldownSeconds(Play::Pincer,Mid,.5f)>Director::CooldownSeconds(Play::Pincer,Mid,0)&&
              Director::SignalSeconds(Play::Pincer,Mid,.5f)>Director::SignalSeconds(Play::Pincer,Mid,0),"Relief spaces plays out and lengthens tells");
        Context Busy;Busy.Playing=true;Busy.Visible=true;Busy.Mannequin=true;
        Policy Strict,Eased;Strict.Reset(1,11);Eased.Reset(1,11);Eased.Relief=1;
        Strict.Tick(Busy,0);Eased.Tick(Busy,0);
        Check(Eased.NextDecision>Strict.NextDecision,"Relief slows decisions");
    }
    // Two survivors in sight: level-specific choice of target.
    {
        TargetCandidate Pair[2];Pair[0].Distance=300;Pair[1].Distance=800;Pair[1].Isolated=true;Pair[1].Stamina=.1f;
        Check(ChooseTarget(Difficulty::For(2),Pair,2,.9f)==0,"Normal goes for the nearest");
        Pair[1].Current=true;
        Check(ChooseTarget(Difficulty::For(2),Pair,2,.9f)==1,"Normal commits to its current target");
        Pair[1].Current=false;
        Check(ChooseTarget(Difficulty::For(3),Pair,2,.9f)==1,"Hard picks the isolated, exhausted survivor");
        Check(ChooseTarget(Difficulty::For(1),Pair,2,.1f)!=ChooseTarget(Difficulty::For(1),Pair,2,.9f),"Beginners waver between targets");
        TargetCandidate Cover[2];Cover[0].Distance=400;Cover[1].Distance=600;Cover[1].Covering=true;
        Check(ChooseTarget(Difficulty::For(3),Cover,2,.9f)==0&&ChooseTarget(Difficulty::For(4),Cover,2,.9f)==1,"Experts punish the survivor covering an ally");
        Check(ChooseTarget(Difficulty::For(3),nullptr,0,0)==-1,"No candidates, no target");
    }
    // Play styles from personality: every style occurs, it is stable per bot, and it only shapes preference.
    {
        bool SeenStyle[8]={};
        for(uint32_t Seed=1;Seed<=2000;++Seed)
        {
            const auto Who=Persona::From(Seed*2654435761u+1u); // Same seed spreading as the bot adapter.
            SeenStyle[static_cast<int>(StyleFor(Who,true))]=true;SeenStyle[static_cast<int>(StyleFor(Who,false))]=true;
        }
        bool All=true;for(bool Found:SeenStyle)All&=Found;
        Check(All,"Every mannequin and survivor play style appears");
        Check(StyleFor(Persona::From(77),true)==StyleFor(Persona::From(77),true),"A bot keeps its play style");
        bool Bounded=true;
        for(uint8_t Kind=0;Kind<8;++Kind)for(size_t I=1;I<PlayCount;++I){const float Lean=PlayAffinity(Kind,static_cast<Play>(I));Bounded&=Lean>=.3f&&Lean<=1.f;}
        Check(Bounded&&PlayAffinity(255,Play::Pincer)==.7f,"Style affinity stays a bounded preference");
        Check(PlayAffinity(static_cast<uint8_t>(Style::Stalker),Play::StillAmbush)>PlayAffinity(static_cast<uint8_t>(Style::Hunter),Play::StillAmbush),"Stalkers prefer ambushes, hunters do not");
        Check(!Director::Unlocked(Play::StillAmbush,Difficulty::For(3)),"A stalker style still cannot use psychological plays below level 4");
        Context Seen;Seen.Playing=true;Seen.Visible=true;Seen.Mannequin=true;
        Policy Hunter,Stalker;Hunter.Reset(3,5);Stalker.Reset(3,5);
        Hunter.Character.PlayStyle=static_cast<uint8_t>(Style::Hunter);Stalker.Character.PlayStyle=static_cast<uint8_t>(Style::Stalker);
        Hunter.Tick(Seen,0);Stalker.Tick(Seen,0);
        Check(Hunter.Weights[1]>Stalker.Weights[1],"Hunters press harder than stalkers at the same level");
    }
    // Carrying learning to the next floor: the clock restarts, habits and relief survive, map spots do not.
    {
        Pattern Learned;const Vec Corner{300,300,0},Origin{0,0,0},Away{900,0,0},Break{250,430,0};
        for(int I=0;I<40;++I)Learned.Observe(500+I*.2,.2f,true,false,true,.5f,&Corner,&Origin,&Origin);
        for(int I=40;I<80;++I)Learned.Observe(500+I*.2,.2f,false,true,false,.5f,&Away,&Break,&Origin);
        const auto Before=Learned.Read(516);
        Pattern Carried=Learned;Carried.Rebase(3-516.0); // New map: same instant is t=3.
        const auto After=Carried.Read(3);
        Check(std::abs(After.Defensive-Before.Defensive)<.02f&&std::abs(After.Watching-Before.Watching)<.02f&&
              std::abs(After.Confidence-Before.Confidence)<.05f,"Behaviour habits survive the floor change");
        Check(After.EscapeConfidence>.9f&&After.EscapeSide>.9f,"Escape habit survives the floor change");
        Check(Before.RestConfidence>.5f&&After.RestConfidence==0,"The old map's rest spots are dropped");
        Check(Carried.Observe(3.2,.2f,true,false,false,0),"Learning continues on the new clock");
        Check(Carried.Read(3+Pattern::HabitLifetime+61).EscapeConfidence==0,"Carried habits still expire");
        SharedSenses Team;
        Check(Team.SeedHabits(1,42,Carried)&&Team.FindHabits(1,42)!=nullptr,"Habits re-attach to the new pawn id");
        Check(Team.Find(1,42,3.5,8)==nullptr,"No stale position comes with the carried habits");
        Check(!Team.SeedHabits(1,42,Carried),"A target already being learned is not overwritten");
        Relief Streak;Streak.Add(500,3);const float Was=Streak.Value(510,Difficulty::For(1));
        Relief Moved=Streak;Moved.Rebase(3-510.0);
        Check(std::abs(Moved.Value(3,Difficulty::For(1))-Was)<.01f,"Relief carries to the next floor");
    }
    // Footsteps.
    Check(HearingRadius(500,false)>HearingRadius(250,false)&&HearingRadius(100,false)==0,"Running is loud, walking quiet, standing silent");
    Check(HearingRadius(500,true)<HearingRadius(500,false)&&HearingRadius(std::numeric_limits<float>::quiet_NaN(),false)==0,"Walls muffle footsteps; bad input is silent");
    // Bot-only survivor team: gaze lock is limited by level, so the mannequin always gets a window.
    {
        Context Gaze;Gaze.Playing=true;Gaze.Visible=true;Gaze.Mannequin=false;Gaze.HasAlly=true;Gaze.HumanAlly=false;Gaze.Range=1;
        const uint16_t Watch=1<<static_cast<int>(Action::Observe);
        Policy Novice;Novice.Reset(1,7);
        Check(Novice.Tick(Gaze,0,Watch).Tactic==Action::Observe,"Bot survivor watches the mannequin");
        Check(Novice.Tick(Gaze,1.6,Watch).Tactic!=Action::Observe,"Beginner bots look away quickly");
        Policy Expert;Expert.Reset(5,7);Expert.Tick(Gaze,0,Watch);
        Check(Expert.Tick(Gaze,1.6,Watch).Tactic==Action::Observe,"Masters hold the gaze longer");
        Check(Expert.Tick(Gaze,4.6,Watch).Tactic!=Action::Observe,"...but still look away eventually");
    }
    return Count;
}

#if defined(PROJECT_BOT_STANDALONE)
#include <iostream>
int main()
{
    int Failures=0;
    const int Count=RunAdaptiveBotChecks([&](bool Value,const char* Message){if(!Value){++Failures;std::cerr<<"FAIL: "<<Message<<'\n';}});
    std::cout<<"Adaptive bot policy: "<<Count<<" checks, "<<Failures<<" failures\n";return Failures?1:0;
}
#else
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectAdaptiveBotPolicyTest,"ProjectProject01.Bots.AdaptivePolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectAdaptiveBotPolicyTest::RunTest(const FString&)
{RunAdaptiveBotChecks([this](bool Value,const char* Message){TestTrue(UTF8_TO_TCHAR(Message),Value);});return true;}
#endif
#endif
