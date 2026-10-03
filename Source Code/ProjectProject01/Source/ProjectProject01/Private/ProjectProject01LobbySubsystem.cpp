// File: Source/ProjectProject01/Private/ProjectProject01LobbySubsystem.cpp
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01LobbySubsystem.h"

#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "ProjectProject01AuthSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectProject01Lobby, Log, All);

namespace ProjectProject01Lobby
{
	bool IsReadOperation(const EProjectProject01LobbyOperation Operation)
	{
		return Operation == EProjectProject01LobbyOperation::RefreshRooms ||
			Operation == EProjectProject01LobbyOperation::RefreshCurrentRoom ||
			Operation == EProjectProject01LobbyOperation::RefreshLeaderboard;
	}

	FString NormalizeBaseUrl(FString Url)
	{
		Url.TrimStartAndEndInline();
		while (Url.EndsWith(TEXT("/")))
		{
			Url.LeftChopInline(1);
		}
		return Url;
	}

	bool ReadRequiredString(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, FString& OutValue)
	{
		return Json.IsValid() && Json->TryGetStringField(Field, OutValue);
	}

	const TCHAR* RoleToString(const EProjectProject01LeaderboardRole Role)
	{
		return Role == EProjectProject01LeaderboardRole::Mannequin ? TEXT("Mannequin") : TEXT("Survivor");
	}

	const TCHAR* SortToString(const EProjectProject01LeaderboardSort Sort)
	{
		switch (Sort)
		{
		case EProjectProject01LeaderboardSort::Captures: return TEXT("captures");
		case EProjectProject01LeaderboardSort::FirstCapture: return TEXT("firstCapture");
		case EProjectProject01LeaderboardSort::AllCaptured: return TEXT("allCaptured");
		case EProjectProject01LeaderboardSort::Rescues: return TEXT("rescues");
		case EProjectProject01LeaderboardSort::Escape: return TEXT("escape");
		default: return TEXT("overall");
		}
	}
}

void UProjectProject01LobbySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ApiBaseUrl = ProjectProject01Lobby::NormalizeBaseUrl(ApiBaseUrl);
	RequestTimeoutSeconds = FMath::Clamp(RequestTimeoutSeconds, 2.0f, 60.0f);
	if (!ApiBaseUrl.StartsWith(TEXT("https://")) && !ApiBaseUrl.StartsWith(TEXT("http://127.0.0.1")) &&
		!ApiBaseUrl.StartsWith(TEXT("http://localhost")))
	{
		UE_LOG(LogProjectProject01Lobby, Error,
			TEXT("Lobby API must use HTTPS except for loopback development: %s"), *ApiBaseUrl);
	}
}

void UProjectProject01LobbySubsystem::Deinitialize()
{
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	Rooms.Reset();
	CurrentRoom = FProjectProject01RoomState();
	LeaderboardEntries.Reset();
	Super::Deinitialize();
}

void UProjectProject01LobbySubsystem::RefreshRooms()
{
	SendRequest(EProjectProject01LobbyOperation::RefreshRooms, TEXT("GET"), TEXT("/api/rooms"), FString());
}

void UProjectProject01LobbySubsystem::RefreshCurrentRoom()
{
	SendRequest(EProjectProject01LobbyOperation::RefreshCurrentRoom, TEXT("GET"), TEXT("/api/rooms/current"), FString());
}

void UProjectProject01LobbySubsystem::CreateRoom(
	const FString& RoomName,
	const bool bIsPublic,
	const FString& Password)
{
	const FString CleanName = RoomName.TrimStartAndEnd();
	if (CleanName.Len() < 1 || CleanName.Len() > 48)
	{
		Complete(EProjectProject01LobbyOperation::CreateRoom, false, TEXT("방 이름은 1~48자여야 합니다."));
		return;
	}
	const FString CleanPassword = Password.TrimStartAndEnd();
	if (!CleanPassword.IsEmpty() && (CleanPassword.Len() < 4 || CleanPassword.Len() > 64))
	{
		Complete(EProjectProject01LobbyOperation::CreateRoom, false,
			TEXT("방 비밀번호는 사용하지 않거나 4~64자로 입력해야 합니다."));
		return;
	}
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), CleanName);
	Json->SetBoolField(TEXT("isPublic"), bIsPublic);
	Json->SetStringField(TEXT("password"), CleanPassword);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));
	SendRequest(EProjectProject01LobbyOperation::CreateRoom, TEXT("POST"), TEXT("/api/rooms"), Body);
}

