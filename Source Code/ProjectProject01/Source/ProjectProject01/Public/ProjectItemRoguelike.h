// File: Source/ProjectProject01/Public/ProjectItemRoguelike.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#pragma once
#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "ProjectAffixes.h"
#include "ProjectItemRoguelike.generated.h"

/** PvP match mode from the match ticket (Docs/ItemRoguelikeMode.md). Keys match Tools/ProjectProject01Backend/Game/MatchModes.cs. */
enum class EProjectMatchMode:uint8 { Normal, ItemRoguelike };

// "아이템 로그라이크": at the start and on every floor each player in play picks one of three cards, drawn from their own
// side's items and the buffs they may hand the other side (survivors give B-group affixes to the mannequin, the mannequin
// gives A-group affixes to the survivors). The game server rolls, judges and applies everything; picks last for the match.
namespace ProjectItemRoguelike
{
    enum class EKind:uint8 { SurvivorItem, MannequinItem, Buff };
    struct FItem
    {
        const TCHAR* Id;const TCHAR* Name;const TCHAR* Effect;const TCHAR* Tell;const TCHAR* Weak;EKind Kind;
        const TCHAR* Glyph; // One character drawn on the card badge.
    };
    inline TConstArrayView<FItem> Items()
    {
        static const FItem List[]={
            {TEXT("S01"),TEXT("낡은 운동화"),TEXT("달리기 스태미나 소모 -25%"),TEXT("달리는 발소리가 커집니다"),TEXT("달리는 소리가 30% 더 멀리 들립니다"),EKind::SurvivorItem,TEXT("화")},
            {TEXT("S02"),TEXT("진정제"),TEXT("몸체를 바라보며 멈춰 있으면 스태미나 회복 2배"),TEXT("하얀 입김이 보입니다"),TEXT("서 있을 때만 효과가 있습니다"),EKind::SurvivorItem,TEXT("진")},
            {TEXT("S03"),TEXT("방화복 조각"),TEXT("경기 중 첫 포획 1번을 막습니다"),TEXT("그을린 옷이 은은하게 빛나고, 막을 때 불꽃이 튑니다"),TEXT("1번만 막습니다. 최대 스태미나 -15"),EKind::SurvivorItem,TEXT("방")},
            {TEXT("S04"),TEXT("무전기"),TEXT("동료가 보고 있는 몸체가 내 화면에서 옅게 빛납니다"),TEXT("동료의 시선이 바뀌면 내 몸에 초록 표시등이 켜집니다"),TEXT("동료가 없으면 쓸모없습니다"),EKind::SurvivorItem,TEXT("무")},
            {TEXT("S05"),TEXT("깨진 손거울"),TEXT("등 뒤 8m에서 움직이는 몸체의 방향을 알려 줍니다"),TEXT("알릴 때 손거울에 빛이 맺힙니다"),TEXT("멈춰 있는 몸체는 잡아내지 못합니다"),EKind::SurvivorItem,TEXT("거")},
            {TEXT("S06"),TEXT("매장 안내도"),TEXT("층 시작 때 가장 가까운 출구의 방향을 알려 줍니다"),TEXT("그 출구가 마네킹에게 3초 동안 붉게 빛납니다"),TEXT("갈 출구가 들킵니다"),EKind::SurvivorItem,TEXT("도")},
            {TEXT("S07"),TEXT("접착 테이프"),TEXT("내가 시선을 뗀 몸체가 0.5초 더 굳어 있습니다"),TEXT("붙잡힌 몸체에 흰 잔광이 남습니다"),TEXT("같은 몸체를 3초 넘게 보면 내 얼굴이 빛납니다"),EKind::SurvivorItem,TEXT("테")},
            {TEXT("S08"),TEXT("방범 벨"),TEXT("데스카운트를 잃으면 6m 안의 몸체가 2초 동안 둔해집니다"),TEXT("벨이 울린 자리에 금빛이 크게 번집니다"),TEXT("내 위치가 모두에게 드러납니다"),EKind::SurvivorItem,TEXT("벨")},
            {TEXT("S09"),TEXT("두꺼운 양말"),TEXT("걸을 때 발소리가 거의 나지 않습니다"),TEXT("소리 없이 모습만 보입니다"),TEXT("달리기 속도 -8%"),EKind::SurvivorItem,TEXT("양")},
            {TEXT("S10"),TEXT("에너지 음료"),TEXT("층마다 스태미나가 처음 바닥나면 50을 즉시 회복합니다"),TEXT("마신 자리에 주황빛이 번집니다"),TEXT("층마다 1번입니다"),EKind::SurvivorItem,TEXT("음")},
            {TEXT("S11"),TEXT("젖은 수건"),TEXT("스태미나 회복 +30%"),TEXT("젖은 발자국이 3초 동안 남습니다"),TEXT("지나간 길이 드러납니다"),EKind::SurvivorItem,TEXT("수")},
            {TEXT("S12"),TEXT("분필"),TEXT("내 핑 자리에 분필 표시가 층이 끝날 때까지 남습니다"),TEXT("표시는 마네킹에게도 보입니다"),TEXT("길이 드러납니다. 층마다 6개까지"),EKind::SurvivorItem,TEXT("분")},
            {TEXT("S13"),TEXT("방독면"),TEXT("몸체 3m 안에서 스태미나 소모 -50%"),TEXT("몸체 가까이에서 방독면이 회색으로 빛납니다"),TEXT("바라보기 판정 각도 -10%"),EKind::SurvivorItem,TEXT("면")},
            {TEXT("S14"),TEXT("아드레날린 주사"),TEXT("동료가 잡히면 10초 동안 스태미나가 줄지 않습니다"),TEXT("붉은 숨이 보입니다"),TEXT("동료가 잡혀야 효과가 납니다"),EKind::SurvivorItem,TEXT("주")},
            {TEXT("S15"),TEXT("우산"),TEXT("바라보기 판정 각도 +10%"),TEXT("우산 끝의 푸른 빛이 보입니다"),TEXT("이동 속도 -5%"),EKind::SurvivorItem,TEXT("우")},
            {TEXT("M01"),TEXT("관절유"),TEXT("직접 조종하는 몸체 이동 +10%"),TEXT("조종하는 몸체의 발소리가 커집니다"),TEXT("조용히 다가갈 수 없습니다"),EKind::MannequinItem,TEXT("유")},
            {TEXT("M02"),TEXT("진열 감시"),TEXT("정지 몸체 정면 4m를 생존자가 지나가면 그 몸체를 알려 줍니다"),TEXT("그 몸체의 눈이 빛납니다"),TEXT("정지 몸체에만 적용됩니다"),EKind::MannequinItem,TEXT("감")},
            {TEXT("M03"),TEXT("바통"),TEXT("몸체를 바꾼 직후 새 몸체가 2초 동안 +15%"),TEXT("새 몸체의 관절에 섬광이 튑니다"),TEXT("다음 전환까지 8초를 기다려야 합니다"),EKind::MannequinItem,TEXT("통")},
            {TEXT("M04"),TEXT("긴 팔"),TEXT("직접 공격 사거리 +15%"),TEXT("공격하는 순간 팔 관절이 주황빛으로 빛납니다"),TEXT("공격 후 회복 시간 +0.5초"),EKind::MannequinItem,TEXT("팔")},
            {TEXT("M05"),TEXT("행렬"),TEXT("따라오기 명령 몸체 이동 +8%"),TEXT("따라오는 몸체 발밑에 먼지가 입니다"),TEXT("뭉쳐 다녀서 눈에 잘 띕니다"),EKind::MannequinItem,TEXT("행")},
            {TEXT("M06"),TEXT("귀 기울이기"),TEXT("정지 몸체 10m 안의 발소리가 마네킹에게 크게 들립니다"),TEXT("정지 몸체의 눈이 희미하게 빛납니다"),TEXT("정지 몸체의 위치가 드러납니다"),EKind::MannequinItem,TEXT("귀")},
            {TEXT("M07"),TEXT("녹슨 태엽"),TEXT("정지 몸체가 시선에서 풀린 직후 1초 동안 +15%"),TEXT("풀리는 순간 관절이 주황빛으로 빛납니다"),TEXT("정지 몸체만, 몸체마다 10초에 1번"),EKind::MannequinItem,TEXT("태")},
            {TEXT("M08"),TEXT("숨죽임"),TEXT("직접 조종 중 걸으면 발소리가 나지 않습니다"),TEXT("소리 없이 모습만 보입니다"),TEXT("직접 조종 중 이동 -10%"),EKind::MannequinItem,TEXT("숨")},
            {TEXT("M09"),TEXT("굶주림"),TEXT("60초 동안 아무도 못 잡으면 모든 몸체 +5%"),TEXT("관절이 점점 붉어집니다"),TEXT("잡으면 처음부터 다시 쌓입니다"),EKind::MannequinItem,TEXT("굶")},
            {TEXT("M10"),TEXT("포식"),TEXT("생존자를 잡은 몸체가 5초 동안 +15%"),TEXT("잡은 몸체에서 붉은 김이 납니다"),TEXT("그 5초 동안 명령을 내릴 수 없습니다"),EKind::MannequinItem,TEXT("포")},
            {TEXT("M11"),TEXT("유리 눈"),TEXT("생존자의 핑 위치가 마네킹에게 보입니다"),TEXT("층 시작 때 생존자에게 알려집니다"),TEXT("생존자가 핑 대신 음성을 씁니다"),EKind::MannequinItem,TEXT("눈")},
            {TEXT("M12"),TEXT("사냥 본능"),TEXT("달리는 생존자 10m 안의 몸체 +6%"),TEXT("그 몸체 관절이 붉게 빛납니다"),TEXT("걷는 생존자에게는 효과가 없습니다"),EKind::MannequinItem,TEXT("냥")},
            {TEXT("M13"),TEXT("냉기"),TEXT("몸체 3m 안에서 생존자의 스태미나가 회복되지 않습니다"),TEXT("몸체 발밑에 하얀 서리가 보입니다"),TEXT("모든 몸체 이동 -5%"),EKind::MannequinItem,TEXT("냉")},
            {TEXT("M14"),TEXT("마지막 진열"),TEXT("생존자가 1명만 남으면 모든 몸체 +6%"),TEXT("남은 생존자에게 알려집니다"),TEXT("둘 다 살아 있으면 효과가 없습니다"),EKind::MannequinItem,TEXT("막")},
            {TEXT("M15"),TEXT("출구 지기"),TEXT("생존자가 출구 10m 안에 들어오면 마네킹에게 알려 줍니다"),TEXT("그 출구가 붉게 빛나 생존자도 압니다"),TEXT("출구마다 30초에 1번"),EKind::MannequinItem,TEXT("출")},
        };
        return List;
    }
    inline const FItem* FindItem(const FString& Id){for(const auto& I:Items())if(Id==I.Id)return &I;return nullptr;}

