using System.Security.Cryptography;
using System.Text;
using System.Threading.RateLimiting;
using Microsoft.AspNetCore.RateLimiting;

internal static class NetworkRequestBudget
{
    // The bearer is never retained or logged. Only its digest is a partition key.
    // Login already has a database-backed, atomic per-account failure/lock policy.
    internal static string SessionPartition(HttpContext context)
    {
        string value=context.Request.Headers.Authorization.ToString();
        return value.StartsWith("Bearer ",StringComparison.OrdinalIgnoreCase)&&value.Length is >7 and <=600
            ?"session:"+Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(value)))
            :"anonymous:"+RequestSecurity.GetClientAddress(context);
    }
    internal static void AddPolicies(RateLimiterOptions options,IConfiguration configuration)
    {
        options.GlobalLimiter=PartitionedRateLimiter.Create<HttpContext,string>(context=>RateLimitPartition.GetFixedWindowLimiter(
            RequestSecurity.GetClientAddress(context),_=>Window(Math.Clamp(configuration.GetValue("Security:SharedIpRequestsPerMinute",6000),600,60000))));
        options.AddPolicy("auth",context=>RateLimitPartition.GetFixedWindowLimiter(RequestSecurity.GetClientAddress(context),_=>Window(600)));
        options.AddPolicy("lobby",context=>RateLimitPartition.GetFixedWindowLimiter(SessionPartition(context),_=>Window(600)));
        options.AddPolicy("game-ticket",context=>RateLimitPartition.GetFixedWindowLimiter(SessionPartition(context),_=>Window(30)));
    }
    private static FixedWindowRateLimiterOptions Window(int limit)=>new(){PermitLimit=limit,Window=TimeSpan.FromMinutes(1),QueueLimit=0,AutoReplenishment=true};
}
