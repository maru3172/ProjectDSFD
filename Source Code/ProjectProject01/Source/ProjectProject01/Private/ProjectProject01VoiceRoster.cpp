#include "ProjectProject01VoiceChatSubsystem.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UProjectProject01VoiceChatSubsystem::ApplyParticipantNames(const TSharedPtr<FJsonObject>& Json)
{
	const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
	if(!Json || !Json->TryGetArrayField(TEXT("participants"),Rows) || Rows->Num()>8)return;
	TMap<FString,FString> Names;
	TMap<FString,FString> Accounts;
	for(const auto& Value:*Rows)
	{
		const TSharedPtr<FJsonObject>* Row=nullptr;FString Id,Name;
		if(!Value || !Value->TryGetObject(Row) || !(*Row)->TryGetStringField(TEXT("productUserId"),Id) ||
			!(*Row)->TryGetStringField(TEXT("displayName"),Name))continue;
		bool bValidId=Id.Len()==32;
		for(TCHAR C:Id)if(!FChar::IsHexDigit(C))bValidId=false;
		if(!bValidId)continue;
		FString Account;
		if((*Row)->TryGetStringField(TEXT("userId"),Account) && !Account.IsEmpty() && Account.Len()<=20 && Account.IsNumeric())Accounts.Add(Id.ToLower(),Account);
		Name.ReplaceInline(TEXT("\r"),TEXT(" "));Name.ReplaceInline(TEXT("\n"),TEXT(" "));Name.ReplaceInline(TEXT("\t"),TEXT(" "));
		Name=Name.TrimStartAndEnd().Left(48);
		if(!Name.IsEmpty())Names.Add(Id.ToLower(),Name);
	}
	ParticipantNames=MoveTemp(Names);
	ParticipantAccountIds=MoveTemp(Accounts);
}

FString UProjectProject01VoiceChatSubsystem::GetParticipantDisplayName(const FString& Id) const
{
	if(const FString* Name=ParticipantNames.Find(Id.ToLower()))return *Name;
	return FString::Printf(TEXT("동료 생존자 (%s)"),*Id.Right(6));
}

void UProjectProject01VoiceChatSubsystem::RefreshParticipantNames()
{
	if(!bConfiguredForSurvivor || !bJoinedChannel || RosterRequest.IsValid())return;
	auto* Auth=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>():nullptr;
	if(!Auth || !Auth->IsSignedIn())return;
	const auto Request=FHttpModule::Get().CreateRequest();
	Request->SetURL(Auth->GetApiBaseUrlForAuthenticatedRequest()+TEXT("/api/voice/participants?matchId=")+MatchId);
	Request->SetVerb(TEXT("GET"));Request->SetTimeout(5.0f);
	Request->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Auth->GetAccessTokenForAuthenticatedRequest());
	FProjectProject01VersionContract::ApplyToRequest(Request);
	RosterRequest=Request;
	const uint64 Generation=SessionGeneration;
	TWeakObjectPtr<UProjectProject01VoiceChatSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda([WeakThis,Generation](FHttpRequestPtr Completed,FHttpResponsePtr Response,bool bSucceeded)
	{
		auto* Self=WeakThis.Get();
		if(!Self || !Self->IsCurrentSession(Generation) || Self->RosterRequest!=Completed)return;
		Self->RosterRequest.Reset();
		if(!bSucceeded || !Response.IsValid() || Response->GetResponseCode()!=200)return;
		TSharedPtr<FJsonObject> Json;
		if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json)||!Json)return;
		Self->ApplyParticipantNames(Json);
		bool bNeedsRefresh=false;
		if(Json->TryGetBoolField(TEXT("needsIdentityRefresh"),bNeedsRefresh) && bNeedsRefresh)Self->RequestJoinCredential(true);
	});
	if(!Request->ProcessRequest())RosterRequest.Reset();
}
