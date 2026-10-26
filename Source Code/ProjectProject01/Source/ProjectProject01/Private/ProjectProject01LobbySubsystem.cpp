// File: Source/ProjectProject01/Private/ProjectProject01LobbySubsystem.cpp
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "ProjectHttpDiagnostics.h"
#include "ProjectRequestProtection.h"

#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01GameInstance.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectProject01Lobby, Log, All);

#if WITH_EDITOR
TFunction<void(TFunction<void(bool,const FString&)>)> UProjectProject01LobbySubsystem::PrepareEditorLocalServer;
bool UProjectProject01LobbySubsystem::IsEditorLoopbackTest() const
{
	return GIsEditor && GetWorld() && GetWorld()->WorldType==EWorldType::PIE &&
		ApiBaseUrl.Equals(TEXT("http://127.0.0.1:5080"),ESearchCase::IgnoreCase) && !!PrepareEditorLocalServer;
}
#endif

namespace ProjectProject01Lobby
{
	bool IsApiBaseUrlAllowed(const FString& Url, const FString& AllowedInsecureVpnUrl)
	{
		return Url.StartsWith(TEXT("https://")) || Url.StartsWith(TEXT("http://127.0.0.1")) ||
			Url.StartsWith(TEXT("http://localhost")) ||
			(!AllowedInsecureVpnUrl.IsEmpty() && Url.Equals(AllowedInsecureVpnUrl, ESearchCase::IgnoreCase));
	}

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

	const TCHAR* ScopeToString(const EProjectProject01LeaderboardScope Scope)
	{
		return Scope == EProjectProject01LeaderboardScope::Team ? TEXT("team") : TEXT("individual");
	}
}

void UProjectProject01LobbySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ApiBaseUrl = ProjectProject01Lobby::NormalizeBaseUrl(ApiBaseUrl);
	AllowedInsecureVpnApiBaseUrl = ProjectProject01Lobby::NormalizeBaseUrl(AllowedInsecureVpnApiBaseUrl);
	RequestTimeoutSeconds = FMath::Clamp(RequestTimeoutSeconds, 2.0f, 60.0f);
	if (!ProjectProject01Lobby::IsApiBaseUrlAllowed(ApiBaseUrl, AllowedInsecureVpnApiBaseUrl))
	{
		UE_LOG(LogProjectProject01Lobby, Error,
			TEXT("Lobby API must use HTTPS except for loopback or the explicitly configured VPN test endpoint: %s"),
			*ApiBaseUrl);
	}
}

void UProjectProject01LobbySubsystem::Deinitialize()
{
	ForceClearLocalSession();
	Super::Deinitialize();
}

void UProjectProject01LobbySubsystem::ForceClearLocalSession()
{
	++RequestGeneration;
	bEditorServerPreparationPending=false;
	bEditorServerPrepared=false;
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	Rooms.Reset();
    bMatchmaking=false;bCancelMatchmaking=false;MatchSearchId.Reset();MatchmakingMessage.Reset();MatchmakingWaiting=0;MatchmakingPosition=0;MatchmakingSeconds=0;
    MatchmakingStartedAtSeconds=0;
	CurrentRoom = FProjectProject01RoomState();
	LeaderboardEntries.Reset();
    DesiredLeaderboardEndpoint.Reset();bLeaderboardRefreshQueued=false;
	ClearGameJoinTicket();
	ClearPendingLeaderboardRecord();
	bLeaderboardOpenedFromMatchResult = false;
	OnRoomListChanged.Broadcast();
	OnCurrentRoomChanged.Broadcast();
	OnLeaderboardChanged.Broadcast();
}

bool UProjectProject01LobbySubsystem::ConfigureLocalTestApiEndpoint(const FString& InApiBaseUrl)
{
	if (ActiveRequest.IsValid() || CurrentRoom.IsValid())
	{
		return false;
	}
	ApiBaseUrl = ProjectProject01Lobby::NormalizeBaseUrl(InApiBaseUrl);
	AllowedInsecureVpnApiBaseUrl = ApiBaseUrl;
	return ProjectProject01Lobby::IsApiBaseUrlAllowed(ApiBaseUrl, AllowedInsecureVpnApiBaseUrl);
}

void UProjectProject01LobbySubsystem::RefreshRooms()
{
	SendRequest(EProjectProject01LobbyOperation::RefreshRooms, TEXT("GET"), TEXT("/api/rooms"), FString());
}

