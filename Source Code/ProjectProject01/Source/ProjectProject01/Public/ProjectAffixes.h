// File: Source/ProjectProject01/Public/ProjectAffixes.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Bot match affixes (Docs/멀티플레이_어픽스와_탐사_퀘스트_기획초안.md, sections 3-4). The backend picks the set for
// Master+/Master++ and the weekly mode; the game server only accepts a set it received with the match ticket.
namespace ProjectAffixes
{
    enum class EGroup:uint8 { Survivor, Mannequin, Environment };
    struct FInfo
    {
        const TCHAR* Id;const TCHAR* Name;const TCHAR* Summary;EGroup Group;
        const TCHAR* Glyph; // One character drawn on the icon badge.
        bool bPlayable;     // Implemented on the game server and its bots; only these may be picked.
    };
    inline TConstArrayView<FInfo> All()
    {
        static const FInfo Items[]={
            {TEXT("A01"),TEXT("연결 섬광"),TEXT("몸체를 바꾸면 새 몸체 관절에 섬광이 튑니다."),EGroup::Survivor,TEXT("섬"),true},
            {TEXT("A02"),TEXT("추격 과열"),TEXT("오래 달린 몸체는 관절이 달아오르다 잠시 둔해집니다."),EGroup::Survivor,TEXT("열"),true},
            {TEXT("A03"),TEXT("군집 간섭"),TEXT("뭉친 몸체 사이에 정전기가 튀고 둔해집니다."),EGroup::Survivor,TEXT("군"),true},
            {TEXT("A04"),TEXT("빈손의 반동"),TEXT("헛공격한 몸체가 휘청이며 회복 시간이 생깁니다."),EGroup::Survivor,TEXT("반"),true},
            {TEXT("A05"),TEXT("충격 방전"),TEXT("생존자를 잡은 몸체가 잠시 둔해집니다."),EGroup::Survivor,TEXT("방"),true},
            {TEXT("A06"),TEXT("남는 경직"),TEXT("오래 바라본 몸체는 시선을 뗀 뒤에도 잠깐 굳어 있습니다."),EGroup::Survivor,TEXT("경"),true},
            {TEXT("A07"),TEXT("깜빡이는 지령"),TEXT("명령을 받은 몸체의 눈이 깜빡입니다."),EGroup::Survivor,TEXT("령"),true},
            {TEXT("A08"),TEXT("눈빛"),TEXT("몸체가 감지하는 앞쪽 범위가 바닥에 빛줄기로 보입니다."),EGroup::Survivor,TEXT("눈"),true},
            {TEXT("A09"),TEXT("겹친 시선"),TEXT("둘이 함께 본 몸체는 1초 더 멈춰 있습니다."),EGroup::Survivor,TEXT("겹"),true},
            {TEXT("A10"),TEXT("숨 고르기"),TEXT("몸체를 붙잡고 멈춰 있으면 스태미나가 빠르게 회복됩니다."),EGroup::Survivor,TEXT("숨"),true},
            {TEXT("B01"),TEXT("과호흡"),TEXT("스태미나를 다 쓰면 지친 자세가 되고 회복이 늦어집니다."),EGroup::Mannequin,TEXT("호"),true},
            {TEXT("B02"),TEXT("일어나는 먼지"),TEXT("둘이 붙어 달리면 발밑에 먼지가 크게 입니다."),EGroup::Mannequin,TEXT("먼"),true},
            {TEXT("B03"),TEXT("고립 신호"),TEXT("동료와 오래 떨어지면 장치의 빨간 불이 깜빡입니다."),EGroup::Mannequin,TEXT("고"),true},
            {TEXT("B04"),TEXT("출구 경보"),TEXT("출구 가까이 가면 출구 표시가 깜빡입니다."),EGroup::Mannequin,TEXT("출"),true},
            {TEXT("B05"),TEXT("떨어지는 먼지"),TEXT("한자리에 오래 있으면 머리 위에서 먼지가 떨어집니다."),EGroup::Mannequin,TEXT("낙"),true},
            {TEXT("B06"),TEXT("잊힌 진열"),TEXT("오래 아무도 안 본 몸체는 칙칙해지고 빨라집니다."),EGroup::Mannequin,TEXT("잊"),true},
            {TEXT("B07"),TEXT("드러나는 시선"),TEXT("몸체를 오래 바라보는 생존자의 얼굴이 빛납니다."),EGroup::Mannequin,TEXT("빛"),true},
            {TEXT("B08"),TEXT("이어받기"),TEXT("한 몸체가 시선에 묶이면 근처 다른 몸체가 빨라집니다."),EGroup::Mannequin,TEXT("이"),true},
            {TEXT("B09"),TEXT("깨어나는 진열"),TEXT("서 있는 마네킹 옆을 달리면 그 마네킹이 깨어납니다."),EGroup::Mannequin,TEXT("깨"),true},
            {TEXT("B10"),TEXT("사냥의 열기"),TEXT("누가 잡히면 잠시 모든 몸체가 붉어지며 빨라집니다."),EGroup::Mannequin,TEXT("냥"),true},
            {TEXT("C01"),TEXT("깜짝 마주침"),TEXT("마주친 순간 먼저 상대를 본 쪽이 2초간 빨라집니다."),EGroup::Environment,TEXT("깜"),true},
            {TEXT("C02"),TEXT("탁한 공기"),TEXT("스태미나 회복이 느리고 직접 공격 간격도 깁니다."),EGroup::Environment,TEXT("탁"),true},
            {TEXT("C03"),TEXT("잔상"),TEXT("움직이는 모두가 잠깐 잔상을 남깁니다."),EGroup::Environment,TEXT("잔"),true},
            {TEXT("C04"),TEXT("교대 시간"),TEXT("주기적으로 몸체 전환과 달리기가 잠시 막힙니다."),EGroup::Environment,TEXT("교"),true},
            {TEXT("C05"),TEXT("러시아워"),TEXT("주기적으로 15초간 생존자와 몸체가 모두 빨라집니다."),EGroup::Environment,TEXT("러"),true},
            {TEXT("C06"),TEXT("마감 시간"),TEXT("마감 뒤 30초마다 몸체와 생존자 회복이 함께 빨라집니다."),EGroup::Environment,TEXT("마"),true},
            {TEXT("C07"),TEXT("공유되는 핑"),TEXT("생존자의 핑 지점이 마네킹에게도 보입니다."),EGroup::Environment,TEXT("핑"),true},
            {TEXT("C08"),TEXT("진동"),TEXT("주기적으로 잠깐 생존자와 몸체가 모두 느려집니다."),EGroup::Environment,TEXT("진"),true},
            {TEXT("C09"),TEXT("폭주하는 몸체"),TEXT("금 간 몸체 2개는 조종할 수 없지만 빠르게 돌아다닙니다."),EGroup::Environment,TEXT("폭"),true},
            {TEXT("C10"),TEXT("대피 방송"),TEXT("방송이 나오는 동안 발소리가 잘 안 들립니다."),EGroup::Environment,TEXT("송"),true},
        };
        return Items;
    }
    inline const FInfo* Find(const FString& Id){for(const auto& I:All())if(Id==I.Id)return &I;return nullptr;}
    inline FLinearColor GroupColor(EGroup G)
    {
        switch(G){case EGroup::Survivor:return FLinearColor(.25f,.62f,.86f);case EGroup::Mannequin:return FLinearColor(.86f,.32f,.28f);default:return FLinearColor(.82f,.66f,.26f);}
    }
    inline const TCHAR* GroupName(EGroup G){switch(G){case EGroup::Survivor:return TEXT("생존자 유리");case EGroup::Mannequin:return TEXT("마네킹 유리");default:return TEXT("환경");}}
}