void UProjectProject01LobbySubsystem::JoinRoom(const FString& RoomCode, const FString& Password)
{
	FString CleanCode = RoomCode.TrimStartAndEnd().ToUpper();
	if (CleanCode.Len() < 6 || CleanCode.Len() > 8)
	{
		Complete(EProjectProject01LobbyOperation::JoinRoom, false, TEXT("6~8자리 참가 코드를 입력하거나 공개방을 선택하세요."));
		return;
	}
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("password"), Password.TrimStartAndEnd());
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));
	SendRequest(EProjectProject01LobbyOperation::JoinRoom, TEXT("POST"),
		TEXT("/api/rooms/") + FGenericPlatformHttp::UrlEncode(CleanCode) + TEXT("/join"), Body);
}

void UProjectProject01LobbySubsystem::LeaveRoom()
{
	SendRequest(EProjectProject01LobbyOperation::LeaveRoom, TEXT("POST"), TEXT("/api/rooms/current/leave"), TEXT("{}"));
}

void UProjectProject01LobbySubsystem::TransferHost(const FString& TargetUserId)
{
	const FString CleanTargetUserId = TargetUserId.TrimStartAndEnd();
	if (CleanTargetUserId.IsEmpty())
	{
		Complete(EProjectProject01LobbyOperation::TransferHost, false, TEXT("방장을 넘길 플레이어를 선택하세요."));
		return;
	}
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("targetUserId"), CleanTargetUserId);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));
	SendRequest(EProjectProject01LobbyOperation::TransferHost, TEXT("POST"),
		TEXT("/api/rooms/current/transfer-host"), Body);
}

void UProjectProject01LobbySubsystem::DeleteRoom()
{
	SendRequest(EProjectProject01LobbyOperation::DeleteRoom, TEXT("POST"), TEXT("/api/rooms/current/delete"), TEXT("{}"));
}

void UProjectProject01LobbySubsystem::SetReady(const bool bReady)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("ready"), bReady);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));
	SendRequest(EProjectProject01LobbyOperation::SetReady, TEXT("POST"), TEXT("/api/rooms/current/ready"), Body);
}

void UProjectProject01LobbySubsystem::StartRoom()
{
	SendRequest(EProjectProject01LobbyOperation::StartRoom, TEXT("POST"), TEXT("/api/rooms/current/start"), TEXT("{}"));
}

void UProjectProject01LobbySubsystem::SendChat(const FString& Message)
{
	const FString CleanMessage = Message.TrimStartAndEnd();
	if (CleanMessage.IsEmpty() || CleanMessage.Len() > 300)
	{
		Complete(EProjectProject01LobbyOperation::SendChat, false, TEXT("채팅은 1~300자여야 합니다."));
		return;
	}
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("message"), CleanMessage);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));
	SendRequest(EProjectProject01LobbyOperation::SendChat, TEXT("POST"), TEXT("/api/rooms/current/chat"), Body);
}

