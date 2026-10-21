#include "ProjectEditorLocalMatchServer.h"
#include "ProjectProject01LobbySubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace ProjectEditorLocalMatchServer
{
	using FCompletion=TFunction<void(bool,const FString&)>;
	static TArray<FCompletion> Waiting;
	static TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> Request;
	static const TCHAR* Endpoint=TEXT("http://127.0.0.1:5080/api/admin/game-server-demand");

	static void Finish(bool bOK,const FString& Message)
	{
		Request.Reset();
		auto Callbacks=MoveTemp(Waiting);Waiting.Reset();
		for (auto& Callback:Callbacks) Callback(bOK,Message);
	}
	static void Prepare(FCompletion Completion)
	{
		Waiting.Add(MoveTemp(Completion));
		if (Request) return; // Multiple PIE clients share one local preparation request.
		FString Text,Key;
		TSharedPtr<FJsonObject> Local;
		const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		const FString Config=Root/TEXT("Tools/ProjectProject01Backend/LocalMySql/ProjectProject01Backend.local.json");
		const TSharedPtr<FJsonObject>* Admin=nullptr;
		if (!FFileHelper::LoadFileToString(Text,*Config) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Local) ||
			!Local || !Local->TryGetObjectField(TEXT("Administrator"),Admin) || !Admin || !(*Admin)->TryGetStringField(TEXT("ApiKey"),Key) || Key.IsEmpty())
		{
			Finish(false,TEXT("로컬 에디터 서버 운영 자격을 찾지 못했습니다. 이 PC의 서버 도우미에서 MySQL 초기화/시작과 백엔드 시작을 먼저 실행하세요."));return;
		}
		const FString Editor=FPaths::ConvertRelativePathToFull(FPaths::EngineDir()/TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
		if (!FPaths::FileExists(Editor)) { Finish(false,TEXT("현재 엔진의 UnrealEditor-Cmd.exe를 찾지 못했습니다."));return; }
		auto Get=FHttpModule::Get().CreateRequest();Request=Get;
		Get->SetURL(Endpoint);Get->SetVerb(TEXT("GET"));Get->SetTimeout(5);
		Get->SetHeader(TEXT("X-ProjectProject01-Admin-Key"),Key);
		Get->OnProcessRequestComplete().BindLambda([Key,Root,Editor](FHttpRequestPtr,FHttpResponsePtr Response,bool bConnected)
		{
			TSharedPtr<FJsonObject> State;
			if (!bConnected || !Response || Response->GetResponseCode()!=200 ||
				!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),State) || !State)
			{ Finish(false,TEXT("로컬 백엔드 운영 상태를 확인하지 못했습니다. 백엔드 실행 및 운영 자격을 확인하세요. (5초 제한)"));return; }
			bool bEnabled=false;State->TryGetBoolField(TEXT("enabled"),bEnabled);
			// Never replace a running operator-selected server pool or cancel maintenance/draining.
			if (bEnabled) { Finish(true,TEXT("현재 운영 중인 서버 설정을 유지합니다."));return; }
			TSharedRef<FJsonObject> Config=MakeShared<FJsonObject>();
			Config->SetBoolField(TEXT("enabled"),true);Config->SetBoolField(TEXT("editorLocalTest"),true);
			Config->SetStringField(TEXT("executable"),Editor);Config->SetStringField(TEXT("projectRoot"),Root);
			Config->SetNumberField(TEXT("firstPort"),17777);Config->SetNumberField(TEXT("maximumConcurrent"),1);
			FString Body;FJsonSerializer::Serialize(Config,TJsonWriterFactory<>::Create(&Body));
			auto Put=FHttpModule::Get().CreateRequest();Request=Put;
			Put->SetURL(Endpoint);Put->SetVerb(TEXT("PUT"));Put->SetTimeout(5);
			Put->SetHeader(TEXT("X-ProjectProject01-Admin-Key"),Key);Put->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
			Put->SetContentAsString(Body);
			Put->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr,FHttpResponsePtr Response,bool bConnected)
			{
				const bool bOK=bConnected && Response && Response->GetResponseCode()==200;
				Finish(bOK,bOK?TEXT("에디터 로컬 경기 세션 실행 준비 완료"):
					TEXT("에디터 서버 운영 설정에 실패했습니다. 새 백엔드 코드로 재시작하고, 기존 경기 세션이 종료되었는지 확인하세요."));
			});
			if (!Put->ProcessRequest()) Finish(false,TEXT("에디터 서버 운영 설정 요청을 시작하지 못했습니다."));
		});
		if (!Get->ProcessRequest()) Finish(false,TEXT("로컬 백엔드 운영 확인을 시작하지 못했습니다."));
	}
	void Startup() { UProjectProject01LobbySubsystem::PrepareEditorLocalServer=&Prepare; }
	void Shutdown()
	{
		UProjectProject01LobbySubsystem::PrepareEditorLocalServer=nullptr;
		if (Request) { Request->OnProcessRequestComplete().Unbind();Request->CancelRequest(); }
		Finish(false,TEXT("에디터 종료로 로컬 서버 준비를 취소했습니다."));
	}
}
