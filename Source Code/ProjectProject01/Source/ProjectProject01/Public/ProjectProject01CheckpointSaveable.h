#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ProjectProject01CheckpointSaveable.generated.h"

/**
 * 체크포인트 전체 상태 저장에 참여하는 액터용 인터페이스입니다.
 * 블루프린트 변수는 Details의 SaveGame 옵션을 켜면 자동 직렬화됩니다.
 */
UINTERFACE(BlueprintType)
class PROJECTPROJECT01_API UProjectProject01CheckpointSaveable : public UInterface
{
	GENERATED_BODY()
};

class PROJECTPROJECT01_API IProjectProject01CheckpointSaveable
{
	GENERATED_BODY()

public:
	/** 맵을 다시 열어도 동일하게 유지되는 ID입니다. 비워두면 클래스와 액터 이름을 사용합니다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="ProjectProject01|Checkpoint")
	FName GetCheckpointSaveId() const;

	/** 위치·컴포넌트·SaveGame 변수가 모두 복원된 뒤 한 번 호출됩니다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="ProjectProject01|Checkpoint")
	void OnCheckpointStateRestored();
};