void UProjectProject01LobbySubsystem::RefreshLeaderboard(
	const EProjectProject01LeaderboardRole Role,
	EProjectProject01LeaderboardSort Sort)
{
	const bool bMannequinSort = Sort == EProjectProject01LeaderboardSort::Overall ||
		Sort == EProjectProject01LeaderboardSort::Captures ||
		Sort == EProjectProject01LeaderboardSort::FirstCapture ||
		Sort == EProjectProject01LeaderboardSort::AllCaptured;
	const bool bSurvivorSort = Sort == EProjectProject01LeaderboardSort::Overall ||
		Sort == EProjectProject01LeaderboardSort::Rescues ||
		Sort == EProjectProject01LeaderboardSort::Escape;
	if ((Role == EProjectProject01LeaderboardRole::Mannequin && !bMannequinSort) ||
		(Role == EProjectProject01LeaderboardRole::Survivor && !bSurvivorSort))
	{
		Sort = EProjectProject01LeaderboardSort::Overall;
	}
	LeaderboardRole = Role;
	LeaderboardSort = Sort;
	const FString Endpoint = FString::Printf(TEXT("/api/leaderboards/%s?sort=%s"),
		ProjectProject01Lobby::RoleToString(Role), ProjectProject01Lobby::SortToString(Sort));
	SendRequest(EProjectProject01LobbyOperation::RefreshLeaderboard, TEXT("GET"), Endpoint, FString());
}

void UProjectProject01LobbySubsystem::SubmitLeaderboardRecord(
	const FString& MatchId,
	const EProjectProject01LeaderboardRole Role,
	const bool bSuccess,
	const int32 CaptureCount,
	const double FirstCaptureSeconds,
	const double AllCapturedSeconds,
	const int32 RescueCount,
	const double EscapeSeconds)
{
	FGuid ParsedMatchId;
	if (!FGuid::Parse(MatchId, ParsedMatchId))
	{
		Complete(EProjectProject01LobbyOperation::SubmitLeaderboardRecord, false, TEXT("경기 ID가 올바르지 않습니다."));
		return;
	}
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("matchId"), ParsedMatchId.ToString(EGuidFormats::DigitsWithHyphens));
	Json->SetStringField(TEXT("role"), ProjectProject01Lobby::RoleToString(Role));
	Json->SetBoolField(TEXT("success"), bSuccess);
	Json->SetNumberField(TEXT("captureCount"), CaptureCount);
	Json->SetNumberField(TEXT("firstCaptureSeconds"), FirstCaptureSeconds);
	Json->SetNumberField(TEXT("allCapturedSeconds"), AllCapturedSeconds);
	Json->SetNumberField(TEXT("rescueCount"), RescueCount);
	Json->SetNumberField(TEXT("escapeSeconds"), EscapeSeconds);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));
	SendRequest(EProjectProject01LobbyOperation::SubmitLeaderboardRecord, TEXT("POST"),
		TEXT("/api/leaderboards/records"), Body);
}

void UProjectProject01LobbySubsystem::SendRequest(
	const EProjectProject01LobbyOperation Operation,
	const FString& Verb,
	const FString& Endpoint,
	const FString& JsonBody,
	const int32 RetryIndex)
{
	if (ActiveRequest.IsValid())
	{
		if (!ProjectProject01Lobby::IsReadOperation(Operation))
		{
			Complete(Operation, false, TEXT("이전 로비 요청이 진행 중입니다."));
		}
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>()
		: nullptr;
	if (!IsValid(Auth) || !Auth->IsSignedIn() || Auth->GetAccessTokenForAuthenticatedRequest().IsEmpty())
	{
		Complete(Operation, false, TEXT("로그인 세션이 필요합니다."));
		return;
	}
	if (ApiBaseUrl.IsEmpty())
	{
		Complete(Operation, false, TEXT("로비 API 주소가 설정되지 않았습니다."));
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiBaseUrl + Endpoint);
	Request->SetVerb(Verb);
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Auth->GetAccessTokenForAuthenticatedRequest());
	Request->SetTimeout(RequestTimeoutSeconds);
	if (!JsonBody.IsEmpty())
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(JsonBody);
	}
	Request->OnProcessRequestComplete().BindUObject(
		this, &UProjectProject01LobbySubsystem::HandleRequestComplete,
		Operation, Verb, Endpoint, JsonBody, RetryIndex);
	ActiveRequest = Request;
	if (!Request->ProcessRequest())
	{
		ActiveRequest.Reset();
		Complete(Operation, false, TEXT("로비 요청을 시작하지 못했습니다."));
	}
}