void UProjectProject01LobbySubsystem::RefreshCurrentRoom()
{
	SendRequest(EProjectProject01LobbyOperation::RefreshCurrentRoom, TEXT("GET"), TEXT("/api/rooms/current"), FString());
}
void UProjectProject01LobbySubsystem::JoinMatchmaking()
{
    if(!HasCurrentRoom()||!CurrentRoom.bRandomParty||!CurrentRoom.bIsHost||CurrentRoom.Members.Num()!=1||bMatchmaking||IsRequestInFlight())return;
    MatchSearchId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);bMatchmaking=true;bCancelMatchmaking=false;
    MatchmakingStartedAtSeconds=FPlatformTime::Seconds();
    MatchmakingMessage=TEXT("매칭 신청 중…");
    SendRequest(EProjectProject01LobbyOperation::JoinMatchmaking,TEXT("POST"),TEXT("/api/rooms/matchmaking/join"),TEXT("{\"searchId\":\"")+MatchSearchId+TEXT("\"}"));
}
void UProjectProject01LobbySubsystem::CreateMatchParty()
{
    if(IsRequestInFlight()||HasCurrentRoom())return;
    SendRequest(EProjectProject01LobbyOperation::CreateMatchParty,TEXT("POST"),TEXT("/api/rooms/matchmaking/party"),TEXT("{}"));
}
int32 UProjectProject01LobbySubsystem::GetMatchmakingElapsedSeconds() const
{
    // Monotonic local time keeps the display ticking between server polls and
    // ignores wall-clock changes. It is not the server's queue-expiry countdown.
    return bMatchmaking && MatchmakingStartedAtSeconds>0
        ? FMath::Max(0,FMath::FloorToInt(FPlatformTime::Seconds()-MatchmakingStartedAtSeconds)) : 0;
}
void UProjectProject01LobbySubsystem::PollMatchmaking()
{
    if(!bMatchmaking||IsRequestInFlight())return;
    if(bCancelMatchmaking){CancelMatchmaking();return;}
    SendRequest(EProjectProject01LobbyOperation::PollMatchmaking,TEXT("GET"),TEXT("/api/rooms/matchmaking/status/")+MatchSearchId,FString());
}
void UProjectProject01LobbySubsystem::CancelMatchmaking()
{
    if(!bMatchmaking)return;bCancelMatchmaking=true;
    if(IsRequestInFlight())return;
    SendRequest(EProjectProject01LobbyOperation::CancelMatchmaking,TEXT("POST"),TEXT("/api/rooms/matchmaking/cancel"),TEXT("{\"searchId\":\"")+MatchSearchId+TEXT("\"}"));
}
bool UProjectProject01LobbySubsystem::ParseMatchmaking(const TSharedPtr<FJsonObject>& Json,FString& Error)
{
    FString State;if(!Json||!Json->TryGetStringField(TEXT("state"),State)){Error=TEXT("매칭 상태 응답이 올바르지 않습니다.");return false;}
    Json->TryGetStringField(TEXT("message"),MatchmakingMessage);
    double N=0;Json->TryGetNumberField(TEXT("waitingCount"),N);MatchmakingWaiting=FMath::Max(0,(int32)N);
    N=0;Json->TryGetNumberField(TEXT("position"),N);MatchmakingPosition=FMath::Max(0,(int32)N);
    N=0;Json->TryGetNumberField(TEXT("secondsRemaining"),N);MatchmakingSeconds=FMath::Max(0,(int32)N);
    bMatchmaking=State==TEXT("Waiting");
    if(bMatchmaking && MatchmakingStartedAtSeconds<=0)MatchmakingStartedAtSeconds=FPlatformTime::Seconds();
    if(!bMatchmaking)MatchmakingStartedAtSeconds=0;
    if(!bMatchmaking)bCancelMatchmaking=false;
    const TSharedPtr<FJsonObject>* Room=nullptr;
    if(Json->TryGetObjectField(TEXT("room"),Room))return ParseRoomState(Json,Error);
    OnCurrentRoomChanged.Broadcast();return true;
}

void UProjectProject01LobbySubsystem::CreateBotMatch(const FString& Role,int32 Difficulty)
{
    if(HasCurrentRoom()||IsMatchmaking()||IsRequestInFlight())return;
    if((Role!=TEXT("Survivor")&&Role!=TEXT("Mannequin"))||Difficulty<1||Difficulty>5)
    {Complete(EProjectProject01LobbyOperation::CreateBotMatch,false,TEXT("역할과 1~5단계 난이도를 선택하세요."));return;}
    ClearGameJoinTicket();
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("role"),Role);Json->SetNumberField(TEXT("difficulty"),Difficulty);
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));
    SendRequest(EProjectProject01LobbyOperation::CreateBotMatch,TEXT("POST"),TEXT("/api/bot-matches"),Body);
}

