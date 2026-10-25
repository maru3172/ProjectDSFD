#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectCaseInvestigationRegion.generated.h"

/** Place at each authored evidence location. A client cannot award discoveries through this volume. */
UCLASS(Blueprintable)
class PROJECTPROJECT01_API AProjectCaseInvestigationRegion : public AActor
{
    GENERATED_BODY()
public:
    AProjectCaseInvestigationRegion();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="사건 조사") TObjectPtr<class UBoxComponent> Region;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="사건 조사") FName RegionId;
    virtual void Tick(float DeltaSeconds)override;
protected:
    virtual void BeginPlay()override;
private:
    bool Submit(class APawn* Pawn);
    TSet<FString> Completed,Pending;
    TMap<FString,double> RetryAfter;
};
