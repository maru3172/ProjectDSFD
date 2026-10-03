// File: Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01LoginWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformApplicationMisc.h"
#include "TimerManager.h"

namespace ProjectProject01LoginUI
{
	UTextBlock* AddLabel(UWidgetTree* WidgetTree, UVerticalBox* Root, const FString& Text, const float FontSize)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Text));
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor(0.82f, 0.82f, 0.82f, 1.0f));
		Background->SetPadding(FMargin(6.0f));
		Background->AddChild(Label);
		if (UVerticalBoxSlot* Slot = Root->AddChildToVerticalBox(Background))
		{
			Slot->SetPadding(FMargin(2.0f, 4.0f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Label;
	}

	UButton* AddButton(UWidgetTree* WidgetTree, UVerticalBox* Root, const FString& Text)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>();
		ButtonText->SetText(FText::FromString(Text));
		ButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		ButtonText->SetJustification(ETextJustify::Center);
		Button->AddChild(ButtonText);
		if (UVerticalBoxSlot* Slot = Root->AddChildToVerticalBox(Button))
		{
			Slot->SetPadding(FMargin(6.0f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Button;
	}

	void SetComboBoxTextBlack(UComboBoxString* ComboBox)
	{
		if (!IsValid(ComboBox))
		{
			return;
		}
		const FSlateColor Black(FLinearColor::Black);
		FComboBoxStyle ComboStyle = ComboBox->GetWidgetStyle();
		ComboStyle.ComboButtonStyle.ButtonStyle
			.SetNormalForeground(Black)
			.SetHoveredForeground(Black)
			.SetPressedForeground(Black)
			.SetDisabledForeground(Black);
		ComboBox->SetWidgetStyle(ComboStyle);
		FTableRowStyle ItemStyle = ComboBox->GetItemStyle();
		ItemStyle.SetTextColor(Black).SetSelectedTextColor(Black);
		ComboBox->SetItemStyle(ItemStyle);
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
	AccountIdInput->SetForegroundColor(FLinearColor::Black);
	Root->AddChildToVerticalBox(AccountIdInput)->SetPadding(FMargin(6.0f));

	PasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	PasswordInput->SetHintText(FText::FromString(TEXT("비밀번호 (10~128자)")));
	PasswordInput->SetForegroundColor(FLinearColor::Black);
	PasswordInput->SetIsPassword(true);
	Root->AddChildToVerticalBox(PasswordInput)->SetPadding(FMargin(6.0f));

	DisplayNameInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	DisplayNameInput->SetHintText(FText::FromString(TEXT("표시 이름 (회원가입 시 2~32자)")));
	DisplayNameInput->SetForegroundColor(FLinearColor::Black);
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
	StatusText->SetColorAndOpacity(bIsError ? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::Black));
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
	if (IsValid(CreateRoomButton)) CreateRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleCreateRoomClicked);
	if (IsValid(RefreshRoomsButton)) RefreshRoomsButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleRefreshRoomsClicked);
	if (IsValid(JoinRoomButton)) JoinRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleJoinRoomClicked);
	if (IsValid(ReadyButton)) ReadyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleReadyClicked);
	if (IsValid(StartRoomButton)) StartRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleStartRoomClicked);
	if (IsValid(LeaveRoomButton)) LeaveRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLeaveRoomClicked);
	if (IsValid(TransferHostButton)) TransferHostButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleTransferHostClicked);
	if (IsValid(DeleteRoomButton)) DeleteRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleDeleteRoomClicked);
	if (IsValid(CopyJoinCodeButton)) CopyJoinCodeButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleCopyJoinCodeClicked);
	if (IsValid(SendChatButton)) SendChatButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleSendChatClicked);
	if (IsValid(OpenLeaderboardTestButton)) OpenLeaderboardTestButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleOpenLeaderboardTestClicked);
	if (IsValid(LogoutButton))
	{
		LogoutButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLogoutClicked);
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLogoutCompleted.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLogoutResult);
			if (!Auth->IsSignedIn())
			{
				SetStatus(TEXT("로그인 세션이 없습니다. 로그인 화면으로 돌아가세요."), true);
				return;
			}
			SetStatus(FString::Printf(TEXT("%s 님이 로그인했습니다."), *Auth->GetSignedInDisplayName()), false);
		}
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->OnRequestCompleted.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLobbyRequestResult);
			Lobby->OnRoomListChanged.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleRoomListChanged);
			Lobby->OnCurrentRoomChanged.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleCurrentRoomChanged);
			Lobby->RefreshCurrentRoom();
		}
	}

	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().SetTimer(
			LobbyRefreshTimer, this, &UProjectProject01LobbyWidget::RefreshLobbyState, 1.0f, true, 1.0f);
	}
}