void UProjectProject01LobbySubsystem::CreateRoom(
	const FString& RoomName,
	const bool bIsPublic,
	const FString& Password)
{
	ClearGameJoinTicket();
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
	ClearGameJoinTicket();
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

void UProjectProject01LobbySubsystem::ReturnToRoom()
{
	ClearGameJoinTicket();
	SendRequest(EProjectProject01LobbyOperation::ReturnToRoom, TEXT("POST"),
		TEXT("/api/rooms/current/return"), TEXT("{}"));
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
	ClearGameJoinTicket();
	SendRequest(EProjectProject01LobbyOperation::StartRoom, TEXT("POST"), TEXT("/api/rooms/current/start"), TEXT("{}"));
}

void UProjectProject01LobbySubsystem::CancelStartRoom()
{
	ClearGameJoinTicket();
	SendRequest(EProjectProject01LobbyOperation::CancelStartRoom, TEXT("POST"),
		TEXT("/api/rooms/current/cancel-start"), TEXT("{}"));
}

void UProjectProject01LobbySubsystem::ConfirmStartRoom()
{
	ClearGameJoinTicket();
	SendRequest(EProjectProject01LobbyOperation::ConfirmStartRoom, TEXT("POST"),
		TEXT("/api/rooms/current/confirm-start"), TEXT("{}"));
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

void UProjectProject01LobbySubsystem::EditRoomBot(const FString& Action,const FString& BotId,int32 Difficulty)
{
    if(!HasCurrentRoom()||!CurrentRoom.bIsHost||CurrentRoom.bBotMatch||CurrentRoom.bRandomMatch||CurrentRoom.bRandomParty||
        CurrentRoom.bStarted||CurrentRoom.bStarting||CurrentRoom.bServerPreparing||Difficulty<1||Difficulty>5||
        (Action!=TEXT("add")&&Action!=TEXT("remove")&&Action!=TEXT("difficulty")))return;
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("action"),Action);Json->SetStringField(TEXT("botId"),BotId);Json->SetNumberField(TEXT("difficulty"),Difficulty);
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));
    SendRequest(EProjectProject01LobbyOperation::EditRoomBot,TEXT("POST"),TEXT("/api/rooms/current/bots"),Body);
}
void UProjectProject01LobbySubsystem::EditRoomParticipant(const FString& Action,const FString& UserId)
{
    if(!HasCurrentRoom()||CurrentRoom.bBotMatch||CurrentRoom.bRandomMatch||CurrentRoom.bRandomParty||CurrentRoom.bStarted||CurrentRoom.bStarting||CurrentRoom.bServerPreparing)return;
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("action"),Action);
    if(!UserId.IsEmpty())Json->SetStringField(TEXT("userId"),UserId);
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));
    SendRequest(EProjectProject01LobbyOperation::EditRoomParticipant,TEXT("POST"),TEXT("/api/rooms/current/participants"),Body);
}

void UProjectProject01LobbySubsystem::RequestGameJoinTicket()
{
	ClearGameJoinTicket();
	SendRequest(EProjectProject01LobbyOperation::RequestGameTicket, TEXT("POST"), TEXT("/api/game-tickets"), TEXT("{}"));
}

void UProjectProject01LobbySubsystem::RefreshLeaderboard(
	const EProjectProject01LeaderboardRole Role,
	EProjectProject01LeaderboardSort Sort,
	EProjectProject01LeaderboardScope Scope,
	const EProjectProject01LeaderboardCategory Category)
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
	if (Role == EProjectProject01LeaderboardRole::Mannequin)
	{
		Scope = EProjectProject01LeaderboardScope::Individual;
	}
	LeaderboardRole = Role;
	LeaderboardSort = Sort;
	LeaderboardScope = Scope;
	const FString Endpoint = FString::Printf(TEXT("/api/leaderboards/%s?sort=%s&scope=%s&category=%s"),
		ProjectProject01Lobby::RoleToString(Role), ProjectProject01Lobby::SortToString(Sort),
		ProjectProject01Lobby::ScopeToString(Scope),
		Category == EProjectProject01LeaderboardCategory::Bot ? TEXT("bot") : TEXT("pvp"));
    DesiredLeaderboardEndpoint=Endpoint;
    LeaderboardEntries.Reset();OnLeaderboardChanged.Broadcast();
    if(IsRequestInFlight()){bLeaderboardRefreshQueued=true;return;}
    bLeaderboardRefreshQueued=false;
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

