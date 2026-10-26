#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProjectPartyAdmissionWidget.generated.h"

UCLASS()
class PROJECTPROJECT01_API UProjectPartyAdmissionWidget final:public UUserWidget
{
    GENERATED_BODY()
    friend class FProjectPartyAdmissionWidgetTest;
public:
    FString AdmissionId;
    void Open();
protected:
    virtual TSharedRef<SWidget> RebuildWidget()override;
    virtual void NativeTick(const FGeometry& Geometry,float Delta)override;
    virtual void NativeDestruct()override;
private:
    void Close();
    TWeakPtr<SWidget> PreviousFocus;
};