void UProjectProject01LobbyWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(LobbyRefreshTimer);
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLogoutCompleted.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleLogoutResult);
		}
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->OnRequestCompleted.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleLobbyRequestResult);
			Lobby->OnRoomListChanged.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleRoomListChanged);
			Lobby->OnCurrentRoomChanged.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleCurrentRoomChanged);
		}
	}
	Super::NativeDestruct();
}

void UProjectProject01LobbyWidget::HandleCreateRoomClicked()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || !IsValid(RoomNameInput) || !IsValid(PublicRoomCheckBox) || !IsValid(CreatePasswordInput))
	{
		SetStatus(TEXT("방 생성 UI 또는 로비 시스템을 찾지 못했습니다."), true);
		return;
	}
	Lobby->CreateRoom(RoomNameInput->GetText().ToString(), PublicRoomCheckBox->IsChecked(),
		CreatePasswordInput->GetText().ToString());
}

void UProjectProject01LobbyWidget::HandleRefreshRoomsClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->RefreshRooms();
		}
	}
}

void UProjectProject01LobbyWidget::HandleJoinRoomClicked()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || !IsValid(DirectRoomCodeInput) || !IsValid(JoinPasswordInput))
	{
		SetStatus(TEXT("방 참가 UI 또는 로비 시스템을 찾지 못했습니다."), true);
		return;
	}
	FString JoinCode = DirectRoomCodeInput->GetText().ToString().TrimStartAndEnd();
	if (JoinCode.IsEmpty() && IsValid(RoomListComboBox))
	{
		const FString Selected = RoomListComboBox->GetSelectedOption();
		if (const FString* SelectedJoinCode = JoinCodeByDisplayOption.Find(Selected))
		{
			JoinCode = *SelectedJoinCode;
		}
	}
	Lobby->JoinRoom(JoinCode, JoinPasswordInput->GetText().ToString());
}

void UProjectProject01LobbyWidget::HandleReadyClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>();
			IsValid(Lobby) && Lobby->HasCurrentRoom())
		{
			Lobby->SetReady(!Lobby->GetCurrentRoom().bIsReady);
		}
	}
}

void UProjectProject01LobbyWidget::HandleStartRoomClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->StartRoom();
		}
	}
}

void UProjectProject01LobbyWidget::HandleLeaveRoomClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->LeaveRoom();
		}
	}
}

void UProjectProject01LobbyWidget::HandleTransferHostClicked()
{
	if (!IsValid(HostTransferComboBox))
	{
		SetStatus(TEXT("방장 위임 UI를 찾지 못했습니다."), true);
		return;
	}
	const FString SelectedOption = HostTransferComboBox->GetSelectedOption();
	const FString* TargetUserId = UserIdByTransferOption.Find(SelectedOption);
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || TargetUserId == nullptr)
	{
		SetStatus(TEXT("방장을 넘길 플레이어를 선택하세요."), true);
		return;
	}
	Lobby->TransferHost(*TargetUserId);
}

void UProjectProject01LobbyWidget::HandleDeleteRoomClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->DeleteRoom();
		}
	}
}

void UProjectProject01LobbyWidget::HandleCopyJoinCodeClicked()
{
	UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || !Lobby->HasCurrentRoom() || Lobby->GetCurrentRoom().JoinCode.IsEmpty())
	{
		SetStatus(TEXT("복사할 참가 코드가 없습니다."), true);
		return;
	}
	FPlatformApplicationMisc::ClipboardCopy(*Lobby->GetCurrentRoom().JoinCode);
	SetStatus(FString::Printf(TEXT("참가 코드 %s를 복사했습니다."), *Lobby->GetCurrentRoom().JoinCode), false);
}

void UProjectProject01LobbyWidget::HandleSendChatClicked()
{
	if (!IsValid(ChatInput))
	{
		return;
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->SendChat(ChatInput->GetText().ToString());
		}
	}
}

void UProjectProject01LobbyWidget::HandleLogoutClicked()
{
	if (IsValid(LogoutButton))
	{
		LogoutButton->SetIsEnabled(false);
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (IsValid(Lobby) && Lobby->HasCurrentRoom())
	{
		bLogoutAfterLeave = true;
		SetStatus(TEXT("방에서 나온 뒤 로그아웃합니다..."), false);
		Lobby->LeaveRoom();
		return;
	}
	BeginLogout();
}

void UProjectProject01LobbyWidget::HandleOpenLeaderboardTestClicked()
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LeaderBoardTest")));
}