    constexpr int32 OfferSize=3;
    constexpr double PickSeconds=20.0;
    constexpr int32 MaxFloors=8; // Picks per match the server accepts (the store has 3 floors).

    inline bool IsSurvivorRole(const FString& Role){return Role.Equals(TEXT("Survivor"),ESearchCase::IgnoreCase);}
    inline bool IsMannequinRole(const FString& Role){return Role.Equals(TEXT("Mannequin"),ESearchCase::IgnoreCase);}
    /** A buff card: an affix this side hands the other side (survivors give B, the mannequin gives A). */
    inline bool IsBuffFor(const FString& Id,bool bPickerIsSurvivor)
    {
        const auto* A=ProjectAffixes::Find(Id);
        return A&&A->bPlayable&&A->Group==(bPickerIsSurvivor?ProjectAffixes::EGroup::Mannequin:ProjectAffixes::EGroup::Survivor);
    }
    inline bool IsItemFor(const FString& Id,bool bPickerIsSurvivor)
    {const auto* I=FindItem(Id);return I&&I->Kind==(bPickerIsSurvivor?EKind::SurvivorItem:EKind::MannequinItem);}
    inline bool IsCardFor(const FString& Id,bool bPickerIsSurvivor){return IsItemFor(Id,bPickerIsSurvivor)||IsBuffFor(Id,bPickerIsSurvivor);}

