#pragma once
#include "CoreMinimal.h"

// No credentials are written to disk. A backend recovery worker drains the same journal
// after an ephemeral dedicated process exits. HTTP success is the only deletion authority.
struct FProjectResultOutbox
{
    static FString Directory();
    static bool IsProductionMatchId(const FString& MatchId);
    static bool Persist(const FString& Root,const FString& Backend,const FString& Route,const FString& Body,FString& OutPath);
    static void Submit(const FString& Backend,const FString& Route,const FString& Body,const FString& Secret,TFunction<void(bool)> Completion={});
    static bool IsTransient(int32 Status);
};