void UProjectProject01LobbyWidget::BeginLogout()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	if (!IsValid(Auth))
	{
		SetStatus(TEXT("인증 시스템을 찾지 못했습니다."), true);
		if (IsValid(LogoutButton)) LogoutButton->SetIsEnabled(true);
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

void UProjectProject01LobbyWidget::HandleLobbyRequestResult(
	const EProjectProject01LobbyOperation Operation,
	const bool bSuccess,
	const FString& Message)
{
	if (Operation != EProjectProject01LobbyOperation::RefreshRooms &&
		Operation != EProjectProject01LobbyOperation::RefreshCurrentRoom)
	{
		SetStatus(Message, !bSuccess);
	}
	if (bSuccess && Operation == EProjectProject01LobbyOperation::SendChat && IsValid(ChatInput))
	{
		ChatInput->SetText(FText::GetEmpty());
	}
	if (bSuccess && Operation == EProjectProject01LobbyOperation::RequestGameTicket)
	{
		UGameInstance* GameInstance = GetGameInstance();
		const UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
			? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
		if (IsValid(Lobby) && Lobby->HasCurrentRoom())
		{
			TravelToStartedGame(Lobby->GetCurrentRoom());
		}
	}
	if (Operation == EProjectProject01LobbyOperation::LeaveRoom)
	{
		if (bSuccess && bLogoutAfterLeave)
		{
			bLogoutAfterLeave = false;
			BeginLogout();
			return;
		}
		if (!bSuccess)
		{
			bLogoutAfterLeave = false;
			if (IsValid(LogoutButton)) LogoutButton->SetIsEnabled(true);
		}
	}
	if (bSuccess && (Operation == EProjectProject01LobbyOperation::CreateRoom ||
		Operation == EProjectProject01LobbyOperation::JoinRoom ||
		Operation == EProjectProject01LobbyOperation::LeaveRoom ||
		Operation == EProjectProject01LobbyOperation::TransferHost ||
		Operation == EProjectProject01LobbyOperation::DeleteRoom))
	{
		RefreshLobbyState();
	}
}

void UProjectProject01LobbyWidget::HandleRoomListChanged()
{
	RefreshRoomListView();
}

void UProjectProject01LobbyWidget::HandleCurrentRoomChanged()
{
	RefreshCurrentRoomView();
}

void UProjectProject01LobbyWidget::RefreshLobbyState()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || Lobby->IsRequestInFlight() || bTravelRequested)
	{
		return;
	}
	if (Lobby->HasCurrentRoom()) Lobby->RefreshCurrentRoom();
	else Lobby->RefreshRooms();
}

void UProjectProject01LobbyWidget::RefreshRoomListView()
{
	if (!IsValid(RoomListComboBox))
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby))
	{
		return;
	}
	const FString PreviousSelection = RoomListComboBox->GetSelectedOption();
	RoomListComboBox->ClearOptions();
	JoinCodeByDisplayOption.Reset();
	for (const FProjectProject01RoomSummary& Room : Lobby->GetRooms())
	{
		const FString LockText = Room.bHasPassword ? TEXT(" [비밀번호]") : FString();
		const FString Option = FString::Printf(TEXT("%s (%d/%d) - %s%s"),
			*Room.Name, Room.MemberCount, Room.MaxPlayers, *Room.HostDisplayName, *LockText);
		RoomListComboBox->AddOption(Option);
		JoinCodeByDisplayOption.Add(Option, Room.JoinCode);
	}
	if (JoinCodeByDisplayOption.Contains(PreviousSelection)) RoomListComboBox->SetSelectedOption(PreviousSelection);
	else if (RoomListComboBox->GetOptionCount() > 0) RoomListComboBox->SetSelectedIndex(0);
}

