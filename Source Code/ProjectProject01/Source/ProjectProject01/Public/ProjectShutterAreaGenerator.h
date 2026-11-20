// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectShutterAreaGenerator.generated.h"

class USceneComponent;

UCLASS()
class PROJECTPROJECT01_API AProjectShutterAreaGenerator : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AProjectShutterAreaGenerator();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shutter Generator", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;
};
