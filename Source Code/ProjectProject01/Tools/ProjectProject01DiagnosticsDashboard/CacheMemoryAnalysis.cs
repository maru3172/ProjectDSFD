using System.Globalization;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed record CacheMemoryFinding(string Role,string Category,string Status,string Code,string Evidence,string Interpretation);

/// <summary>Offline, read-only hypotheses. Retained memory is never labelled a confirmed leak or pure cache.</summary>
internal static class CacheMemoryAnalysis
{
    internal static PerformanceSample ReadOptionalCounters(PerformanceSample sample,IReadOnlyDictionary<string,string> row)
    {
        bool Flag(string key)=>row.TryGetValue(key,out var v)&&bool.TryParse(v,out var b)&&b;
        double? MiB(string key)=>row.TryGetValue(key,out var v)&&double.TryParse(v,NumberStyles.Float,CultureInfo.InvariantCulture,out var n)&&double.IsFinite(n)&&n>=0?n/1048576d:null;
        var used=Flag("AllocatorStatsAvailable")?MiB("AllocatorUsedBytes"):null;
        var committed=Flag("AllocatorStatsAvailable")?MiB("AllocatorCommittedBytes"):null;
        bool valid=used.HasValue&&committed.HasValue&&committed>=used;
        var available=MiB("AvailablePhysicalBytes");var total=MiB("TotalPhysicalBytes");
        bool systemValid=available.HasValue&&total is >0&&available<=total;
        // Derive slack from the validated pair, never trust a negative/underflowing difference in an input file.
        return sample with
        {
            AllocatorUsedMiB=valid?used:null,AllocatorCommittedMiB=valid?committed:null,
            AllocatorRetainedMiB=valid?committed-used:null,
            AvailablePhysicalMiB=systemValid?available:null,TotalPhysicalMiB=systemValid?total:null,
            HasTextureCounters=Flag("TextureStatsAvailable")
            ,RuntimeCounters=RuntimeMetricAnalysis.Read(row)
        };
    }