void UProjectProject01LobbyWidget::RefreshCurrentRoomView()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby))
	{
		return;
	}
	const bool bInRoom = Lobby->HasCurrentRoom();
	if (IsValid(CreateRoomButton)) CreateRoomButton->SetIsEnabled(!bInRoom);
	if (IsValid(JoinRoomButton)) JoinRoomButton->SetIsEnabled(!bInRoom);
	if (IsValid(ReadyButton)) ReadyButton->SetVisibility(bInRoom && !Lobby->GetCurrentRoom().bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(StartRoomButton)) StartRoomButton->SetVisibility(bInRoom && Lobby->GetCurrentRoom().bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(LeaveRoomButton)) LeaveRoomButton->SetVisibility(bInRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(CopyJoinCodeButton)) CopyJoinCodeButton->SetVisibility(bInRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	const bool bCanManageRoom = bInRoom && Lobby->GetCurrentRoom().bIsHost;
	if (IsValid(HostTransferComboBox)) HostTransferComboBox->SetVisibility(bCanManageRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(TransferHostButton)) TransferHostButton->SetVisibility(bCanManageRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(DeleteRoomButton)) DeleteRoomButton->SetVisibility(bCanManageRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(SendChatButton)) SendChatButton->SetIsEnabled(bInRoom);
	if (!bInRoom)
	{
		if (IsValid(CurrentRoomText)) CurrentRoomText->SetText(FText::FromString(TEXT("참가 중인 방 없음")));
		if (IsValid(MemberListText)) MemberListText->SetText(FText::GetEmpty());
		if (IsValid(ChatLogText)) ChatLogText->SetText(FText::GetEmpty());
		if (IsValid(HostTransferComboBox)) HostTransferComboBox->ClearOptions();
		UserIdByTransferOption.Reset();
		return;
	}

	const FProjectProject01RoomState& Room = Lobby->GetCurrentRoom();
	if (IsValid(CurrentRoomText))
	{
		CurrentRoomText->SetText(FText::FromString(FString::Printf(
			TEXT("방: %s\n참가 코드: %s\n인원: %d/3"), *Room.Name, *Room.JoinCode, Room.Members.Num())));
	}
	FString MembersText;
	for (const FProjectProject01RoomMember& Member : Room.Members)
	{
		const FString RoleText = Member.AssignedRole.IsEmpty()
			? FString()
			: FString::Printf(TEXT(" [%s]"), *Member.AssignedRole);
		MembersText += FString::Printf(TEXT("%s%s - %s%s\n"), *Member.DisplayName,
			Member.bIsHost ? TEXT(" [방장]") : TEXT(""),
			Member.bIsHost ? TEXT("시작 대기") : (Member.bIsReady ? TEXT("준비") : TEXT("대기")),
			*RoleText);
	}
	if (IsValid(MemberListText)) MemberListText->SetText(FText::FromString(MembersText));
	if (IsValid(HostTransferComboBox))
	{
		const FString PreviousSelection = HostTransferComboBox->GetSelectedOption();
		HostTransferComboBox->ClearOptions();
		UserIdByTransferOption.Reset();
		if (Room.bIsHost)
		{
			for (const FProjectProject01RoomMember& Member : Room.Members)
			{
				if (Member.bIsHost || Member.UserId.IsEmpty())
				{
					continue;
				}
				const FString Option = FString::Printf(TEXT("%s (%s)"), *Member.DisplayName, *Member.UserId);
				HostTransferComboBox->AddOption(Option);
				UserIdByTransferOption.Add(Option, Member.UserId);
			}
			if (UserIdByTransferOption.Contains(PreviousSelection))
			{
				HostTransferComboBox->SetSelectedOption(PreviousSelection);
			}
			else if (HostTransferComboBox->GetOptionCount() > 0)
			{
				HostTransferComboBox->SetSelectedIndex(0);
			}
		}
	}
	if (IsValid(TransferHostButton))
	{
		TransferHostButton->SetIsEnabled(Room.bIsHost && UserIdByTransferOption.Num() > 0);
	}
	if (IsValid(ReadyButton))
	{
		if (UTextBlock* ButtonText = Cast<UTextBlock>(ReadyButton->GetChildAt(0)))
		{
			ButtonText->SetText(FText::FromString(Room.bIsReady ? TEXT("준비 취소") : TEXT("준비")));
		}
	}
	if (IsValid(StartRoomButton)) StartRoomButton->SetIsEnabled(Room.bCanStart);

	FString ChatText;
	for (const FProjectProject01RoomChatMessage& Chat : Room.ChatMessages)
	{
		ChatText += FString::Printf(TEXT("%s: %s\n"), *Chat.DisplayName, *Chat.Message);
	}
	if (IsValid(ChatLogText)) ChatLogText->SetText(FText::FromString(ChatText));
	if (Room.bStarted)
	{
		if (Lobby->HasPendingGameJoinTicket())
		{
			TravelToStartedGame(Room);
		}
		else if (!Lobby->IsRequestInFlight())
		{
			SetStatus(TEXT("일회용 게임 접속 티켓을 발급받는 중입니다..."), false);
			Lobby->RequestGameJoinTicket();
		}
	}
}

