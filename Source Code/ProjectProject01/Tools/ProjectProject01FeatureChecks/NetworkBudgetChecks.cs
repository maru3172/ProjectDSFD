using System.Net;
using System.Net.Http.Headers;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

internal static class NetworkBudgetChecks
{
    internal static async Task RunAsync(Action<bool,string> check)
    {
        var builder=WebApplication.CreateBuilder();builder.Logging.ClearProviders();
        builder.Services.AddRateLimiter(options=>{options.RejectionStatusCode=429;NetworkRequestBudget.AddPolicies(options,new ConfigurationBuilder().Build());});
        await using var app=builder.Build();app.Urls.Add("http://127.0.0.1:0");app.UseRateLimiter();
        app.MapGet("/ticket",()=>"budget fixture, not authentication").RequireRateLimiting("game-ticket");
        app.MapGet("/auth",()=>"budget fixture, not authentication").RequireRateLimiting("auth");
        await app.StartAsync();
        using var client=new HttpClient{BaseAddress=new Uri(app.Urls.Single())};
        for(int i=0;i<20;i++)
        {
            using var response=await client.GetAsync("/auth");check(response.IsSuccessStatusCode,"C02 공유 IP 로그인 요청 예산 "+i);
        }
        client.DefaultRequestHeaders.Authorization=new AuthenticationHeaderValue("Bearer","fixture-a");
        for(int i=0;i<30;i++){using var response=await client.GetAsync("/ticket");if(!response.IsSuccessStatusCode)throw new Exception("Early ticket budget exhaustion");}
        using(var response=await client.GetAsync("/ticket"))check((int)response.StatusCode==429,"C02 한 세션의 티켓 호출 상한 유지");
        client.DefaultRequestHeaders.Authorization=new AuthenticationHeaderValue("Bearer","fixture-b");
        using(var response=await client.GetAsync("/ticket"))check(response.IsSuccessStatusCode,"C02 같은 IP의 다른 로그인 세션은 독립된 티켓 예산");
        await app.StopAsync();
    }
}
