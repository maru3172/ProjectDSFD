#pragma once

#include "CoreMinimal.h"
#include "MannequinTrackingTypes.generated.h"

USTRUCT(BlueprintType)
struct FMannequinPlayerObservation
{
	GENERATED_BODY()
	
	// 플레이어를 마지막으로 직접 확인한 위치
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	FVector LastSeenLocation = FVector::ZeroVector;
	
	// 플레이어를 마지막으로 직접 확인했을 때의 속도
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	FVector LastSeenVelocity = FVector::ZeroVector;
	
	// 마지막 관측 정보를 이용해 계산한 플레이어 예상 위치
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	FVector PredictedLocation = FVector::ZeroVector;
	
	// 플레이어를 마지막으로 직접 확인한 월드 시각
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	double LastSeenTime = -1.0;
	
	// 현재 관측 정보의 신뢰도
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	float Confidence = 0.0f;
	
	// 유효한 플레이어 관측 정보를 가지고 있는지
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	bool bHasObservation = false;
	
	// 현재 플레이어를 직접 보고 있는지
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	bool bDirectlyVisible = false;
};