enum class EProjectAffixMode:uint8 { None, MasterPlus, MasterPlusPlus, Weekly, Custom };

struct FProjectMatchAffixes
{
    EProjectAffixMode Mode=EProjectAffixMode::None;
    TArray<FString> Ids;
    FString WeekName; // Weekly mode only: the week's concept name from the server.

    static const TCHAR* ModeKey(EProjectAffixMode M)
    {switch(M){case EProjectAffixMode::MasterPlus:return TEXT("MasterPlus");case EProjectAffixMode::MasterPlusPlus:return TEXT("MasterPlusPlus");case EProjectAffixMode::Weekly:return TEXT("Weekly");case EProjectAffixMode::Custom:return TEXT("Custom");default:return TEXT("None");}}
    static bool ModeFromKey(const FString& Key,EProjectAffixMode& Out)
    {for(uint8 I=0;I<=uint8(EProjectAffixMode::Custom);++I)if(Key==ModeKey(EProjectAffixMode(I))){Out=EProjectAffixMode(I);return true;}return false;}
    FString ModeTitle()const
    {switch(Mode){case EProjectAffixMode::MasterPlus:return TEXT("마스터+");case EProjectAffixMode::MasterPlusPlus:return TEXT("마스터++");
        case EProjectAffixMode::Weekly:return WeekName.IsEmpty()?FString(TEXT("주간 어픽스")):FString::Printf(TEXT("주간 어픽스 · %s"),*WeekName);
        case EProjectAffixMode::Custom:return TEXT("사용자 지정 어픽스");default:return FString();}}
    bool IsActive()const{return Mode!=EProjectAffixMode::None&&!Ids.IsEmpty();}
    bool Has(const TCHAR* Id)const{return Ids.Contains(Id);}
    // Every id known and playable, no duplicates, and the counts each mode allows.
    bool IsValid()const
    {
        if(Mode==EProjectAffixMode::None)return Ids.IsEmpty()&&WeekName.IsEmpty();
        if(Ids.IsEmpty()||Ids.Num()>ProjectAffixes::All().Num()||WeekName.Len()>32)return false;
        TSet<FString> Seen;int32 Groups[3]{};
        for(const FString& Id:Ids){const auto* I=ProjectAffixes::Find(Id);if(!I||!I->bPlayable||Seen.Contains(Id))return false;Seen.Add(Id);++Groups[uint8(I->Group)];}
        switch(Mode)
        {
        case EProjectAffixMode::MasterPlus:return Ids.Num()==1&&Groups[2]==1;
        case EProjectAffixMode::MasterPlusPlus:return Ids.Num()==3&&Groups[0]==1&&Groups[1]==1&&Groups[2]==1;
        case EProjectAffixMode::Weekly:return Ids.Num()==3;
        default:return true;
        }
    }
    bool operator==(const FProjectMatchAffixes& R)const{return Mode==R.Mode&&Ids==R.Ids&&WeekName==R.WeekName;}
    static bool Parse(const TSharedPtr<FJsonObject>& Json,FProjectMatchAffixes& Out)
    {
        if(!Json)return false;FProjectMatchAffixes R;FString Key;const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if(!Json->TryGetStringField(TEXT("mode"),Key)||!ModeFromKey(Key,R.Mode))return false;
        if(Json->TryGetArrayField(TEXT("ids"),Values)&&Values)
            for(const auto& V:*Values){FString Id;if(!V.IsValid()||!V->TryGetString(Id))return false;R.Ids.Add(Id);}
        else if(R.Mode!=EProjectAffixMode::None)return false; // A room without affixes may send a null list.
        Json->TryGetStringField(TEXT("weekName"),R.WeekName);
        if(!R.IsValid())return false;Out=MoveTemp(R);return true;
    }
    TSharedRef<FJsonObject> ToJson()const
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("mode"),ModeKey(Mode));
        TArray<TSharedPtr<FJsonValue>> Values;for(const FString& Id:Ids)Values.Add(MakeShared<FJsonValueString>(Id));
        J->SetArrayField(TEXT("ids"),Values);J->SetStringField(TEXT("weekName"),WeekName);return J;
    }
};
