#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProjectCustomMatchRules.h"
#include "ProjectCustomRoomRulesWidget.generated.h"
template<typename OptionType> class SComboBox;

UCLASS()
class PROJECTPROJECT01_API UProjectCustomRoomRulesWidget final:public UUserWidget
{
    GENERATED_BODY()
    friend class FProjectCustomRulesRenderTest;
public:
    static UProjectCustomRoomRulesWidget* OpenFor(class APlayerController* Player,UUserWidget* Return);
protected:
    virtual TSharedRef<SWidget> RebuildWidget()override;
    virtual void NativeConstruct()override;
    virtual void NativeDestruct()override;
    virtual void NativeTick(const FGeometry& Geometry,float Delta)override;
    virtual void ReleaseSlateResources(bool Children)override;
private:
    void Close();
    void Send(const FString& Verb,const FString& Path,const TSharedPtr<FJsonObject>& Body=nullptr);
    void ReadPresets(const TSharedPtr<FJsonObject>& Json);
    bool CanEdit()const;
    FProjectCustomMatchRules Draft;
    FString RoomId,Status,SelectedName;
    bool bEditable=false;
    TArray<TSharedPtr<FString>> PresetNames;
    TMap<FString,FProjectCustomMatchRules> Presets;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> PresetCombo;
    TSharedPtr<class SEditableTextBox> NameInput;
    TSharedPtr<class IHttpRequest,ESPMode::ThreadSafe> Request;
    TWeakObjectPtr<UUserWidget> ReturnWidget;
};
