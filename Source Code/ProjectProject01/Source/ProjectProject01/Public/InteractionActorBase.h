// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractionInterface.h"
#include "InteractionActorBase.generated.h"

class APawn;
class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS()
class PROJECTPROJECT01_API AInteractionActorBase : public AActor, public IInteractionInterface
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AInteractionActorBase();
	
	// 플레이어가 해당 액터를 바라볼 때 UI 에 표시할 상호작용 문구를 반환한다.
	// InteractionInterface에서 BlueprintNativeEvent로 선언했기 때문에 C++ 기본 구현 함수에는 "_Implementation"이 붙는 것이다.
	// 자식 BP 에서 이 함수를 재정의하면 Blueprint 구현이 우선적으로 호출되며, 재정의하지 않으면 이 클래스의 C++ 기본 구현이 사용된다.
	virtual FText GetInteractionText_Implementation(APawn* Interactor) const override;
	
	// 현재 대상과 상호작용할 수 있는 상태인지 확인
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	
	// 상호작용이 확정되었을 때 실제 동작을 확인한다.
	virtual void Interact_Implementation(APawn* Interactor) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Components")
	TObjectPtr<USceneComponent> SceneRoot;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Components")
	TObjectPtr<UStaticMeshComponent> ItemMesh;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Components")
	TObjectPtr<UBoxComponent> InteractionCollision;
	
	// 플레이어가 대상을 바라볼 때 표시할 문구
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FText InteractionText;
	
	// false 라면 Line Trace 에 맞더라도 상호작용하지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	bool bInteractionEnabled;
	
	// 아이템별 동작은 자식 BP 에서 구현할 것이다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Interaction", meta = (DisplayName = "On Interacted"))
	void BP_OnInteracted(APawn* Interactor);
	
};