    internal static List<CacheMemoryFinding> Analyze(IReadOnlyList<PerformanceSample> samples,IReadOnlyList<DiagnosticEvent> events)
    {
        List<CacheMemoryFinding> result=[];
        foreach(var group in samples.GroupBy(s=>(s.Role,s.NetMode)))
        {
            var rows=group.OrderBy(s=>s.GameSeconds).ToList();string role=group.Key.Role;
            var allocator=rows.Where(s=>s.AllocatorUsedMiB.HasValue&&s.AllocatorCommittedMiB.HasValue).ToList();
            if(allocator.Count==0)
                result.Add(new(role,"RAM 할당자","확인 불가","AllocatorUnavailable","지원되는 할당자 카운터가 없음 / 구형 기록","프로세스 RAM 전체를 캐시로 추정하지 않습니다. 새 캡처 또는 Memory Insights의 할당 추적이 필요합니다."));
            else
            {
                var last=allocator[^1];double peak=allocator.Max(s=>s.AllocatorRetainedMiB!.Value);
                bool Pressure(PerformanceSample s)=>s.AllocatorRetainedMiB>=256&&s.AllocatorRetainedMiB>=s.AllocatorCommittedMiB*.3&&
                    s.AvailablePhysicalMiB is >=0 && s.TotalPhysicalMiB is >0 && s.AvailablePhysicalMiB<=s.TotalPhysicalMiB &&
                    (s.AvailablePhysicalMiB<=512||s.AvailablePhysicalMiB/s.TotalPhysicalMiB<.1);
                bool sustained=Sustained(rows,Pressure);
                result.Add(new(role,"RAM 할당자",sustained?"검토 후보":"관측값",sustained?"RetainedMemoryPressure":"AllocatorAccounting",
                    $"최근 사용/OS 보유/차이={last.AllocatorUsedMiB:F1}/{last.AllocatorCommittedMiB:F1}/{last.AllocatorRetainedMiB:F1} MiB; 차이 최대={peak:F1} MiB",
                    sustained?"보유 공간이 큰 시점에 시스템 RAM 여유도 낮았습니다. 재사용 캐시·풀 여유·정렬·조각화를 구분할 추가 검사가 필요하며 강제 비우기를 권하지 않습니다.":
                    "차이에는 재사용 캐시·빈 풀·정렬·조각화가 포함될 수 있습니다. 많이 보유한다는 사실만으로 누수/오류가 아닙니다."));
                if(allocator.Count>=3&&allocator[^1].GameSeconds-allocator[0].GameSeconds>=30)
                {
                    var first=allocator[0];double usedGrowth=(last.AllocatorUsedMiB-first.AllocatorUsedMiB)!.Value;
                    double retainedGrowth=(last.AllocatorRetainedMiB-first.AllocatorRetainedMiB)!.Value;
                    if(retainedGrowth>=64&&Math.Abs(usedGrowth)<=16)
                        result.Add(new(role,"RAM 추세","참고","RetainedGrowthWithStableUsage",
                            $"{last.GameSeconds-first.GameSeconds:F1}초: 사용량 변화={usedGrowth:F1} MiB, 보유 차이 변화={retainedGrowth:F1} MiB",
                            "사용 중 할당은 비슷하고 보유 차이가 늘었습니다. 재사용/풀 확장 후보이며 누수로 단정하지 않습니다. 같은 상황 반복 및 GC 후 기준선을 비교하세요."));
                }
            }

            var texture=rows.Where(s=>s.TexturePoolUsePercent.HasValue).ToList();
            var high=texture.Where(s=>s.TexturePoolUsePercent>=95).ToList();
            bool highSustained=Sustained(rows,s=>s.TexturePoolUsePercent>=95);
            bool overSustained=Sustained(rows,s=>s.TexturePoolUsePercent>100);
            result.Add(texture.Count==0
                ?new(role,"텍스처 풀","확인 불가","TexturePoolUnavailable","RHI 카운터 없음 / 풀 제한 없음 / 구형 기록","0바이트 정상으로 간주하지 않습니다. 텍스처 풀은 전체 VRAM·드라이버 캐시와 다릅니다.")
                :new(role,"텍스처 풀",overSustained?"검토 후보":highSustained?"참고":"관측값",overSustained?"TexturePoolPressure":highSustained?"TexturePoolNearBudget":"TexturePoolAccounting",
                    $"스트리밍/설정 풀 비율 최대={texture.Max(s=>s.TexturePoolUsePercent):F1}%; 고사용 표본={high.Count}/{texture.Count}",
                    overSustained?"스트리밍 할당이 설정 풀을 초과하는 구간이 관측됐습니다. 텍스처 해상도·스트리밍 요구량과 엔진 풀 초과 로그를 함께 확인하세요. 이것만으로 끊김/누수를 확정하지 않습니다.":highSustained?
                    "설정 풀의 95% 이상을 사용합니다. 캐시 풀이 채워지는 것은 정상일 수 있어 이것만으로 문제 후보로 분류하지 않습니다.":
                    "이 비율은 스트리밍 텍스처와 설정 풀의 비교이며 전체 GPU 메모리 예산 측정이 아닙니다."));

            var gc=events.Where(e=>e.Role==role&&e.NetMode==group.Key.NetMode&&e.Code=="GarbageCollectionMemoryDelta")
                .OrderBy(e=>e.GameSeconds).Select(e=>(Event:e,Values:ParseFields(e.Message))).ToList();
            var baselines=gc.Where(e=>e.Values.GetValueOrDefault("AllocatorAfterAvailable")==1&&e.Values.ContainsKey("AllocatorUsedAfterBytes")&&e.Values.ContainsKey("PhysicalAfterBytes")).ToList();
            if(baselines.Count>=3&&baselines[^1].Event.GameSeconds-baselines[0].Event.GameSeconds>=30)
            {
                double liveDelta=(baselines[^1].Values["AllocatorUsedAfterBytes"]-baselines[0].Values["AllocatorUsedAfterBytes"])/1048576d;
                double physicalDelta=(baselines[^1].Values["PhysicalAfterBytes"]-baselines[0].Values["PhysicalAfterBytes"])/1048576d;
                result.Add(new(role,"GC 후 기준선",liveDelta>=64?"검토 후보":"참고",liveDelta>=64?"PostGCLiveGrowth":"PostGCBaseline",
                    $"GC {baselines.Count}회: GC 후 사용 중 할당 변화={liveDelta:F1} MiB, 물리 메모리 변화={physicalDelta:F1} MiB",
                    liveDelta>=64?"GC 후에도 사용 중 할당이 증가했습니다. 살아 있는 게임 상태·로드 자산·참조 유지·누수를 구분해야 합니다. 같은 조건을 반복해 확인하세요.":
                    "GC 후 프로세스 RAM이 그대로여도 할당자 재사용 때문에 정상일 수 있습니다. 이 결과는 누수가 없다는 보증이 아닙니다."));
            }
            else result.Add(new(role,"GC 후 기준선","자료 부족","PostGCInsufficient",
                $"GC 이벤트 {gc.Count}회 / 할당 정보 있는 GC {baselines.Count}회","할당 정보가 있는 GC 3회와 30초 이상의 관측이 있어야 기준선 추세를 비교합니다. 게임 중 GC를 강제하지 않습니다."));
        }
        if(samples.Count==0)result.Add(new("—","캐시·메모리","자료 없음","NoSamples","성능 표본 없음","캡처를 시작하고 같은 동작/맵 복귀를 여러 번 반복한 뒤 종료하세요."));
        return result;
    }

    private static Dictionary<string,double> ParseFields(string message)
    {
        Dictionary<string,double> values=new(StringComparer.Ordinal);
        foreach(var part in message.Split(';',StringSplitOptions.RemoveEmptyEntries))
        {
            var pair=part.Split('=',2);if(pair.Length==2&&double.TryParse(pair[1],NumberStyles.Float,CultureInfo.InvariantCulture,out var n)&&double.IsFinite(n)&&n>=0)values[pair[0].Trim()]=n;
        }
        return values;
    }

    private static bool Sustained(IEnumerable<PerformanceSample> samples,Func<PerformanceSample,bool> predicate)
    {
        int count=0;double start=0,previous=double.NegativeInfinity;
        foreach(var s in samples)
        {
            if(!double.IsFinite(s.GameSeconds)||!predicate(s)){count=0;previous=double.NegativeInfinity;continue;}
            if(s.GameSeconds-previous>2.5){count=0;start=s.GameSeconds;}
            previous=s.GameSeconds;++count;
            if(count>=3&&s.GameSeconds-start>=5)return true;
        }
        return false;
    }
}