void UProjectProject01LobbySubsystem::HandleRequestComplete(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	const bool bConnectedSuccessfully,
	const EProjectProject01LobbyOperation Operation,
	FString Verb,
	FString Endpoint,
	FString JsonBody,
	const int32 RetryIndex)
{
	if (ActiveRequest == Request)
	{
		ActiveRequest.Reset();
	}

	const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
	const bool bRetryableRead = ProjectProject01Lobby::IsReadOperation(Operation) && RetryIndex < 1 &&
		(!bConnectedSuccessfully || StatusCode == 408 || StatusCode == 429 || StatusCode >= 500);
	if (bRetryableRead)
	{
		SendRequest(Operation, Verb, Endpoint, JsonBody, RetryIndex + 1);
		return;
	}
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		Complete(Operation, false, TEXT("로비 서버에 연결하지 못했습니다."));
		return;
	}

	TSharedPtr<FJsonObject> Json;
	const bool bJsonValid = FJsonSerializer::Deserialize(
		TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json) && Json.IsValid();
	FString Message = TEXT("로비 서버 응답을 처리하지 못했습니다.");
	if (bJsonValid)
	{
		Json->TryGetStringField(TEXT("message"), Message);
	}
	if (StatusCode < 200 || StatusCode >= 300 || !bJsonValid)
	{
		if (StatusCode == 404 && Operation == EProjectProject01LobbyOperation::RefreshCurrentRoom)
		{
			CurrentRoom = FProjectProject01RoomState();
			OnCurrentRoomChanged.Broadcast();
		}
		else if (StatusCode == 404 &&
			(Operation == EProjectProject01LobbyOperation::LeaveRoom ||
				Operation == EProjectProject01LobbyOperation::DeleteRoom))
		{
			// 서버에서 이미 시간 초과 정리된 방은 로컬에서도 나간 것으로 처리한다.
			CurrentRoom = FProjectProject01RoomState();
			OnCurrentRoomChanged.Broadcast();
			Complete(Operation, true, TEXT("이미 정리된 방에서 나왔습니다."));
			return;
		}
		Complete(Operation, false, Message);
		return;
	}

	FString ParseError;
	bool bParsed = true;
	if (Operation == EProjectProject01LobbyOperation::RefreshRooms)
	{
		bParsed = ParseRoomList(Json, ParseError);
	}
	else if (Operation == EProjectProject01LobbyOperation::RefreshLeaderboard)
	{
		bParsed = ParseLeaderboard(Json, ParseError);
	}
	else if (Operation == EProjectProject01LobbyOperation::LeaveRoom ||
		Operation == EProjectProject01LobbyOperation::DeleteRoom)
	{
		CurrentRoom = FProjectProject01RoomState();
		OnCurrentRoomChanged.Broadcast();
	}
	else if (Operation == EProjectProject01LobbyOperation::SubmitLeaderboardRecord)
	{
		// 등록 응답에는 방 또는 리더보드 배열이 없으며, UI가 성공 후 목록을 새로 요청한다.
	}
	else
	{
		bParsed = ParseRoomState(Json, ParseError);
	}
	Complete(Operation, bParsed, bParsed ? Message : ParseError);
}

