internal static class CaseMatchRewards
{
    internal sealed record Reward(int Xp,int Credits);
    internal static Reward Calculate(string mode,string role,bool won,int captures,int rescues,double firstCapture,double allCaptured,double escape,double difficulty)
    {
        static double Speed(double seconds)=>double.IsFinite(seconds)&&seconds>0?1/(1+seconds/180):0;
        double performance=role=="Mannequin"
            ?.55*Math.Clamp(captures/2.0,0,1)+(won?.25:0)+.1*Speed(firstCapture)+.1*Speed(allCaptured)
            :(won?.5:0)+.3*Math.Clamp(rescues/3.0,0,1)+(won?.2*Speed(escape):0);
        performance=Math.Clamp(performance,0,1);
        double score=performance;
        if(mode=="Bot")
        {
            double level=double.IsFinite(difficulty)?Math.Clamp(difficulty,1,5):1;
            double[] curve=[0,.12,.32,.60,1];int low=(int)Math.Floor(level)-1,high=Math.Min(4,low+1);
            double weight=curve[low]+(curve[high]-curve[low])*(level-Math.Floor(level));
            score=.8*weight+.2*performance;
        }
        return new Reward((int)Math.Round(60+180*score,MidpointRounding.AwayFromZero),(int)Math.Round(5+35*score,MidpointRounding.AwayFromZero));
    }
}
