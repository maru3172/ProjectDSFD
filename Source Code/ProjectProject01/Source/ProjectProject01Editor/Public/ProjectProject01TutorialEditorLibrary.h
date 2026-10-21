#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectProject01TutorialEditorLibrary.generated.h"

UCLASS()
class PROJECTPROJECT01EDITOR_API UProjectProject01TutorialEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** NewBlankMap in commandlets does not initialize a navigation system itself. */
	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Tutorial Editor")
	static bool PrepareNavigation(UWorld* World);
	static struct FRequestPlaySessionParams MakePracticeRequest();
	static bool StartPractice(bool bMannequin,bool bFreePractice=false);
	static void RegisterMenus();
	static void ShutdownPractice();
};
