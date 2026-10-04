// File: Source/ProjectProject01/Public/ProjectProject01FrontendGameMode.h
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "ProjectProject01FrontendGameMode.generated.h"

UCLASS()
class PROJECTPROJECT01_API AProjectProject01TitlePlayerController final : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01TitleWidget> TitleWidget;
};

UCLASS()
class PROJECTPROJECT01_API AProjectProject01TitleGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	AProjectProject01TitleGameMode();
};

UCLASS()
class PROJECTPROJECT01_API AProjectProject01LoginPlayerController final : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01LoginWidget> LoginWidget;
};

UCLASS()
class PROJECTPROJECT01_API AProjectProject01LoginGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	AProjectProject01LoginGameMode();
};

UCLASS()
class PROJECTPROJECT01_API AProjectProject01LobbyPlayerController final : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01LobbyWidget> LobbyWidget;
};

UCLASS()
class PROJECTPROJECT01_API AProjectProject01LobbyGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	AProjectProject01LobbyGameMode();
};

UCLASS()
class PROJECTPROJECT01_API AProjectProject01LeaderboardPlayerController final : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01LeaderboardWidget> LeaderboardWidget;
};

UCLASS()
class PROJECTPROJECT01_API AProjectProject01LeaderboardGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	AProjectProject01LeaderboardGameMode();
};