bool UProjectProject01LobbySubsystem::ParseRoomList(const TSharedPtr<FJsonObject>& Json, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Json.IsValid() || !Json->TryGetArrayField(TEXT("rooms"), Values) || Values == nullptr)
	{
		OutError = TEXT("방 목록 응답에 rooms 배열이 없습니다.");
		return false;
	}

	TArray<FProjectProject01RoomSummary> ParsedRooms;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		const TSharedPtr<FJsonObject> RoomJson = Value.IsValid() ? Value->AsObject() : nullptr;
		FProjectProject01RoomSummary Room;
		double MemberCount = 0.0;
		double MaxPlayers = 0.0;
		if (!ProjectProject01Lobby::ReadRequiredString(RoomJson, TEXT("roomId"), Room.RoomId) ||
			!ProjectProject01Lobby::ReadRequiredString(RoomJson, TEXT("joinCode"), Room.JoinCode) ||
			!ProjectProject01Lobby::ReadRequiredString(RoomJson, TEXT("name"), Room.Name) ||
			!ProjectProject01Lobby::ReadRequiredString(RoomJson, TEXT("hostDisplayName"), Room.HostDisplayName) ||
			!RoomJson->TryGetNumberField(TEXT("memberCount"), MemberCount) ||
			!RoomJson->TryGetNumberField(TEXT("maxPlayers"), MaxPlayers) ||
			!RoomJson->TryGetBoolField(TEXT("hasPassword"), Room.bHasPassword))
		{
			OutError = TEXT("방 목록 항목 형식이 올바르지 않습니다.");
			return false;
		}
		Room.MemberCount = FMath::Max(0, FMath::RoundToInt(MemberCount));
		Room.MaxPlayers = FMath::Max(1, FMath::RoundToInt(MaxPlayers));
		ParsedRooms.Add(MoveTemp(Room));
	}
	Rooms = MoveTemp(ParsedRooms);
	OnRoomListChanged.Broadcast();
	OutError.Reset();
	return true;
}

bool UProjectProject01LobbySubsystem::ParseRoomState(const TSharedPtr<FJsonObject>& Json, FString& OutError)
{
	const TSharedPtr<FJsonObject>* StateJsonPointer = nullptr;
	if (!Json.IsValid() || !Json->TryGetObjectField(TEXT("room"), StateJsonPointer) || StateJsonPointer == nullptr)
	{
		OutError = TEXT("방 상태 응답에 room 객체가 없습니다.");
		return false;
	}
	const TSharedPtr<FJsonObject>& StateJson = *StateJsonPointer;
	FProjectProject01RoomState Parsed;
	if (!ProjectProject01Lobby::ReadRequiredString(StateJson, TEXT("roomId"), Parsed.RoomId) ||
		!ProjectProject01Lobby::ReadRequiredString(StateJson, TEXT("joinCode"), Parsed.JoinCode) ||
		!ProjectProject01Lobby::ReadRequiredString(StateJson, TEXT("name"), Parsed.Name) ||
		!StateJson->TryGetBoolField(TEXT("isHost"), Parsed.bIsHost) ||
		!StateJson->TryGetBoolField(TEXT("isReady"), Parsed.bIsReady) ||
		!StateJson->TryGetBoolField(TEXT("canStart"), Parsed.bCanStart) ||
		!StateJson->TryGetBoolField(TEXT("started"), Parsed.bStarted))
	{
		OutError = TEXT("방 상태의 필수 값이 올바르지 않습니다.");
		return false;
	}
	StateJson->TryGetStringField(TEXT("travelUrl"), Parsed.TravelUrl);
	StateJson->TryGetStringField(TEXT("assignedRole"), Parsed.AssignedRole);

	const TArray<TSharedPtr<FJsonValue>>* MemberValues = nullptr;
	if (!StateJson->TryGetArrayField(TEXT("members"), MemberValues) || MemberValues == nullptr)
	{
		OutError = TEXT("방 상태에 members 배열이 없습니다.");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *MemberValues)
	{
		const TSharedPtr<FJsonObject> MemberJson = Value.IsValid() ? Value->AsObject() : nullptr;
		FProjectProject01RoomMember Member;
		if (!ProjectProject01Lobby::ReadRequiredString(MemberJson, TEXT("userId"), Member.UserId) ||
			!ProjectProject01Lobby::ReadRequiredString(MemberJson, TEXT("displayName"), Member.DisplayName) ||
			!MemberJson->TryGetBoolField(TEXT("isHost"), Member.bIsHost) ||
			!MemberJson->TryGetBoolField(TEXT("ready"), Member.bIsReady))
		{
			OutError = TEXT("방 참가자 형식이 올바르지 않습니다.");
			return false;
		}
		MemberJson->TryGetStringField(TEXT("assignedRole"), Member.AssignedRole);
		Parsed.Members.Add(MoveTemp(Member));
	}

	const TArray<TSharedPtr<FJsonValue>>* ChatValues = nullptr;
	if (StateJson->TryGetArrayField(TEXT("chatMessages"), ChatValues) && ChatValues != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ChatValues)
		{
			const TSharedPtr<FJsonObject> ChatJson = Value.IsValid() ? Value->AsObject() : nullptr;
			FProjectProject01RoomChatMessage Chat;
			if (!ProjectProject01Lobby::ReadRequiredString(ChatJson, TEXT("messageId"), Chat.MessageId) ||
				!ProjectProject01Lobby::ReadRequiredString(ChatJson, TEXT("displayName"), Chat.DisplayName) ||
				!ProjectProject01Lobby::ReadRequiredString(ChatJson, TEXT("message"), Chat.Message) ||
				!ProjectProject01Lobby::ReadRequiredString(ChatJson, TEXT("createdAtUtc"), Chat.CreatedAtUtc))
			{
				OutError = TEXT("채팅 메시지 형식이 올바르지 않습니다.");
				return false;
			}
			Parsed.ChatMessages.Add(MoveTemp(Chat));
		}
	}

	CurrentRoom = MoveTemp(Parsed);
	OnCurrentRoomChanged.Broadcast();
	OutError.Reset();
	return true;
}

