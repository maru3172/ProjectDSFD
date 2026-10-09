// File: Source/ProjectProject01/Public/ProjectProject01LobbySubsystem.h
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectProject01LobbySubsystem.generated.h"

UENUM(BlueprintType)
enum class EProjectProject01LobbyOperation : uint8
{
	RefreshRooms,
	RefreshCurrentRoom,
	CreateRoom,
	JoinRoom,
	ReturnToRoom,
	LeaveRoom,
	TransferHost,
	DeleteRoom,
	SetReady,
	StartRoom,
	SendChat,
	RequestGameTicket,
	RefreshLeaderboard,
	SubmitLeaderboardRecord
};

UENUM(BlueprintType)
enum class EProjectProject01LeaderboardRole : uint8
{
	Mannequin,
	Survivor
};

UENUM(BlueprintType)
enum class EProjectProject01LeaderboardSort : uint8
{
	Overall,
	Captures,
	FirstCapture,
	AllCaptured,
	Rescues,
	Escape
};

USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FProjectProject01LeaderboardEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 Rank = 0;

	UPROPERTY(BlueprintReadOnly)
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly)
	FString Role;

	UPROPERTY(BlueprintReadOnly)
	double OverallScore = 0.0;

	UPROPERTY(BlueprintReadOnly)
	int32 CaptureCount = 0;

	UPROPERTY(BlueprintReadOnly)
	double FirstCaptureSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly)
	double AllCapturedSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly)
	int32 RescueCount = 0;

	UPROPERTY(BlueprintReadOnly)
	double EscapeSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly)
	FString CreatedAtUtc;
};

USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FProjectProject01RoomSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString RoomId;

	UPROPERTY(BlueprintReadOnly)
	FString JoinCode;

	UPROPERTY(BlueprintReadOnly)
	FString Name;

	UPROPERTY(BlueprintReadOnly)
	FString HostDisplayName;

	UPROPERTY(BlueprintReadOnly)
	int32 MemberCount = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 MaxPlayers = 3;

	UPROPERTY(BlueprintReadOnly)
	bool bHasPassword = false;
};

USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FProjectProject01RoomMember
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString UserId;

	UPROPERTY(BlueprintReadOnly)
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly)
	bool bIsHost = false;

	UPROPERTY(BlueprintReadOnly)
	bool bIsReady = false;

	UPROPERTY(BlueprintReadOnly)
	bool bReturnedToRoom = false;

	UPROPERTY(BlueprintReadOnly)
	FString AssignedRole;
};

USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FProjectProject01RoomChatMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString MessageId;

	UPROPERTY(BlueprintReadOnly)
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly)
	FString Message;

	UPROPERTY(BlueprintReadOnly)
	FString CreatedAtUtc;
};

USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FProjectProject01RoomState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString RoomId;

	UPROPERTY(BlueprintReadOnly)
	FString JoinCode;

	UPROPERTY(BlueprintReadOnly)
	FString Name;

	UPROPERTY(BlueprintReadOnly)
	bool bIsHost = false;

	UPROPERTY(BlueprintReadOnly)
	bool bIsReady = false;

	UPROPERTY(BlueprintReadOnly)
	bool bCanStart = false;

	UPROPERTY(BlueprintReadOnly)
	bool bStarted = false;

	/** 현재 로컬 사용자가 진행 중인 경기 연결을 끝내고 방 화면으로 복귀했는지 표시합니다. */
	UPROPERTY(BlueprintReadOnly)
	bool bReturnedToRoom = false;

	UPROPERTY(BlueprintReadOnly)
	FString TravelUrl;

	UPROPERTY(BlueprintReadOnly)
	FString AssignedRole;

	UPROPERTY(BlueprintReadOnly)
	TArray<FProjectProject01RoomMember> Members;

	UPROPERTY(BlueprintReadOnly)
	TArray<FProjectProject01RoomChatMessage> ChatMessages;

	bool IsValid() const { return !RoomId.IsEmpty(); }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FProjectProject01LobbyRequestResult,
	EProjectProject01LobbyOperation, Operation,
	bool, bSuccess,
	const FString&, Message);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FProjectProject01LobbyDataChanged);