void UProjectProject01LobbyWidget::TravelToStartedGame(const FProjectProject01RoomState& RoomState)
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (bTravelRequested || !IsValid(Lobby) || !Lobby->HasPendingGameJoinTicket())
	{
		if (!bTravelRequested && RoomState.bStarted)
		{
			SetStatus(TEXT("검증된 게임 접속 티켓 또는 AES-GCM 키가 없습니다."), true);
		}
		return;
	}
	APlayerController* PlayerController = GetOwningPlayer();
	if (!IsValid(PlayerController))
	{
		SetStatus(TEXT("게임 서버로 이동할 PlayerController를 찾지 못했습니다."), true);
		return;
	}
	bTravelRequested = true;
	const FString TravelUrl = FString::Printf(TEXT("%s?EncryptionToken=%s?GameTicket=%s?LobbyRoomId=%s?MatchId=%s"),
		*Lobby->GetGameTicketTravelUrl(), *Lobby->GetGameJoinTicket(), *Lobby->GetGameJoinTicket(),
		*Lobby->GetGameTicketRoomId(), *Lobby->GetGameTicketMatchId());
	SetStatus(FString::Printf(TEXT("검증된 MultiplayTest 서버로 이동합니다. 역할: %s"),
		*Lobby->GetGameTicketRole()), false);
	PlayerController->ClientTravel(TravelUrl, TRAVEL_Absolute);
}

void UProjectProject01LobbyWidget::BuildWidgetTree()
{
	if (!IsValid(WidgetTree) || IsValid(WidgetTree->RootWidget))
	{
		return;
	}

	UScrollBox* ScrollRoot = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	ScrollRoot->AddChild(Root);
	WidgetTree->RootWidget = ScrollRoot;
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("ProjectProject01 Lobby"), 30.0f);
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("방 만들기 / 참가"), 20.0f);
	LobbyStatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("세션 확인 중..."), 14.0f);

	RoomNameInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	RoomNameInput->SetHintText(FText::FromString(TEXT("방 이름 (1~48자)")));
	RoomNameInput->SetForegroundColor(FLinearColor::Black);
	Root->AddChildToVerticalBox(RoomNameInput)->SetPadding(FMargin(6.0f));
	PublicRoomCheckBox = WidgetTree->ConstructWidget<UCheckBox>();
	PublicRoomCheckBox->SetIsChecked(true);
	UBorder* PublicModeBackground = WidgetTree->ConstructWidget<UBorder>();
	PublicModeBackground->SetBrushColor(FLinearColor(0.82f, 0.82f, 0.82f, 1.0f));
	PublicModeBackground->SetPadding(FMargin(8.0f));
	UHorizontalBox* PublicModeRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	PublicModeRow->AddChild(PublicRoomCheckBox);
	UTextBlock* PublicLabel = WidgetTree->ConstructWidget<UTextBlock>();
	PublicLabel->SetText(FText::FromString(TEXT("공개방: 체크 / 비공개방: 체크 해제 (참가 코드를 공유해 입장)")));
	PublicLabel->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	PublicModeRow->AddChild(PublicLabel);
	PublicModeBackground->AddChild(PublicModeRow);
	Root->AddChildToVerticalBox(PublicModeBackground)->SetPadding(FMargin(6.0f));
	CreatePasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	CreatePasswordInput->SetHintText(FText::FromString(TEXT("방 비밀번호 (선택, 사용 시 4~64자)")));
	CreatePasswordInput->SetForegroundColor(FLinearColor::Black);
	CreatePasswordInput->SetIsPassword(true);
	Root->AddChildToVerticalBox(CreatePasswordInput)->SetPadding(FMargin(6.0f));
	CreateRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("방 만들기"));
	RefreshRoomsButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("공개방 목록 새로고침"));

	RoomListComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
	ProjectProject01LoginUI::SetComboBoxTextBlack(RoomListComboBox);
	Root->AddChildToVerticalBox(RoomListComboBox)->SetPadding(FMargin(6.0f));
	DirectRoomCodeInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	DirectRoomCodeInput->SetHintText(FText::FromString(TEXT("6~8자리 참가 코드 (비공개방 또는 직접 참가)")));
	DirectRoomCodeInput->SetForegroundColor(FLinearColor::Black);
	Root->AddChildToVerticalBox(DirectRoomCodeInput)->SetPadding(FMargin(6.0f));
	JoinPasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	JoinPasswordInput->SetHintText(FText::FromString(TEXT("참가 비밀번호 (비밀번호방만)")));
	JoinPasswordInput->SetForegroundColor(FLinearColor::Black);
	JoinPasswordInput->SetIsPassword(true);
	Root->AddChildToVerticalBox(JoinPasswordInput)->SetPadding(FMargin(6.0f));
	JoinRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("선택/ID 방 참가"));

	CurrentRoomText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("참가 중인 방 없음"), 16.0f);
	CopyJoinCodeButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("참가 코드 복사"));
	MemberListText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT(""), 14.0f);
	ReadyButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("준비"));
	StartRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("게임 시작"));
	LeaveRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("방 나가기"));
	HostTransferComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
	ProjectProject01LoginUI::SetComboBoxTextBlack(HostTransferComboBox);
	Root->AddChildToVerticalBox(HostTransferComboBox)->SetPadding(FMargin(6.0f));
	TransferHostButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("선택한 플레이어에게 방장 넘기기"));
	DeleteRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("방 삭제 (모두 로비로)"));
	ReadyButton->SetVisibility(ESlateVisibility::Collapsed);
	StartRoomButton->SetVisibility(ESlateVisibility::Collapsed);
	LeaveRoomButton->SetVisibility(ESlateVisibility::Collapsed);
	CopyJoinCodeButton->SetVisibility(ESlateVisibility::Collapsed);
	HostTransferComboBox->SetVisibility(ESlateVisibility::Collapsed);
	TransferHostButton->SetVisibility(ESlateVisibility::Collapsed);
	DeleteRoomButton->SetVisibility(ESlateVisibility::Collapsed);

	ChatLogText = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>();
	ChatLogText->SetIsReadOnly(true);
	ChatLogText->SetHintText(FText::FromString(TEXT("방 채팅")));
	ChatLogText->SetForegroundColor(FLinearColor::Black);
	Root->AddChildToVerticalBox(ChatLogText)->SetPadding(FMargin(6.0f));
	ChatInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	ChatInput->SetHintText(FText::FromString(TEXT("채팅 입력 (최대 300자)")));
	ChatInput->SetForegroundColor(FLinearColor::Black);
	Root->AddChildToVerticalBox(ChatInput)->SetPadding(FMargin(6.0f));
	SendChatButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("채팅 보내기"));
	SendChatButton->SetIsEnabled(false);
	OpenLeaderboardTestButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("리더보드 테스트 열기"));
	LogoutButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("로그아웃 / 로그인 화면으로"));
}