    /** Name, one-line effect and the card's second line (tell and weak spot, or who receives the buff). */
    inline bool Describe(const FString& Id,FString& Name,FString& Effect,FString& Detail,bool& bBuff,FString& Glyph)
    {
        if(const auto* I=FindItem(Id)){Name=I->Name;Effect=I->Effect;Detail=FString::Printf(TEXT("신호: %s / 약점: %s"),I->Tell,I->Weak);bBuff=false;Glyph=I->Glyph;return true;}
        if(const auto* A=ProjectAffixes::Find(Id))
        {
            Name=A->Name;Effect=A->Summary;bBuff=true;Glyph=A->Glyph;
            Detail=A->Group==ProjectAffixes::EGroup::Mannequin?TEXT("마네킹에게 주는 버프"):TEXT("생존자 전원에게 주는 버프");return true;
        }
        return false;
    }

    /** Cards this player may still be offered: own side's items not yet owned, buffs not yet in play. */
    inline TArray<FString> Pool(bool bSurvivor,const TArray<FString>& Owned,const TArray<FString>& ActiveBuffs)
    {
        TArray<FString> Out;
        for(const auto& I:Items())if(I.Kind==(bSurvivor?EKind::SurvivorItem:EKind::MannequinItem)&&!Owned.Contains(I.Id))Out.Add(I.Id);
        for(const auto& A:ProjectAffixes::All())if(IsBuffFor(A.Id,bSurvivor)&&!ActiveBuffs.Contains(A.Id)&&!Owned.Contains(A.Id))Out.Add(A.Id);
        return Out;
    }
    /** Three distinct cards with at least one item and one buff while both kinds remain. Avoid excludes the last offer (reroll). */
    inline TArray<FString> Roll(bool bSurvivor,const TArray<FString>& Owned,const TArray<FString>& ActiveBuffs,FRandomStream& Rng,const TArray<FString>& Avoid={})
    {
        TArray<FString> All=Pool(bSurvivor,Owned,ActiveBuffs),Fresh;
        for(const FString& Id:All)if(!Avoid.Contains(Id))Fresh.Add(Id);
        if(Fresh.Num()>=OfferSize)All=MoveTemp(Fresh); // A reroll shows new cards while enough are left.
        TArray<FString> ItemsLeft,BuffsLeft;for(const FString& Id:All)(IsItemFor(Id,bSurvivor)?ItemsLeft:BuffsLeft).Add(Id);
        auto Take=[&Rng](TArray<FString>& From){const int32 I=Rng.RandRange(0,From.Num()-1);FString Id=From[I];From.RemoveAtSwap(I);return Id;};
        TArray<FString> Out;
        if(!ItemsLeft.IsEmpty())Out.Add(Take(ItemsLeft));
        if(!BuffsLeft.IsEmpty())Out.Add(Take(BuffsLeft));
        TArray<FString> Rest=ItemsLeft;Rest.Append(BuffsLeft);
        while(Out.Num()<OfferSize&&!Rest.IsEmpty())Out.Add(Take(Rest));
        for(int32 I=Out.Num()-1;I>0;--I)Out.Swap(I,Rng.RandRange(0,I)); // The item/buff order says nothing.
        return Out;
    }
}

