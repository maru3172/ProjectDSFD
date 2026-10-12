// File: Source/ProjectProject01/Public/ProjectProject01RoleSpawnPoint.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "ProjectProject01RoleSpawnPoint.generated.h"

UENUM(BlueprintType)
enum class EProjectProject01SpawnRole : uint8
{
	Survivor UMETA(DisplayName = "생존자"),
	Mannequin UMETA(DisplayName = "마네킹")
};

/** MultiplayTest에서 서버가 역할별 무작위 시작 위치를 고를 때 사용하는 맵 배치용 지점입니다. */
UCLASS(Blueprintable)
class PROJECTPROJECT01_API AProjectProject01RoleSpawnPoint final : public APlayerStart
{
	GENERATED_BODY()

public:
	AProjectProject01RoleSpawnPoint(const FObjectInitializer& ObjectInitializer);

	EProjectProject01SpawnRole GetSpawnRole() const { return SpawnRole; }

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ProjectProject01|Spawn",
		meta = (AllowPrivateAccess = "true"))
	EProjectProject01SpawnRole SpawnRole = EProjectProject01SpawnRole::Survivor;
};