bool UProjectProject01LobbySubsystem::ParseLeaderboard(const TSharedPtr<FJsonObject>& Json, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Json.IsValid() || !Json->TryGetArrayField(TEXT("entries"), Values) || Values == nullptr)
	{
		OutError = TEXT("리더보드 응답에 entries 배열이 없습니다.");
		return false;
	}

	TArray<FProjectProject01LeaderboardEntry> ParsedEntries;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		const TSharedPtr<FJsonObject> EntryJson = Value.IsValid() ? Value->AsObject() : nullptr;
		FProjectProject01LeaderboardEntry Entry;
		double Rank = 0.0;
		if (!EntryJson.IsValid() || !EntryJson->TryGetNumberField(TEXT("rank"), Rank) ||
			!ProjectProject01Lobby::ReadRequiredString(EntryJson, TEXT("displayName"), Entry.DisplayName) ||
			!ProjectProject01Lobby::ReadRequiredString(EntryJson, TEXT("role"), Entry.Role) ||
			!EntryJson->TryGetNumberField(TEXT("overallScore"), Entry.OverallScore) ||
			!ProjectProject01Lobby::ReadRequiredString(EntryJson, TEXT("createdAtUtc"), Entry.CreatedAtUtc))
		{
			OutError = TEXT("리더보드 항목 형식이 올바르지 않습니다.");
			return false;
		}
		Entry.Rank = FMath::Max(1, FMath::RoundToInt(Rank));
		double Number = 0.0;
		if (EntryJson->TryGetNumberField(TEXT("captureCount"), Number)) Entry.CaptureCount = FMath::Max(0, FMath::RoundToInt(Number));
		if (EntryJson->TryGetNumberField(TEXT("firstCaptureSeconds"), Number)) Entry.FirstCaptureSeconds = FMath::Max(0.0, Number);
		if (EntryJson->TryGetNumberField(TEXT("allCapturedSeconds"), Number)) Entry.AllCapturedSeconds = FMath::Max(0.0, Number);
		if (EntryJson->TryGetNumberField(TEXT("rescueCount"), Number)) Entry.RescueCount = FMath::Max(0, FMath::RoundToInt(Number));
		if (EntryJson->TryGetNumberField(TEXT("escapeSeconds"), Number)) Entry.EscapeSeconds = FMath::Max(0.0, Number);
		ParsedEntries.Add(MoveTemp(Entry));
	}
	LeaderboardEntries = MoveTemp(ParsedEntries);
	OnLeaderboardChanged.Broadcast();
	OutError.Reset();
	return true;
}

void UProjectProject01LobbySubsystem::Complete(
	const EProjectProject01LobbyOperation Operation,
	const bool bSuccess,
	const FString& Message)
{
	OnRequestCompleted.Broadcast(Operation, bSuccess, Message);
}
