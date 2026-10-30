#pragma once
#include "Editor.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

// Editor play-session tests: a PIE request with a map override (or a replay that loads its own map) leaves the
// source world loaded and initialized after PIE ends. Nothing cleans it up, so whichever later test collects garbage
// trips the engine's "subsystem destroyed while still initialized" ensure. Call once PIE has fully stopped.
namespace ProjectEditorTestWorlds
{
    inline void ReleaseLeftoverWorlds()
    {
        const UWorld* EditorWorld=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
        TArray<UWorld*> Leftovers;
        for(TObjectIterator<UWorld> It;It;++It)
            if(UWorld* World=*It;World!=EditorWorld&&World->bIsWorldInitialized&&!World->IsGameWorld()&&!GEngine->GetWorldContextFromWorld(World)&&
                (World->WorldType==EWorldType::Editor||World->WorldType==EWorldType::Inactive))
                Leftovers.Add(World);
        for(UWorld* World:Leftovers)World->DestroyWorld(false);
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    }
}
