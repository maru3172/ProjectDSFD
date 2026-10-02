// File: Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01LoginWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
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
#include "TimerManager.h"

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
	if (IsValid(CreateRoomButton)) CreateRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleCreateRoomClicked);
	if (IsValid(RefreshRoomsButton)) RefreshRoomsButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleRefreshRoomsClicked);
	if (IsValid(JoinRoomButton)) JoinRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleJoinRoomClicked);
	if (IsValid(ReadyButton)) ReadyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleReadyClicked);
	if (IsValid(StartRoomButton)) StartRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleStartRoomClicked);
	if (IsValid(LeaveRoomButton)) LeaveRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLeaveRoomClicked);
	if (IsValid(SendChatButton)) SendChatButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleSendChatClicked);
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
	if (!IsValid(Lobby) || !IsValid(DirectRoomIdInput) || !IsValid(JoinPasswordInput))
	{
		SetStatus(TEXT("방 참가 UI 또는 로비 시스템을 찾지 못했습니다."), true);
		return;
	}
	FString RoomId = DirectRoomIdInput->GetText().ToString().TrimStartAndEnd();
	if (RoomId.IsEmpty() && IsValid(RoomListComboBox))
	{
		const FString Selected = RoomListComboBox->GetSelectedOption();
		if (const FString* SelectedRoomId = RoomIdByDisplayOption.Find(Selected))
		{
			RoomId = *SelectedRoomId;
		}
	}
	Lobby->JoinRoom(RoomId, JoinPasswordInput->GetText().ToString());
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
		Operation == EProjectProject01LobbyOperation::LeaveRoom))
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
	RoomIdByDisplayOption.Reset();
	for (const FProjectProject01RoomSummary& Room : Lobby->GetRooms())
	{
		const FString LockText = Room.bHasPassword ? TEXT(" [비밀번호]") : FString();
		const FString Option = FString::Printf(TEXT("%s (%d/%d) - %s%s"),
			*Room.Name, Room.MemberCount, Room.MaxPlayers, *Room.HostDisplayName, *LockText);
		RoomListComboBox->AddOption(Option);
		RoomIdByDisplayOption.Add(Option, Room.RoomId);
	}
	if (RoomIdByDisplayOption.Contains(PreviousSelection)) RoomListComboBox->SetSelectedOption(PreviousSelection);
	else if (RoomListComboBox->GetOptionCount() > 0) RoomListComboBox->SetSelectedIndex(0);
}

void UProjectProject01LobbyWidget::RefreshCurrentRoomView()
{
	UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
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
	if (IsValid(SendChatButton)) SendChatButton->SetIsEnabled(bInRoom);
	if (!bInRoom)
	{
		if (IsValid(CurrentRoomText)) CurrentRoomText->SetText(FText::FromString(TEXT("참가 중인 방 없음")));
		if (IsValid(MemberListText)) MemberListText->SetText(FText::GetEmpty());
		if (IsValid(ChatLogText)) ChatLogText->SetText(FText::GetEmpty());
		return;
	}

	const FProjectProject01RoomState& Room = Lobby->GetCurrentRoom();
	if (IsValid(CurrentRoomText))
	{
		CurrentRoomText->SetText(FText::FromString(FString::Printf(
			TEXT("방: %s\n방 ID: %s\n인원: %d/3"), *Room.Name, *Room.RoomId, Room.Members.Num())));
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
		TravelToStartedGame(Room);
	}
}

void UProjectProject01LobbyWidget::TravelToStartedGame(const FProjectProject01RoomState& RoomState)
{
	if (bTravelRequested || RoomState.TravelUrl.IsEmpty() || RoomState.AssignedRole.IsEmpty())
	{
		if (!bTravelRequested && RoomState.bStarted)
		{
			SetStatus(TEXT("게임 시작 정보에 서버 주소 또는 역할이 없습니다."), true);
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
	const FString TravelUrl = FString::Printf(TEXT("%s?LobbyRoomId=%s?LobbyRole=%s"),
		*RoomState.TravelUrl, *RoomState.RoomId, *RoomState.AssignedRole);
	SetStatus(FString::Printf(TEXT("MultiplayTest 서버로 이동합니다. 역할: %s"), *RoomState.AssignedRole), false);
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
	Root->AddChildToVerticalBox(RoomNameInput)->SetPadding(FMargin(6.0f));
	PublicRoomCheckBox = WidgetTree->ConstructWidget<UCheckBox>();
	PublicRoomCheckBox->SetIsChecked(true);
	UTextBlock* PublicLabel = WidgetTree->ConstructWidget<UTextBlock>();
	PublicLabel->SetText(FText::FromString(TEXT("공개방 (끄면 비공개방: 방 ID로만 참가)")));
	PublicRoomCheckBox->AddChild(PublicLabel);
	Root->AddChildToVerticalBox(PublicRoomCheckBox)->SetPadding(FMargin(6.0f));
	CreatePasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	CreatePasswordInput->SetHintText(FText::FromString(TEXT("방 비밀번호 (선택, 사용 시 4~64자)")));
	CreatePasswordInput->SetIsPassword(true);
	Root->AddChildToVerticalBox(CreatePasswordInput)->SetPadding(FMargin(6.0f));
	CreateRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("방 만들기"));
	RefreshRoomsButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("공개방 목록 새로고침"));

	RoomListComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
	Root->AddChildToVerticalBox(RoomListComboBox)->SetPadding(FMargin(6.0f));
	DirectRoomIdInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	DirectRoomIdInput->SetHintText(FText::FromString(TEXT("비공개방 ID 또는 직접 참가할 방 ID")));
	Root->AddChildToVerticalBox(DirectRoomIdInput)->SetPadding(FMargin(6.0f));
	JoinPasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	JoinPasswordInput->SetHintText(FText::FromString(TEXT("참가 비밀번호 (비밀번호방만)")));
	JoinPasswordInput->SetIsPassword(true);
	Root->AddChildToVerticalBox(JoinPasswordInput)->SetPadding(FMargin(6.0f));
	JoinRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("선택/ID 방 참가"));

	CurrentRoomText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("참가 중인 방 없음"), 16.0f);
	MemberListText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT(""), 14.0f);
	ReadyButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("준비"));
	StartRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("게임 시작"));
	LeaveRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("방 나가기"));
	ReadyButton->SetVisibility(ESlateVisibility::Collapsed);
	StartRoomButton->SetVisibility(ESlateVisibility::Collapsed);
	LeaveRoomButton->SetVisibility(ESlateVisibility::Collapsed);

	ChatLogText = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>();
	ChatLogText->SetIsReadOnly(true);
	ChatLogText->SetHintText(FText::FromString(TEXT("방 채팅")));
	Root->AddChildToVerticalBox(ChatLogText)->SetPadding(FMargin(6.0f));
	ChatInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	ChatInput->SetHintText(FText::FromString(TEXT("채팅 입력 (최대 300자)")));
	Root->AddChildToVerticalBox(ChatInput)->SetPadding(FMargin(6.0f));
	SendChatButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("채팅 보내기"));
	SendChatButton->SetIsEnabled(false);
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
