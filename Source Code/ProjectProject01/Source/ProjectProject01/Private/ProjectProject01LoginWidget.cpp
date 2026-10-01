// File: Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01LoginWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "ProjectProject01AuthSubsystem.h"

namespace ProjectProject01LoginUI
{
	UTextBlock* AddLabel(UWidgetTree* WidgetTree, UVerticalBox* Root, const FString& Text, const float FontSize)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Text));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		if (UVerticalBoxSlot* Slot = Root->AddChildToVerticalBox(Label))
		{
			Slot->SetPadding(FMargin(6.0f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Label;
	}

	UButton* AddButton(UWidgetTree* WidgetTree, UVerticalBox* Root, const FString& Text)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>();
		ButtonText->SetText(FText::FromString(Text));
		ButtonText->SetJustification(ETextJustify::Center);
		Button->AddChild(ButtonText);
		if (UVerticalBoxSlot* Slot = Root->AddChildToVerticalBox(Button))
		{
			Slot->SetPadding(FMargin(6.0f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Button;
	}
}

bool UProjectProject01LoginWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	return true;
}

void UProjectProject01LoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(LoginButton))
	{
		LoginButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleLoginClicked);
	}
	if (IsValid(RegisterButton))
	{
		RegisterButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleRegisterClicked);
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLoginCompleted.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleLoginResult);
			Auth->OnRegistrationCompleted.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleRegistrationResult);
		}
	}
}

void UProjectProject01LoginWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLoginCompleted.RemoveDynamic(this, &UProjectProject01LoginWidget::HandleLoginResult);
			Auth->OnRegistrationCompleted.RemoveDynamic(this, &UProjectProject01LoginWidget::HandleRegistrationResult);
		}
	}
	Super::NativeDestruct();
}

void UProjectProject01LoginWidget::HandleLoginClicked()
{
	if (!IsValid(AccountIdInput) || !IsValid(PasswordInput))
	{
		SetStatus(TEXT("로그인 입력 UI를 찾지 못했습니다."), true);
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>()
		: nullptr;
	if (!IsValid(Auth))
	{
		SetStatus(TEXT("인증 시스템을 찾지 못했습니다."), true);
		return;
	}

	SetRequestControlsEnabled(false);
	SetStatus(TEXT("로그인 중..."), false);
	Auth->Login(AccountIdInput->GetText().ToString(), PasswordInput->GetText().ToString());
}

void UProjectProject01LoginWidget::HandleRegisterClicked()
{
	if (!IsValid(AccountIdInput) || !IsValid(PasswordInput) || !IsValid(DisplayNameInput))
	{
		SetStatus(TEXT("회원가입 입력 UI를 찾지 못했습니다."), true);
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>()
		: nullptr;
	if (!IsValid(Auth))
	{
		SetStatus(TEXT("인증 시스템을 찾지 못했습니다."), true);
		return;
	}

	SetRequestControlsEnabled(false);
	SetStatus(TEXT("계정 생성 중..."), false);
	Auth->RegisterAccount(
		AccountIdInput->GetText().ToString(),
		PasswordInput->GetText().ToString(),
		DisplayNameInput->GetText().ToString());
}

void UProjectProject01LoginWidget::HandleLoginResult(
	const bool bSuccess,
	const FString& Message,
	const FString& DisplayName)
{
	SetRequestControlsEnabled(true);
	SetStatus(Message, !bSuccess);
	if (bSuccess)
	{
		EnterLobby();
	}
}

void UProjectProject01LoginWidget::HandleRegistrationResult(
	const bool bSuccess,
	const FString& Message,
	const FString& DisplayName)
{
	SetRequestControlsEnabled(true);
	SetStatus(Message, !bSuccess);
	if (bSuccess)
	{
		EnterLobby();
	}
}

void UProjectProject01LoginWidget::BuildWidgetTree()
{
	if (!IsValid(WidgetTree) || IsValid(WidgetTree->RootWidget))
	{
		return;
	}

	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Root;
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("ProjectProject01"), 30.0f);
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("로그인 / 회원가입"), 20.0f);

	AccountIdInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	AccountIdInput->SetHintText(FText::FromString(TEXT("계정 ID (영문/숫자/밑줄 3~32자)")));
	Root->AddChildToVerticalBox(AccountIdInput)->SetPadding(FMargin(6.0f));

	PasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	PasswordInput->SetHintText(FText::FromString(TEXT("비밀번호 (10~128자)")));
	PasswordInput->SetIsPassword(true);
	Root->AddChildToVerticalBox(PasswordInput)->SetPadding(FMargin(6.0f));

	DisplayNameInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	DisplayNameInput->SetHintText(FText::FromString(TEXT("표시 이름 (회원가입 시 2~32자)")));
	Root->AddChildToVerticalBox(DisplayNameInput)->SetPadding(FMargin(6.0f));

	LoginButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("로그인"));
	RegisterButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("회원가입"));
	StatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("인증 서버에 연결할 준비가 되었습니다."), 14.0f);
}