UCLASS(Config=Game)
class PROJECTPROJECT01_API UProjectProject01LobbySubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category="ProjectProject01|Lobby")
	FProjectProject01LobbyRequestResult OnRequestCompleted;

	UPROPERTY(BlueprintAssignable, Category="ProjectProject01|Lobby")
	FProjectProject01LobbyDataChanged OnRoomListChanged;

	UPROPERTY(BlueprintAssignable, Category="ProjectProject01|Lobby")
	FProjectProject01LobbyDataChanged OnCurrentRoomChanged;

	UPROPERTY(BlueprintAssignable, Category="ProjectProject01|Leaderboard")
	FProjectProject01LobbyDataChanged OnLeaderboardChanged;

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void RefreshRooms();

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void RefreshCurrentRoom();

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void CreateRoom(const FString& RoomName, bool bIsPublic, const FString& Password);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void JoinRoom(const FString& RoomCode, const FString& Password);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void LeaveRoom();

	/** 게임 서버 접속만 끝내고 로그인과 현재 방 멤버십을 유지합니다. */
	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void ReturnToRoom();

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void TransferHost(const FString& TargetUserId);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void DeleteRoom();

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void SetReady(bool bReady);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void StartRoom();

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void SendChat(const FString& Message);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void RequestGameJoinTicket();

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Leaderboard")
	void RefreshLeaderboard(EProjectProject01LeaderboardRole Role, EProjectProject01LeaderboardSort Sort);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Leaderboard")
	void SubmitLeaderboardRecord(
		const FString& MatchId,
		EProjectProject01LeaderboardRole Role,
		bool bSuccess,
		int32 CaptureCount,
		double FirstCaptureSeconds,
		double AllCapturedSeconds,
		int32 RescueCount,
		double EscapeSeconds);

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Lobby")
	bool HasCurrentRoom() const { return CurrentRoom.IsValid(); }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Lobby")
	bool IsRequestInFlight() const { return ActiveRequest.IsValid(); }

	const TArray<FProjectProject01RoomSummary>& GetRooms() const { return Rooms; }
	const FProjectProject01RoomState& GetCurrentRoom() const { return CurrentRoom; }
	const TArray<FProjectProject01LeaderboardEntry>& GetLeaderboardEntries() const { return LeaderboardEntries; }
	EProjectProject01LeaderboardRole GetLeaderboardRole() const { return LeaderboardRole; }
	EProjectProject01LeaderboardSort GetLeaderboardSort() const { return LeaderboardSort; }
	bool HasPendingGameJoinTicket() const { return !GameJoinTicket.IsEmpty() && !GameEncryptionKeyBase64.IsEmpty(); }
	/** 로그인 화면에서 선택한 백엔드 주소를 로비·리더보드 요청에도 동일하게 적용합니다. */
	bool ConfigureLocalTestApiEndpoint(const FString& InApiBaseUrl);
	const FString& GetGameJoinTicket() const { return GameJoinTicket; }
	const FString& GetGameEncryptionKeyBase64() const { return GameEncryptionKeyBase64; }
	const FString& GetGameTicketTravelUrl() const { return GameTicketTravelUrl; }
	const FString& GetGameTicketRoomId() const { return GameTicketRoomId; }
	const FString& GetGameTicketMatchId() const { return GameTicketMatchId; }
	const FString& GetGameTicketRole() const { return GameTicketRole; }

private:
	void SendRequest(EProjectProject01LobbyOperation Operation, const FString& Verb,
		const FString& Endpoint, const FString& JsonBody, int32 RetryIndex = 0);
	void HandleRequestComplete(
		TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<class IHttpResponse, ESPMode::ThreadSafe> Response,
		bool bConnectedSuccessfully,
		EProjectProject01LobbyOperation Operation,
		FString Verb,
		FString Endpoint,
		FString JsonBody,
		int32 RetryIndex);
	bool ParseRoomList(const TSharedPtr<class FJsonObject>& Json, FString& OutError);
	bool ParseRoomState(const TSharedPtr<class FJsonObject>& Json, FString& OutError);
	bool ParseLeaderboard(const TSharedPtr<class FJsonObject>& Json, FString& OutError);
	bool ParseGameJoinTicket(const TSharedPtr<class FJsonObject>& Json, FString& OutError);
	void ClearGameJoinTicket();
	void Complete(EProjectProject01LobbyOperation Operation, bool bSuccess, const FString& Message);

	UPROPERTY(Config)
	FString ApiBaseUrl = TEXT("http://127.0.0.1:5080");

	/** Hamachi 등 암호화된 개발 VPN에서만 허용할 정확한 HTTP API 주소입니다. 운영 배포에서는 비워 둡니다. */
	UPROPERTY(Config)
	FString AllowedInsecureVpnApiBaseUrl;

	UPROPERTY(Config)
	float RequestTimeoutSeconds = 10.0f;

	UPROPERTY(Transient)
	TArray<FProjectProject01RoomSummary> Rooms;

	UPROPERTY(Transient)
	FProjectProject01RoomState CurrentRoom;

	UPROPERTY(Transient)
	TArray<FProjectProject01LeaderboardEntry> LeaderboardEntries;

	EProjectProject01LeaderboardRole LeaderboardRole = EProjectProject01LeaderboardRole::Mannequin;
	EProjectProject01LeaderboardSort LeaderboardSort = EProjectProject01LeaderboardSort::Overall;

	FString GameJoinTicket;
	FString GameEncryptionKeyBase64;
	FString GameTicketTravelUrl;
	FString GameTicketRoomId;
	FString GameTicketMatchId;
	FString GameTicketRole;

	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;
};