void UProjectProject01LobbyWidget::SetStatus(const FString& Message, const bool bIsError)
{
	if (!IsValid(LobbyStatusText))
	{
		return;
	}
	LobbyStatusText->SetText(FText::FromString(Message));
	LobbyStatusText->SetColorAndOpacity(bIsError ? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::Black));
}

bool UProjectProject01LeaderboardWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	return true;
}

void UProjectProject01LeaderboardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(RefreshButton)) RefreshButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleRefreshClicked);
	if (IsValid(RoleComboBox)) RoleComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleRoleSelectionChanged);
	if (IsValid(SortComboBox)) SortComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleSortSelectionChanged);
	if (IsValid(SubmitRecordButton)) SubmitRecordButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleSubmitRecordClicked);
	if (IsValid(ReturnToLobbyButton)) ReturnToLobbyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleReturnToLobbyClicked);

	PendingTestMatchId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	RefreshSortOptions();

	UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Auth) || !Auth->IsSignedIn() || !IsValid(Lobby))
	{
		SetStatus(TEXT("로그인 세션 또는 리더보드 시스템이 없습니다."), true);
		return;
	}

	Lobby->OnRequestCompleted.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleLeaderboardRequestResult);
	Lobby->OnLeaderboardChanged.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleLeaderboardChanged);
	Lobby->RefreshLeaderboard(GetSelectedRole(), GetSelectedSort());
}

void UProjectProject01LeaderboardWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->OnRequestCompleted.RemoveDynamic(this, &UProjectProject01LeaderboardWidget::HandleLeaderboardRequestResult);
			Lobby->OnLeaderboardChanged.RemoveDynamic(this, &UProjectProject01LeaderboardWidget::HandleLeaderboardChanged);
		}
	}
	Super::NativeDestruct();
}

void UProjectProject01LeaderboardWidget::HandleRefreshClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->RefreshLeaderboard(GetSelectedRole(), GetSelectedSort());
		}
	}
}

void UProjectProject01LeaderboardWidget::HandleRoleSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	(void)SelectedItem;
	(void)SelectionType;
	RefreshSortOptions();
	HandleRefreshClicked();
}

void UProjectProject01LeaderboardWidget::HandleSortSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	(void)SelectedItem;
	(void)SelectionType;
	if (bUpdatingSortOptions)
	{
		return;
	}
	HandleRefreshClicked();
}

