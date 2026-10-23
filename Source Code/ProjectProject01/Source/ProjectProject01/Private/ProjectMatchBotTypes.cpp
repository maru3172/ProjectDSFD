#include "ProjectMatchBotTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

bool FProjectMatchBotSpec::Parse(const TArray<TSharedPtr<FJsonValue>>& Values,TArray<FProjectMatchBotSpec>& Out,FString& Error)
{
    if(Values.Num()>3){Error=TEXT("Too many server-owned bot participants.");return false;}
    TArray<FProjectMatchBotSpec> Parsed;TSet<FString> Ids;int32 Mannequins=0,Survivors=0;
    for(const auto& Value:Values)
    {
        const auto Json=Value.IsValid()&&Value->Type==EJson::Object?Value->AsObject():nullptr;FProjectMatchBotSpec Bot;double Level=0;FGuid Id;
        if(!Json||!Json->TryGetStringField(TEXT("botId"),Bot.BotId)||!FGuid::Parse(Bot.BotId,Id)||!Id.IsValid()||
            !Json->TryGetStringField(TEXT("displayName"),Bot.DisplayName)||Bot.DisplayName.TrimStartAndEnd().IsEmpty()||Bot.DisplayName.Len()>48||Bot.DisplayName.Contains(TEXT("\n"))||Bot.DisplayName.Contains(TEXT("\r"))||
            !Json->TryGetStringField(TEXT("assignedRole"),Bot.Role)||
            (Bot.Role!=TEXT("Mannequin")&&Bot.Role!=TEXT("Survivor"))||
            !Json->TryGetNumberField(TEXT("difficulty"),Level)||!FMath::IsFinite(Level)||Level<1||Level>5||Level!=static_cast<int32>(Level))
        {Error=TEXT("Invalid server-owned bot participant.");return false;}
        Bot.BotId=Id.ToString(EGuidFormats::DigitsWithHyphensLower);
        if(Ids.Contains(Bot.BotId)){Error=TEXT("Duplicate server-owned bot identity.");return false;}
        Ids.Add(Bot.BotId);Bot.Difficulty=static_cast<int32>(Level);
        Mannequins+=Bot.Role==TEXT("Mannequin");Survivors+=Bot.Role==TEXT("Survivor");
        if(Mannequins>1||Survivors>2){Error=TEXT("Invalid server-owned bot role allocation.");return false;}
        Parsed.Add(MoveTemp(Bot));
    }
    Out=MoveTemp(Parsed);Error.Reset();return true;
}
