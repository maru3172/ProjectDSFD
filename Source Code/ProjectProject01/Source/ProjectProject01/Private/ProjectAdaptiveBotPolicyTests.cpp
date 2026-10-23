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
        Check(D.Reaction>=.35f&&D.Memory<=8.01f&&D.Adaptation<=.53f,"Hard bounded difficulty profile");
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
    Policy Baseline,Adaptive;Baseline.Reset(5,10);Adaptive.Reset(5,10);C.Playing=true;C.Mannequin=true;C.Visible=true;
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