void UProjectProject01LeaderboardWidget::HandleSubmitRecordClicked()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby))
	{
		SetStatus(TEXT("리더보드 시스템을 찾지 못했습니다."), true);
		return;
	}

	const int32 CaptureCount = IsValid(CaptureCountInput) ? FCString::Atoi(*CaptureCountInput->GetText().ToString()) : 0;
	const double FirstCaptureSeconds = IsValid(FirstCaptureSecondsInput) ? FCString::Atod(*FirstCaptureSecondsInput->GetText().ToString()) : 0.0;
	const double AllCapturedSeconds = IsValid(AllCapturedSecondsInput) ? FCString::Atod(*AllCapturedSecondsInput->GetText().ToString()) : 0.0;
	const int32 RescueCount = IsValid(RescueCountInput) ? FCString::Atoi(*RescueCountInput->GetText().ToString()) : 0;
	const double EscapeSeconds = IsValid(EscapeSecondsInput) ? FCString::Atod(*EscapeSecondsInput->GetText().ToString()) : 0.0;

	SetStatus(TEXT("테스트 성공 기록을 등록하는 중..."), false);
	Lobby->SubmitLeaderboardRecord(PendingTestMatchId, GetSelectedRole(), true,
		CaptureCount, FirstCaptureSeconds, AllCapturedSeconds, RescueCount, EscapeSeconds);
}

void UProjectProject01LeaderboardWidget::HandleReturnToLobbyClicked()
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LobbyLevel")));
}

void UProjectProject01LeaderboardWidget::HandleLeaderboardChanged()
{
	RefreshLeaderboardView();
}

void UProjectProject01LeaderboardWidget::HandleLeaderboardRequestResult(
	const EProjectProject01LobbyOperation Operation,
	const bool bSuccess,
	const FString& Message)
{
	if (Operation != EProjectProject01LobbyOperation::RefreshLeaderboard &&
		Operation != EProjectProject01LobbyOperation::SubmitLeaderboardRecord)
	{
		return;
	}
	SetStatus(Message, !bSuccess);
	if (bSuccess && Operation == EProjectProject01LobbyOperation::SubmitLeaderboardRecord)
	{
		PendingTestMatchId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
			{
				Lobby->RefreshLeaderboard(GetSelectedRole(), GetSelectedSort());
			}
		}
	}
}

void UProjectProject01LeaderboardWidget::RefreshLeaderboardView()
{
	if (!IsValid(LeaderboardText))
	{
		return;
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby))
	{
		return;
	}

	FString Text = GetSelectedRole() == EProjectProject01LeaderboardRole::Mannequin
		? TEXT("순위 | 이름 | 역할 | 종합점수 | 포획 | 첫 포획(초) | 전원 포획(초)\n")
		: TEXT("순위 | 이름 | 역할 | 종합점수 | 구출 | 탈출(초)\n");
	for (const FProjectProject01LeaderboardEntry& Entry : Lobby->GetLeaderboardEntries())
	{
		if (GetSelectedRole() == EProjectProject01LeaderboardRole::Mannequin)
		{
			Text += FString::Printf(TEXT("%d | %s | %s | %.1f | %d | %.2f | %.2f\n"),
				Entry.Rank, *Entry.DisplayName, *Entry.Role, Entry.OverallScore, Entry.CaptureCount,
				Entry.FirstCaptureSeconds, Entry.AllCapturedSeconds);
		}
		else
		{
			Text += FString::Printf(TEXT("%d | %s | %s | %.1f | %d | %.2f\n"),
				Entry.Rank, *Entry.DisplayName, *Entry.Role, Entry.OverallScore,
				Entry.RescueCount, Entry.EscapeSeconds);
		}
	}
	if (Lobby->GetLeaderboardEntries().IsEmpty())
	{
		Text += TEXT("등록된 성공 기록이 없습니다.\n");
	}
	LeaderboardText->SetText(FText::FromString(Text));
}