void UProjectProject01LobbySubsystem::CachePendingLeaderboardRecord(
	const FString& MatchId,
	const EProjectProject01LeaderboardRole Role,
	const int32 CaptureCount,
	const double FirstCaptureSeconds,
	const double AllCapturedSeconds,
	const int32 RescueCount,
	const double EscapeSeconds)
{
	FGuid ParsedMatchId;
	if (!FGuid::Parse(MatchId, ParsedMatchId))
	{
		ClearPendingLeaderboardRecord();
		return;
	}

	PendingLeaderboardRecord.MatchId = ParsedMatchId.ToString(EGuidFormats::DigitsWithHyphens);
	PendingLeaderboardRecord.Role = Role;
	PendingLeaderboardRecord.CaptureCount = CaptureCount;
	PendingLeaderboardRecord.FirstCaptureSeconds = FirstCaptureSeconds;
	PendingLeaderboardRecord.AllCapturedSeconds = AllCapturedSeconds;
	PendingLeaderboardRecord.RescueCount = RescueCount;
	PendingLeaderboardRecord.EscapeSeconds = EscapeSeconds;
}

void UProjectProject01LobbySubsystem::SubmitPendingLeaderboardRecord()
{
	if (!PendingLeaderboardRecord.IsValid())
	{
		Complete(EProjectProject01LobbyOperation::SubmitLeaderboardRecord, false,
			TEXT("등록할 서버 검증 경기 기록이 없습니다."));
		return;
	}

	const FProjectProject01PendingLeaderboardRecord Record = PendingLeaderboardRecord;
	SubmitLeaderboardRecord(Record.MatchId, Record.Role, true,
		Record.CaptureCount, Record.FirstCaptureSeconds, Record.AllCapturedSeconds,
		Record.RescueCount, Record.EscapeSeconds);
}

void UProjectProject01LobbySubsystem::ClearPendingLeaderboardRecord()
{
	PendingLeaderboardRecord = FProjectProject01PendingLeaderboardRecord();
}

void UProjectProject01LobbySubsystem::SendRequest(
	const EProjectProject01LobbyOperation Operation,
	const FString& Verb,
	const FString& Endpoint,
	const FString& JsonBody,
	const int32 RetryIndex)
{
#if WITH_DEV_AUTOMATION_TESTS
    if(ReviewRequestSink){ReviewRequestSink(Endpoint);return;}
#endif
	if (ActiveRequest.IsValid())
	{
		if (!ProjectProject01Lobby::IsReadOperation(Operation))
		{
			Complete(Operation, false, TEXT("이전 로비 요청이 진행 중입니다."));
		}
		return;
	}
	if (bEditorServerPreparationPending) return;
	++RequestGeneration;

	const UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>()
		: nullptr;
	if (!IsValid(Auth) || !Auth->IsSignedIn() || Auth->GetAccessTokenForAuthenticatedRequest().IsEmpty())
	{
		Complete(Operation, false, TEXT("로그인 세션이 필요합니다."));
		return;
	}
	if (ApiBaseUrl.IsEmpty() ||
		!ProjectProject01Lobby::IsApiBaseUrlAllowed(ApiBaseUrl, AllowedInsecureVpnApiBaseUrl))
	{
		Complete(Operation, false, TEXT("로비 API 주소가 설정되지 않았습니다."));
		return;
	}

#if WITH_EDITOR
	if (!bEditorServerPrepared && IsEditorLoopbackTest() &&
		(Operation==EProjectProject01LobbyOperation::RefreshRooms || Operation==EProjectProject01LobbyOperation::RefreshCurrentRoom ||
		 Operation==EProjectProject01LobbyOperation::StartRoom || Operation==EProjectProject01LobbyOperation::CreateMatchParty || Operation==EProjectProject01LobbyOperation::JoinMatchmaking))
	{
		bEditorServerPreparationPending=true;
		const uint64 Generation=RequestGeneration;
		const TWeakObjectPtr<UProjectProject01LobbySubsystem> Weak(this);
		PrepareEditorLocalServer([Weak,Generation,Operation,Verb,Endpoint,JsonBody,RetryIndex](bool bOK,const FString& Message)
		{
			if (auto* Self=Weak.Get();Self && Self->RequestGeneration==Generation)
			{
				Self->bEditorServerPreparationPending=false;
				Self->bEditorServerPrepared=bOK;
				if (bOK) Self->SendRequest(Operation,Verb,Endpoint,JsonBody,RetryIndex);
				else Self->Complete(Operation,false,Message);
			}
		});
		return;
	}
#endif
	FString CircuitError;
	if(!ProjectRequestProtection::Acquire(GetGameInstance(),ApiBaseUrl,RequestTimeoutSeconds,ActiveCircuitTicket,CircuitError))
	{
		Complete(Operation,false,CircuitError);
		return;
	}
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiBaseUrl + Endpoint);
	Request->SetVerb(Verb);
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Auth->GetAccessTokenForAuthenticatedRequest());
	FProjectProject01VersionContract::ApplyToRequest(Request);
