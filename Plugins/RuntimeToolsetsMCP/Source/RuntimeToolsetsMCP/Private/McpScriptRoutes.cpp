#include "McpScriptRoutes.h"
#include "RuntimeToolsetsCore.h"
#include "Dom/JsonObject.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace McpScriptRoutes
{
	namespace
	{
		TSharedPtr<IHttpRouter> Router;
		TArray<FHttpRouteHandle> Handles;
		FString Session;

		TUniquePtr<FHttpServerResponse> Reply(const FString& Body, bool bOk)
		{
			TUniquePtr<FHttpServerResponse> R = FHttpServerResponse::Create(Body, TEXT("application/json"));
			R->Code = bOk ? EHttpServerResponseCodes::Ok : EHttpServerResponseCodes::BadRequest;
			return R;
		}

		FString Error(const FString& Message)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("ok"), false);
			O->SetStringField(TEXT("error"), Message);
			FString S;
			FJsonSerializer::Serialize(O, TJsonWriterFactory<>::Create(&S));
			return S;
		}

		/** The result JSON, with attached images as base64 (_images) so scripts can save screenshots. */
		FString ResultJson(const FToolResult& R)
		{
			FString Body = R.ToJsonString();
			if (R.Attachments.Num() == 0)
			{
				return Body;
			}
			TSharedPtr<FJsonObject> O;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), O) || !O.IsValid())
			{
				return Body;
			}
			TArray<TSharedPtr<FJsonValue>> Images;
			for (const FToolAttachment& A : R.Attachments)
			{
				TSharedRef<FJsonObject> I = MakeShared<FJsonObject>();
				I->SetStringField(TEXT("mimeType"), A.MimeType);
				I->SetStringField(TEXT("data"), FBase64::Encode(A.Bytes.GetData(), A.Bytes.Num()));
				Images.Add(MakeShared<FJsonValueObject>(I));
			}
			O->SetArrayField(TEXT("_images"), Images);
			FString Out;
			FJsonSerializer::Serialize(O.ToSharedRef(), TJsonWriterFactory<>::Create(&Out));
			return Out;
		}

		bool Handle(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, bool bBatch)
		{
			const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
			TSharedPtr<FJsonObject> In;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), In) || !In.IsValid())
			{
				OnComplete(Reply(Error(TEXT("body must be JSON: {\"tool\": \"build.create_primitive\", \"arguments\": {...}}")), false));
				return true;
			}
			FString Tool;
			TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			if (bBatch)
			{
				Tool = TEXT("ops.run_batch");
				for (const TCHAR* Field : {TEXT("calls"), TEXT("atomic"), TEXT("stop_on_error"), TEXT("label")})
				{
					if (TSharedPtr<FJsonValue> V = In->TryGetField(Field))
					{
						Args->SetField(Field, V);
					}
				}
			}
			else
			{
				In->TryGetStringField(TEXT("tool"), Tool);
				const TSharedPtr<FJsonObject>* A = nullptr;
				if (In->TryGetObjectField(TEXT("arguments"), A))
				{
					Args = *A;
				}
			}
			if (Tool.IsEmpty())
			{
				OnComplete(Reply(Error(TEXT("body needs \"tool\", e.g. {\"tool\": \"build.create_assembly\", \"arguments\": {\"parts\": [...]}}")), false));
				return true;
			}
			FToolCallerContext Caller;
			Caller.SessionId = Session; // the same undo history as the agent's MCP calls
			Caller.ClientName = TEXT("script");
			IRuntimeToolsetsCore::Get().Invoke(Tool, Args, Caller,
				FOnToolComplete::CreateLambda([OnComplete](const FToolResult& R) { OnComplete(Reply(ResultJson(R), R.IsOk())); }));
			return true;
		}
	}

	void Start(int32 Port, const FString& SessionId)
	{
		Session = SessionId;
		Router = FHttpServerModule::Get().GetHttpRouter(static_cast<uint32>(Port));
		if (!Router.IsValid())
		{
			return;
		}
		Handles.Add(Router->BindRoute(FHttpPath(TEXT("/call")), EHttpServerRequestVerbs::VERB_POST,
			FHttpRequestHandler::CreateLambda([](const FHttpServerRequest& R, const FHttpResultCallback& C) { return Handle(R, C, false); })));
		Handles.Add(Router->BindRoute(FHttpPath(TEXT("/batch")), EHttpServerRequestVerbs::VERB_POST,
			FHttpRequestHandler::CreateLambda([](const FHttpServerRequest& R, const FHttpResultCallback& C) { return Handle(R, C, true); })));
		FHttpServerModule::Get().StartAllListeners();
		SetScriptUrl(FString::Printf(TEXT("http://127.0.0.1:%d"), Port));
	}

	void Stop()
	{
		if (Router.IsValid())
		{
			for (const FHttpRouteHandle& H : Handles)
			{
				Router->UnbindRoute(H);
			}
		}
		Handles.Reset();
		Router.Reset();
	}

	void SetScriptUrl(const FString& BaseUrl)
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("RUNTIMETOOLSETS_SCRIPT_URL"), *BaseUrl);
	}
}
