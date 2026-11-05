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
    void Send(const FString& Verb,const FString& Path,const TSharedPtr<FJsonObject>& Body=nullptr,TFunction<void(const TSharedPtr<FJsonObject>&)> OnJson=nullptr);
    void ReadPresets(const TSharedPtr<FJsonObject>& Json);
    bool CanEdit()const;
    /** The draft as it will be sent: with the custom rules off, every playground switch is cleared. */
    FProjectCustomMatchRules Outgoing()const;
    bool CanPickAffix(const FString& Id)const;
    void ToggleAffix(const FString& Id);
    FString MixDetail()const;
    FProjectCustomMatchRules Draft;
    int32 Tab=0;          // 0 기본, 1 섞기, 2 자유 규칙, 3 시야·소리, 4 프리셋·코드. Every tab is built once; switching never rebuilds widgets.
    FString HoveredAffix; // Shown in the fixed detail line under the affix grid (no tooltips over other UI).
    FString HoveredPreset; // Recommended preset whose one-line summary is shown (fixed line, no tooltip).
    FString CodeLine;      // Rule code and its plain result summary (fixed two-line area).
    TSharedPtr<class SEditableTextBox> CodeInput;
    void ReadCodeAnswer(const TSharedPtr<FJsonObject>& Json,bool bLoadRules);
    FString RoomId,Status,SelectedName;
    bool bEditable=false;
    TArray<TSharedPtr<FString>> PresetNames;
    TMap<FString,FProjectCustomMatchRules> Presets;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> PresetCombo;
    TSharedPtr<class SEditableTextBox> NameInput;
    TSharedPtr<class IHttpRequest,ESPMode::ThreadSafe> Request;
    /** "적용하시겠습니까?" with 오늘 하루 다시 묻지 않기, before the rules are sent. */
    UPROPERTY(Transient) TObjectPtr<class UProjectConfirmDialogWidget> ApplyConfirm;
    void AskThen(const FString& Question,const FString& SkipKey,TFunction<void()> Run);
    TWeakObjectPtr<UUserWidget> ReturnWidget;
};