#if WITH_EDITOR
	// Editor builds only: a loopback backend may then run bot/observer matches on an editor dedicated
	// server. Packaged clients never send this and still need a real dedicated server.
	Request->SetHeader(TEXT("X-ProjectProject01-Client"), TEXT("Editor"));
#endif
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
		ProjectRequestProtection::Complete(GetGameInstance(),ActiveCircuitTicket,Request,nullptr,false);
		ProjectHttpDiagnostics::Complete(GetWorld(),TEXT("Lobby"),Request,nullptr,false,RetryIndex,1,false,RequestTimeoutSeconds);
		ActiveRequest.Reset();
		Complete(Operation, false, TEXT("로비 요청을 시작하지 못했습니다."));
		if (UGameInstance* MutableGameInstance = GetGameInstance(); IsValid(MutableGameInstance))
		{
			if (UProjectProject01AuthSubsystem* MutableAuth =
				MutableGameInstance->GetSubsystem<UProjectProject01AuthSubsystem>(); IsValid(MutableAuth))
			{
				MutableAuth->InvalidateSessionFromServerFailure(
					TEXT("백엔드 서버가 꺼져 있습니다. 다시 로그인하세요."));
			}
		}
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
	if (ActiveRequest != Request) return;
	ActiveRequest.Reset();

	const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
	ProjectHttpDiagnostics::Complete(GetWorld(),TEXT("Lobby"),Request,Response,bConnectedSuccessfully,RetryIndex,1,
		(StatusCode==401&&RetryIndex==0)||(ProjectProject01Lobby::IsReadOperation(Operation)&&RetryIndex<1&&
		(!bConnectedSuccessfully||StatusCode==408||StatusCode==429||StatusCode>=500)),RequestTimeoutSeconds);
	ProjectRequestProtection::Complete(GetGameInstance(),ActiveCircuitTicket,Request,Response,bConnectedSuccessfully);
	if (StatusCode == 401 && RetryIndex == 0)
	{
		UGameInstance* GameInstance = GetGameInstance();
		UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
			? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
		if (IsValid(Auth))
		{
			const TWeakObjectPtr<UProjectProject01LobbySubsystem> WeakThis(this);
			const uint64 Generation=RequestGeneration;
			Auth->RefreshSession([WeakThis, Operation, Verb, Endpoint, JsonBody, Generation](const bool bRefreshed)
			{
				if (UProjectProject01LobbySubsystem* Self = WeakThis.Get(); IsValid(Self) && Self->RequestGeneration==Generation)
				{
					if (bRefreshed)
					{
						Self->SendRequest(Operation, Verb, Endpoint, JsonBody, 1);
					}
					else
					{
						Self->Complete(Operation, false, TEXT("로그인 세션이 만료되었습니다. 다시 로그인하세요."));
					}
				}
			});
			return;
		}
	}
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
		if (UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
		{
			if (UProjectProject01AuthSubsystem* Auth =
				GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>(); IsValid(Auth))
			{
				Auth->InvalidateSessionFromServerFailure(
					TEXT("백엔드 서버 연결이 종료되었습니다. 다시 로그인하세요."));
			}
		}
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
	else if (StatusCode == 403 && Operation == EProjectProject01LobbyOperation::SubmitLeaderboardRecord)
	{
		Message = TEXT("서버가 검증한 현재 계정의 성공 경기 기록을 찾지 못했습니다.");
	}
	else if (StatusCode == 429)
	{
		Message = TEXT("요청이 너무 많습니다. 잠시 후 다시 시도하세요.");
	}
	if (StatusCode < 200 || StatusCode >= 300 || !bJsonValid)
	{
		if (StatusCode == 404 && Operation == EProjectProject01LobbyOperation::RefreshCurrentRoom)
		{
			CurrentRoom = FProjectProject01RoomState();
			ClearGameJoinTicket();
			OnCurrentRoomChanged.Broadcast();
		}
		else if (StatusCode == 404 &&
			(Operation == EProjectProject01LobbyOperation::LeaveRoom ||
				Operation == EProjectProject01LobbyOperation::DeleteRoom))
		{
			// 서버에서 이미 시간 초과 정리된 방은 로컬에서도 나간 것으로 처리한다.
			CurrentRoom = FProjectProject01RoomState();
			ClearGameJoinTicket();
			OnCurrentRoomChanged.Broadcast();
			Complete(Operation, true, TEXT("이미 정리된 방에서 나왔습니다."));
			return;
		}
		Complete(Operation, false, Message);
		// Bot/observer endpoints answer 503 with a message while the feature or its game server is not
		// ready. That is a feature state, not a dead backend: show it instead of logging the player out.
		const bool bFeatureUnavailable = StatusCode == 503 && bJsonValid &&
			(Operation == EProjectProject01LobbyOperation::CreateBotMatch ||
			 Operation == EProjectProject01LobbyOperation::EditRoomBot ||
			 Operation == EProjectProject01LobbyOperation::EditRoomParticipant);
		if (!bFeatureUnavailable && (StatusCode == 401 || StatusCode == 408 || StatusCode >= 500))
		{
			if (UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
			{
				if (UProjectProject01AuthSubsystem* Auth =
					GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>(); IsValid(Auth))
				{
					Auth->InvalidateSessionFromServerFailure(StatusCode == 401
						? TEXT("로그인 세션이 종료되었습니다. 다시 로그인하세요.")
						: TEXT("백엔드 서버를 사용할 수 없습니다. 다시 로그인하세요."));
				}
			}
		}
		return;
	}

	FString ParseError;
	bool bParsed = true;
	if (Operation==EProjectProject01LobbyOperation::JoinMatchmaking||Operation==EProjectProject01LobbyOperation::PollMatchmaking||Operation==EProjectProject01LobbyOperation::CancelMatchmaking)
    {
        bParsed=ParseMatchmaking(Json,ParseError);
    }
    else if (Operation == EProjectProject01LobbyOperation::RefreshRooms)
	{
		bParsed = ParseRoomList(Json, ParseError);
	}
	else if (Operation == EProjectProject01LobbyOperation::RefreshLeaderboard)
	{
        if(Endpoint!=DesiredLeaderboardEndpoint){Complete(Operation,true,TEXT("선택한 리더보드를 다시 불러오는 중입니다."));return;}
		bParsed = ParseLeaderboard(Json, ParseError);
	}
	else if (Operation == EProjectProject01LobbyOperation::RequestGameTicket)
	{
		bParsed = ParseGameJoinTicket(Json, ParseError);
	}
	else if (Operation == EProjectProject01LobbyOperation::LeaveRoom ||
		Operation == EProjectProject01LobbyOperation::DeleteRoom)
	{
		CurrentRoom = FProjectProject01RoomState();
		ClearGameJoinTicket();
		OnCurrentRoomChanged.Broadcast();
	}
	else if (Operation == EProjectProject01LobbyOperation::SubmitLeaderboardRecord)
	{
		// 등록 응답에는 방 또는 리더보드 배열이 없으며, UI가 성공 후 목록을 새로 요청한다.
		ClearPendingLeaderboardRecord();
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
    const TSharedPtr<FJsonObject>* Rules=nullptr;
    if(StateJson->HasField(TEXT("customRules"))&&(!StateJson->TryGetObjectField(TEXT("customRules"),Rules)||!FProjectCustomMatchRules::Parse(*Rules,Parsed.CustomRules)))
    {OutError=TEXT("방 규칙 응답이 올바르지 않습니다.");return false;}
	if (!ProjectProject01Lobby::ReadRequiredString(StateJson, TEXT("roomId"), Parsed.RoomId) ||
		!ProjectProject01Lobby::ReadRequiredString(StateJson, TEXT("joinCode"), Parsed.JoinCode) ||
		!ProjectProject01Lobby::ReadRequiredString(StateJson, TEXT("name"), Parsed.Name) ||
		!StateJson->TryGetBoolField(TEXT("isHost"), Parsed.bIsHost) ||
		!StateJson->TryGetBoolField(TEXT("isReady"), Parsed.bIsReady) ||
		!StateJson->TryGetBoolField(TEXT("canStart"), Parsed.bCanStart) ||
		!StateJson->TryGetBoolField(TEXT("gameServerOnline"), Parsed.bGameServerOnline) ||
		!StateJson->TryGetBoolField(TEXT("starting"), Parsed.bStarting) ||
		!StateJson->TryGetBoolField(TEXT("started"), Parsed.bStarted))
	{
		OutError = TEXT("방 상태의 필수 값이 올바르지 않습니다.");
		return false;
	}
	StateJson->TryGetBoolField(TEXT("returnedToRoom"), Parsed.bReturnedToRoom);
	StateJson->TryGetStringField(TEXT("matchId"), Parsed.MatchId);
	StateJson->TryGetBoolField(TEXT("serverPreparing"), Parsed.bServerPreparing);
	StateJson->TryGetStringField(TEXT("sessionWaitPhase"), Parsed.SessionWaitPhase);
	double WaitSeconds=0;
	if (StateJson->TryGetNumberField(TEXT("sessionWaitElapsedSeconds"),WaitSeconds) && FMath::IsFinite(WaitSeconds))
		Parsed.SessionWaitElapsedSeconds=static_cast<int32>(FMath::Clamp(WaitSeconds,0.0,3600.0));
    StateJson->TryGetBoolField(TEXT("randomMatch"),Parsed.bRandomMatch);
    StateJson->TryGetBoolField(TEXT("randomParty"),Parsed.bRandomParty);
    StateJson->TryGetStringField(TEXT("matchSearchId"),Parsed.MatchSearchId);
    StateJson->TryGetBoolField(TEXT("botMatch"),Parsed.bBotMatch);
    StateJson->TryGetBoolField(TEXT("isObserver"),Parsed.bIsObserver);
    const TArray<TSharedPtr<FJsonValue>>* BotValues=nullptr;
    if(StateJson->TryGetArrayField(TEXT("bots"),BotValues)&&BotValues)
    {
        if(BotValues->Num()>3){OutError=TEXT("봇 정원 응답이 올바르지 않습니다.");return false;}
        TSet<FString> Ids;
        for(const auto& Value:*BotValues)
        {
            auto BotJson=Value.IsValid()&&Value->Type==EJson::Object?Value->AsObject():nullptr;FProjectProject01RoomBot Bot;double Level=0;FGuid Id;
            if(!ProjectProject01Lobby::ReadRequiredString(BotJson,TEXT("botId"),Bot.BotId)||!FGuid::Parse(Bot.BotId,Id)||!Id.IsValid()||Ids.Contains(Id.ToString(EGuidFormats::DigitsWithHyphensLower))||
                !ProjectProject01Lobby::ReadRequiredString(BotJson,TEXT("displayName"),Bot.DisplayName)||Bot.DisplayName.Len()>48||
                !BotJson->TryGetNumberField(TEXT("difficulty"),Level)||!FMath::IsFinite(Level)||Level<1||Level>5||Level!=static_cast<int32>(Level))
            {OutError=TEXT("봇 구성 응답이 올바르지 않습니다.");return false;}
            Bot.BotId=Id.ToString(EGuidFormats::DigitsWithHyphensLower);Ids.Add(Bot.BotId);Bot.Difficulty=static_cast<int32>(Level);BotJson->TryGetStringField(TEXT("assignedRole"),Bot.AssignedRole);
            if(!Bot.AssignedRole.IsEmpty()&&Bot.AssignedRole!=TEXT("Mannequin")&&Bot.AssignedRole!=TEXT("Survivor"))
            {OutError=TEXT("봇 역할 응답이 올바르지 않습니다.");return false;}
            Parsed.Bots.Add(MoveTemp(Bot));
        }
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
		MemberJson->TryGetBoolField(TEXT("returnedToRoom"), Member.bReturnedToRoom);
        MemberJson->TryGetBoolField(TEXT("isObserver"),Member.bIsObserver);
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

    const bool bPreviouslyConfirmed=CurrentRoom.bRandomMatch;
	if (CurrentRoom.RoomId != Parsed.RoomId || CurrentRoom.MatchId != Parsed.MatchId ||
		!Parsed.bStarted || Parsed.bReturnedToRoom) ClearGameJoinTicket();
	CurrentRoom = MoveTemp(Parsed);
    bMatchmaking=CurrentRoom.bRandomParty&&!CurrentRoom.MatchSearchId.IsEmpty();
    if(bMatchmaking)MatchSearchId=CurrentRoom.MatchSearchId;else bCancelMatchmaking=false;
    if(bPreviouslyConfirmed&&!CurrentRoom.bRandomMatch&&!bMatchmaking)MatchmakingMessage.Reset();
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
			!ProjectProject01Lobby::ReadRequiredString(EntryJson, TEXT("roomName"), Entry.RoomName) ||
			!ProjectProject01Lobby::ReadRequiredString(EntryJson, TEXT("role"), Entry.Role) ||
			!EntryJson->TryGetBoolField(TEXT("isTeam"), Entry.bIsTeam) ||
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
		if (EntryJson->TryGetNumberField(TEXT("survivorCount"), Number)) Entry.SurvivorCount = FMath::Max(0, FMath::RoundToInt(Number));
		// API 필드명은 기존 패키지 호환을 위해 유지하지만 값은 팀 전체 구출 횟수입니다.
		if (EntryJson->TryGetNumberField(TEXT("averageRescueCount"), Number)) Entry.TeamRescueCount = FMath::Max(0.0, Number);
		// API 필드명은 기존 패키지 호환을 위해 유지하지만 값은 마지막 생존자의 탈출 시간입니다.
		if (EntryJson->TryGetNumberField(TEXT("averageEscapeSeconds"), Number)) Entry.LastEscapeSeconds = FMath::Max(0.0, Number);
		ParsedEntries.Add(MoveTemp(Entry));
	}
	LeaderboardEntries = MoveTemp(ParsedEntries);
	OnLeaderboardChanged.Broadcast();
	OutError.Reset();
	return true;
}

bool UProjectProject01LobbySubsystem::ParseGameJoinTicket(
	const TSharedPtr<FJsonObject>& Json,
	FString& OutError)
{
	FString ParsedTicket;
	FString ParsedKey;
	FString ParsedTravelUrl;
	FString ParsedRoomId;
	FString ParsedMatchId;
	FString ParsedRole;
	if (!ProjectProject01Lobby::ReadRequiredString(Json, TEXT("ticket"), ParsedTicket) ||
		!ProjectProject01Lobby::ReadRequiredString(Json, TEXT("encryptionKey"), ParsedKey) ||
		!ProjectProject01Lobby::ReadRequiredString(Json, TEXT("travelUrl"), ParsedTravelUrl) ||
		!ProjectProject01Lobby::ReadRequiredString(Json, TEXT("roomId"), ParsedRoomId) ||
		!ProjectProject01Lobby::ReadRequiredString(Json, TEXT("matchId"), ParsedMatchId) ||
		!ProjectProject01Lobby::ReadRequiredString(Json, TEXT("role"), ParsedRole))
	{
		OutError = TEXT("게임 접속 티켓 응답의 필수 값이 없습니다.");
		ClearGameJoinTicket();
		return false;
	}

	if (!CurrentRoom.IsValid() || !CurrentRoom.bStarted || CurrentRoom.bReturnedToRoom ||
		CurrentRoom.RoomId != ParsedRoomId || (!CurrentRoom.MatchId.IsEmpty() && CurrentRoom.MatchId != ParsedMatchId) ||
		!CurrentRoom.AssignedRole.Equals(ParsedRole, ESearchCase::IgnoreCase) || CurrentRoom.TravelUrl != ParsedTravelUrl)
	{
		OutError = TEXT("현재 방·경기와 일치하지 않는 접속 허가를 폐기했습니다.");
		ClearGameJoinTicket();
		return false;
	}
	UProjectProject01GameInstance* ProjectGameInstance = Cast<UProjectProject01GameInstance>(GetGameInstance());
	if (!IsValid(ProjectGameInstance) || !ProjectGameInstance->ConfigurePendingGameConnection(
		ParsedTicket, ParsedKey, ParsedRoomId, ParsedMatchId, ParsedRole))
	{
		OutError = TEXT("게임 접속 티켓 또는 AES-GCM 키가 올바르지 않습니다.");
		ClearGameJoinTicket();
		return false;
	}

	GameJoinTicket = MoveTemp(ParsedTicket);
	GameEncryptionKeyBase64 = MoveTemp(ParsedKey);
	GameTicketTravelUrl = MoveTemp(ParsedTravelUrl);
	GameTicketRoomId = MoveTemp(ParsedRoomId);
	GameTicketMatchId = MoveTemp(ParsedMatchId);
	GameTicketRole = MoveTemp(ParsedRole);
	OutError.Reset();
	return true;
}

bool UProjectProject01LobbySubsystem::HasGameJoinTicketForCurrentRoom() const
{
	return HasPendingGameJoinTicket() && CurrentRoom.IsValid() && CurrentRoom.bStarted && !CurrentRoom.bReturnedToRoom &&
		CurrentRoom.RoomId == GameTicketRoomId && (CurrentRoom.MatchId.IsEmpty() || CurrentRoom.MatchId == GameTicketMatchId) &&
		CurrentRoom.AssignedRole.Equals(GameTicketRole, ESearchCase::IgnoreCase) && CurrentRoom.TravelUrl == GameTicketTravelUrl;
}

void UProjectProject01LobbySubsystem::ClearGameJoinTicket()
{
	if (UProjectProject01GameInstance* ProjectGameInstance = Cast<UProjectProject01GameInstance>(GetGameInstance()))
	{
		ProjectGameInstance->ClearPendingGameConnection();
	}
	GameJoinTicket.Reset();
	GameEncryptionKeyBase64.Reset();
	GameTicketTravelUrl.Reset();
	GameTicketRoomId.Reset();
	GameTicketMatchId.Reset();
	GameTicketRole.Reset();
}

void UProjectProject01LobbySubsystem::Complete(
	const EProjectProject01LobbyOperation Operation,
	const bool bSuccess,
	const FString& Message)
{
	OnRequestCompleted.Broadcast(Operation, bSuccess, Message);
    if(bLeaderboardRefreshQueued&&!IsRequestInFlight()&&!DesiredLeaderboardEndpoint.IsEmpty())
    {
        bLeaderboardRefreshQueued=false;
        SendRequest(EProjectProject01LobbyOperation::RefreshLeaderboard,TEXT("GET"),DesiredLeaderboardEndpoint,FString());
    }
}
