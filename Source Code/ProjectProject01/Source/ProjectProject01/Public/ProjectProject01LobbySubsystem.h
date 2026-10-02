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
	LeaveRoom,
	SetReady,
	StartRoom,
	SendChat
};

USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FProjectProject01RoomSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString RoomId;

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
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly)
	bool bIsHost = false;

	UPROPERTY(BlueprintReadOnly)
	bool bIsReady = false;

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
	FString Name;

	UPROPERTY(BlueprintReadOnly)
	bool bIsHost = false;

	UPROPERTY(BlueprintReadOnly)
	bool bIsReady = false;

	UPROPERTY(BlueprintReadOnly)
	bool bCanStart = false;

	UPROPERTY(BlueprintReadOnly)
	bool bStarted = false;

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

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void RefreshRooms();

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void RefreshCurrentRoom();

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void CreateRoom(const FString& RoomName, bool bIsPublic, const FString& Password);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void JoinRoom(const FString& RoomId, const FString& Password);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void LeaveRoom();

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void SetReady(bool bReady);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void StartRoom();

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Lobby")
	void SendChat(const FString& Message);

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Lobby")
	bool HasCurrentRoom() const { return CurrentRoom.IsValid(); }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Lobby")
	bool IsRequestInFlight() const { return ActiveRequest.IsValid(); }

	const TArray<FProjectProject01RoomSummary>& GetRooms() const { return Rooms; }
	const FProjectProject01RoomState& GetCurrentRoom() const { return CurrentRoom; }

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
	void Complete(EProjectProject01LobbyOperation Operation, bool bSuccess, const FString& Message);

	UPROPERTY(Config)
	FString ApiBaseUrl = TEXT("http://127.0.0.1:5080");

	UPROPERTY(Config)
	float RequestTimeoutSeconds = 10.0f;

	UPROPERTY(Transient)
	TArray<FProjectProject01RoomSummary> Rooms;

	UPROPERTY(Transient)
	FProjectProject01RoomState CurrentRoom;

	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;
};