struct FProjectMatchModeKeys
{
    static const TCHAR* Key(EProjectMatchMode M){return M==EProjectMatchMode::ItemRoguelike?TEXT("ItemRoguelike"):TEXT("Normal");}
    static bool FromKey(const FString& K,EProjectMatchMode& Out)
    {if(K==TEXT("Normal")){Out=EProjectMatchMode::Normal;return true;}if(K==TEXT("ItemRoguelike")){Out=EProjectMatchMode::ItemRoguelike;return true;}return false;}
    static const TCHAR* Title(EProjectMatchMode M){return M==EProjectMatchMode::ItemRoguelike?TEXT("아이템 로그라이크"):TEXT("일반");}
};

/** The cards one player is choosing from now (sent to that player only). */
USTRUCT()
struct FProjectRogueOffer
{
    GENERATED_BODY()
    UPROPERTY() int32 Serial=0;        // Changes with every new offer or reroll; stale clicks are ignored.
    UPROPERTY() int32 Floor=0;         // 0 = match start.
    UPROPERTY() TArray<FString> Cards;
    UPROPERTY() TArray<FString> Owned; // Earlier picks, shown under the cards.
    UPROPERTY() bool bRerollUsed=false;
    UPROPERTY() bool bPicked=false;
    UPROPERTY() FString PickedId;
    UPROPERTY() float SecondsLeft=0;
    UPROPERTY() int32 Waiting=0;       // Players still choosing.
};

/** One player's picks for the result screen (bAuto: the server chose when time ran out). */
USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FProjectRoguePickRecord
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Name;
    UPROPERTY(BlueprintReadOnly) FString Role;
    UPROPERTY(BlueprintReadOnly) TArray<FString> Ids;
    UPROPERTY(BlueprintReadOnly) TArray<bool> Auto;
};

/** The result screen's lines for one player's picks: one line per pick, or one short line (observer summary). */
inline FString ProjectRoguePickSummary(const FProjectRoguePickRecord& R,bool bOneLine)
{
    const bool bSurvivor=ProjectItemRoguelike::IsSurvivorRole(R.Role);
    TArray<FString> Parts;
    for(int32 I=0;I<R.Ids.Num();++I)
    {
        FString Name,Effect,Detail,Glyph;bool bBuff=false;
        if(!ProjectItemRoguelike::Describe(R.Ids[I],Name,Effect,Detail,bBuff,Glyph))continue;
        const bool bAuto=R.Auto.IsValidIndex(I)&&R.Auto[I];
        if(bOneLine){Parts.Add(Name+(bBuff?TEXT("(버프)"):TEXT(""))+(bAuto?TEXT("(자동)"):TEXT("")));continue;}
        const FString When=I==0?FString(TEXT("경기 시작")):FString::Printf(TEXT("%d번째 층"),I+1);
        const TCHAR* Kind=!bBuff?TEXT("내 아이템"):bSurvivor?TEXT("마네킹에게 준 버프"):TEXT("생존자에게 준 버프");
        Parts.Add(FString::Printf(TEXT("%s · %s — %s%s\n    %s"),*When,*Name,Kind,bAuto?TEXT(" · 시간 초과로 자동 선택"):TEXT(""),*Effect));
    }
    if(Parts.IsEmpty())return FString();
    return bOneLine?FString(TEXT("카드: "))+FString::Join(Parts,TEXT(", ")):FString(TEXT("선택한 카드 (이번 경기에서만 적용되었습니다)\n"))+FString::Join(Parts,TEXT("\n"));
}