void UProjectProject01LoginWidget::SetRequestControlsEnabled(const bool bEnabled)
{
	if (IsValid(LoginButton))
	{
		LoginButton->SetIsEnabled(bEnabled);
	}
	if (IsValid(RegisterButton))
	{
		RegisterButton->SetIsEnabled(bEnabled);
	}
}

void UProjectProject01LoginWidget::SetStatus(const FString& Message, const bool bIsError)
{
	if (!IsValid(StatusText))
	{
		return;
	}
	StatusText->SetText(FText::FromString(Message));
	StatusText->SetColorAndOpacity(bIsError ? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::White));
}

void UProjectProject01LoginWidget::EnterLobby()
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LobbyLevel")));
}

bool UProjectProject01LobbyWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	return true;
}

void UProjectProject01LobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(LogoutButton))
	{
		LogoutButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLogoutClicked);
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLogoutCompleted.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLogoutResult);
			SetStatus(
				Auth->IsSignedIn()
					? FString::Printf(TEXT("%s 님이 로그인했습니다. 방 기능은 다음 단계에서 연결됩니다."), *Auth->GetSignedInDisplayName())
					: TEXT("로그인 세션이 없습니다. 로그인 화면으로 돌아가세요."),
				!Auth->IsSignedIn());
			return;
		}
	}
	SetStatus(TEXT("인증 시스템을 찾지 못했습니다."), true);
}

void UProjectProject01LobbyWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLogoutCompleted.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleLogoutResult);
		}
	}
	Super::NativeDestruct();
}

void UProjectProject01LobbyWidget::HandleLogoutClicked()
{
	if (IsValid(LogoutButton))
	{
		LogoutButton->SetIsEnabled(false);
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>()
		: nullptr;
	if (!IsValid(Auth))
	{
		SetStatus(TEXT("인증 시스템을 찾지 못했습니다."), true);
		if (IsValid(LogoutButton))
		{
			LogoutButton->SetIsEnabled(true);
		}
		return;
	}
	SetStatus(TEXT("로그아웃 중..."), false);
	Auth->Logout();
}

void UProjectProject01LobbyWidget::HandleLogoutResult(
	const bool bSuccess,
	const FString& Message,
	const FString& DisplayName)
{
	SetStatus(Message, !bSuccess);
	if (bSuccess)
	{
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LoginLevel")));
		return;
	}
	if (IsValid(LogoutButton))
	{
		LogoutButton->SetIsEnabled(true);
	}
}

void UProjectProject01LobbyWidget::BuildWidgetTree()
{
	if (!IsValid(WidgetTree) || IsValid(WidgetTree->RootWidget))
	{
		return;
	}

	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Root;
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("ProjectProject01 Lobby"), 30.0f);
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("로비 대기 화면"), 20.0f);
	LobbyStatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("세션 확인 중..."), 14.0f);
	LogoutButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("로그아웃 / 로그인 화면으로"));
}

void UProjectProject01LobbyWidget::SetStatus(const FString& Message, const bool bIsError)
{
	if (!IsValid(LobbyStatusText))
	{
		return;
	}
	LobbyStatusText->SetText(FText::FromString(Message));
	LobbyStatusText->SetColorAndOpacity(bIsError ? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::White));
}