void UProjectProject01LeaderboardWidget::RefreshSortOptions()
{
	if (!IsValid(SortComboBox))
	{
		return;
	}
	bUpdatingSortOptions = true;
	SortComboBox->ClearOptions();
	SortComboBox->AddOption(TEXT("종합 점수"));
	const bool bMannequin = GetSelectedRole() == EProjectProject01LeaderboardRole::Mannequin;
	if (bMannequin)
	{
		SortComboBox->AddOption(TEXT("붙잡은 횟수"));
		SortComboBox->AddOption(TEXT("첫 포획 시간"));
		SortComboBox->AddOption(TEXT("전원 포획 시간"));
	}
	else
	{
		SortComboBox->AddOption(TEXT("구출 횟수"));
		SortComboBox->AddOption(TEXT("탈출 시간"));
	}
	SortComboBox->SetSelectedOption(TEXT("종합 점수"));
	if (IsValid(CaptureCountInput)) CaptureCountInput->SetVisibility(bMannequin ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(FirstCaptureSecondsInput)) FirstCaptureSecondsInput->SetVisibility(bMannequin ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(AllCapturedSecondsInput)) AllCapturedSecondsInput->SetVisibility(bMannequin ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(RescueCountInput)) RescueCountInput->SetVisibility(bMannequin ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (IsValid(EscapeSecondsInput)) EscapeSecondsInput->SetVisibility(bMannequin ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	bUpdatingSortOptions = false;
}

EProjectProject01LeaderboardRole UProjectProject01LeaderboardWidget::GetSelectedRole() const
{
	return IsValid(RoleComboBox) && RoleComboBox->GetSelectedOption() == TEXT("생존자")
		? EProjectProject01LeaderboardRole::Survivor : EProjectProject01LeaderboardRole::Mannequin;
}

EProjectProject01LeaderboardSort UProjectProject01LeaderboardWidget::GetSelectedSort() const
{
	const FString Option = IsValid(SortComboBox) ? SortComboBox->GetSelectedOption() : FString();
	if (Option == TEXT("붙잡은 횟수")) return EProjectProject01LeaderboardSort::Captures;
	if (Option == TEXT("첫 포획 시간")) return EProjectProject01LeaderboardSort::FirstCapture;
	if (Option == TEXT("전원 포획 시간")) return EProjectProject01LeaderboardSort::AllCaptured;
	if (Option == TEXT("구출 횟수")) return EProjectProject01LeaderboardSort::Rescues;
	if (Option == TEXT("탈출 시간")) return EProjectProject01LeaderboardSort::Escape;
	return EProjectProject01LeaderboardSort::Overall;
}

void UProjectProject01LeaderboardWidget::BuildWidgetTree()
{
	UScrollBox* ScrollRoot = WidgetTree->ConstructWidget<UScrollBox>();
	WidgetTree->RootWidget = ScrollRoot;
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	ScrollRoot->AddChild(Root);

	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("ProjectProject01 LeaderBoardTest"), 24.0f);
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root,
		TEXT("성공한 경기만 등록하는 리더보드 테스트입니다. 미지정 정렬은 역할별 평균 순위 기반 종합점수입니다."), 14.0f);
	LeaderboardStatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("리더보드를 불러오는 중..."), 14.0f);

	RoleComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
	ProjectProject01LoginUI::SetComboBoxTextBlack(RoleComboBox);
	RoleComboBox->AddOption(TEXT("마네킹"));
	RoleComboBox->AddOption(TEXT("생존자"));
	RoleComboBox->SetSelectedOption(TEXT("마네킹"));
	Root->AddChildToVerticalBox(RoleComboBox)->SetPadding(FMargin(6.0f));

	SortComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
	ProjectProject01LoginUI::SetComboBoxTextBlack(SortComboBox);
	Root->AddChildToVerticalBox(SortComboBox)->SetPadding(FMargin(6.0f));
	RefreshButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("리더보드 새로고침"));

	LeaderboardText = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>();
	LeaderboardText->SetIsReadOnly(true);
	LeaderboardText->SetForegroundColor(FLinearColor::Black);
	Root->AddChildToVerticalBox(LeaderboardText)->SetPadding(FMargin(6.0f));

	ProjectProject01LoginUI::AddLabel(WidgetTree, Root,
		TEXT("테스트 성공 결과 입력 (실제 게임 종료 연결 전 수동 검증용)"), 16.0f);
	auto AddInput = [this, Root](const FString& Hint, const FString& DefaultValue)
	{
		UEditableTextBox* Input = WidgetTree->ConstructWidget<UEditableTextBox>();
		Input->SetHintText(FText::FromString(Hint));
		Input->SetText(FText::FromString(DefaultValue));
		Input->SetForegroundColor(FLinearColor::Black);
		Root->AddChildToVerticalBox(Input)->SetPadding(FMargin(6.0f));
		return Input;
	};
	CaptureCountInput = AddInput(TEXT("붙잡은 횟수"), TEXT("2"));
	FirstCaptureSecondsInput = AddInput(TEXT("첫 포획 시간(초)"), TEXT("30"));
	AllCapturedSecondsInput = AddInput(TEXT("전원 포획 시간(초)"), TEXT("120"));
	RescueCountInput = AddInput(TEXT("구출 횟수"), TEXT("1"));
	EscapeSecondsInput = AddInput(TEXT("탈출 시간(초)"), TEXT("180"));
	SubmitRecordButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("현재 성공 기록을 리더보드에 등록"));
	ReturnToLobbyButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("등록하지 않고 로비로 돌아가기"));
}

void UProjectProject01LeaderboardWidget::SetStatus(const FString& Message, const bool bIsError)
{
	if (!IsValid(LeaderboardStatusText))
	{
		return;
	}
	LeaderboardStatusText->SetText(FText::FromString(Message));
	LeaderboardStatusText->SetColorAndOpacity(
		bIsError ? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::Black));
}
