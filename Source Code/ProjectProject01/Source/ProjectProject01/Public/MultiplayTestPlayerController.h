// File: Source/ProjectProject01/Public/MultiplayTestPlayerController.h
// Build target: ProjectProject01Server / ProjectProject01

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MultiplayTestPlayerController.generated.h"

struct FProjectProject01ValidatedJoinClaim;

UENUM(BlueprintType)
enum class EMultiplayTestMatchPhase : uint8
{
	Waiting,
	RoleAssignment,
	InProgress,
	Ending,
	Results,
	ReturningToRoom
};

UENUM(BlueprintType)
enum class EMultiplayTestSurvivorState : uint8
{
	NotApplicable,
	Active,
	Eliminated,
	Escaped
};

/** 서버가 확정하여 해당 소유 클라이언트에만 전달하는 한 경기의 결과입니다. */
USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FMultiplayTestMatchResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString MatchId;

	UPROPERTY(BlueprintReadOnly)
	FString Role;

	UPROPERTY(BlueprintReadOnly)
	FString Outcome;

	UPROPERTY(BlueprintReadOnly)
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly)
	bool bLeaderboardVerificationReady = false;

	UPROPERTY(BlueprintReadOnly)
	double MatchDurationSeconds = 0.0;

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
};

/** 마네킹 슬롯 선택 입력을 서버에 요청하는 MultiplayTest 전용 컨트롤러입니다. */
UCLASS()
class PROJECTPROJECT01_API AMultiplayTestPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	class AMannequinAICharacter* GetViewedMannequin() const;
	void SetViewedMannequin(class AMannequinAICharacter* Mannequin);
	void SetAuthenticatedLobbyIdentity(const FProjectProject01ValidatedJoinClaim& Claim);
	void SetServerMatchState(EMultiplayTestMatchPhase NewPhase, EMultiplayTestSurvivorState NewSurvivorState);
	void DeliverMatchResultToOwner(const FMultiplayTestMatchResult& Result);
	const FString& GetAuthenticatedUserId() const { return AuthenticatedUserId; }
	const FString& GetAuthenticatedMatchId() const { return AuthenticatedMatchId; }
	const FString& GetAuthenticatedRole() const { return AuthenticatedRole; }
	EMultiplayTestMatchPhase GetMatchPhase() const { return MatchPhase; }
	EMultiplayTestSurvivorState GetSurvivorState() const { return SurvivorState; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnRep_Pawn() override;

private:
	void SelectMannequinSlot0();
	void SelectMannequinSlot1();
	void SelectMannequinSlot2();
	void SelectMannequinSlot3();
	void SelectMannequinSlot4();
	void SelectMannequinSlot5();
	void SelectMannequinSlot6();
	void SelectMannequinSlot7();
	void SelectMannequinSlot8();
	void SelectMannequinSlot9();
	void RequestMannequinSlot(int32 Slot);
	void RequestMannequinManualControl();
	void RequestPostPossessionChaseCommand();
	void ToggleSessionMenu();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestMannequinSlot(int32 Slot);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestMannequinManualControl();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestPostPossessionChaseCommand();

	UFUNCTION(Client, Reliable)
	void ClientApplyViewedMannequin(class AMannequinAICharacter* Mannequin);

	UFUNCTION(Client, Reliable)
	void ClientPresentMatchResult(const FMultiplayTestMatchResult& Result);

	UFUNCTION()
	void OnRep_ViewedMannequin();

	void ApplyViewedMannequinCamera(class AMannequinAICharacter* Mannequin);
	bool AllowServerRequest(uint8 RequestType, int32 MaximumCalls, float WindowSeconds);

	/** 소유 클라이언트가 Pawn 복제 이후에도 다시 적용할 수 있는 마지막 선택 시점 대상입니다. */
	UPROPERTY(ReplicatedUsing = OnRep_ViewedMannequin)
	TObjectPtr<class AMannequinAICharacter> ViewedMannequin;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01SessionMenuWidget> SessionMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01MatchResultWidget> MatchResultWidget;

	UPROPERTY(Replicated)
	EMultiplayTestMatchPhase MatchPhase = EMultiplayTestMatchPhase::Waiting;

	UPROPERTY(Replicated)
	EMultiplayTestSurvivorState SurvivorState = EMultiplayTestSurvivorState::NotApplicable;

	FString AuthenticatedUserId;
	FString AuthenticatedMatchId;
	FString AuthenticatedRole;
	float RpcWindowStartSeconds[3] = { 0.0f, 0.0f, 0.0f };
	int32 RpcWindowCallCount[3] = { 0, 0, 0 };
};
