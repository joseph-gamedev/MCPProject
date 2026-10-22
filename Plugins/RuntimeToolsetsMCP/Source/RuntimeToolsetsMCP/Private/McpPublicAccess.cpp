#include "McpPublicAccess.h"
#include "RuntimeToolsetsMCPSettings.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogMcpRelay, Log, All);

namespace McpPublicAccessPrivate
{
	FString Relay;      // https://unreal-mcp-relay.<account>.workers.dev
	FString HostToken;
	FString SessionCode; // 6 characters shown in the game
	FString LocalUrl;   // http://127.0.0.1:8018/mcp
	bool bRunning = false;
	bool bOnline = false;
	int32 Failures = 0;
	FTSTicker::FDelegateHandle OverlayTick;
	TSharedPtr<SWidget> Overlay;
	TWeakObjectPtr<UGameViewportClient> OverlayViewport;

	FString MakeCode()
	{
		static const TCHAR* Alphabet = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
		FRandomStream R(static_cast<int32>(FPlatformTime::Cycles64() ^ FPlatformProcess::GetCurrentProcessId()));
		FString C;
		for (int32 i = 0; i < 6; ++i)
		{
			C.AppendChar(Alphabet[R.RandRange(0, 31)]);
		}
		return C;
	}

	void Poll();

	void PostResult(const FString& Id, int32 Status, const TSharedRef<FJsonObject>& Headers, const FString& Body)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("status"), Status);
		Out->SetObjectField(TEXT("headers"), Headers);
		Out->SetStringField(TEXT("body"), Body);
		FString Text;
		FJsonSerializer::Serialize(Out, TJsonWriterFactory<>::Create(&Text));
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(FString::Printf(TEXT("%s/host/%s/result/%s"), *Relay, *SessionCode, *Id));
		Req->SetVerb(TEXT("POST"));
		Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + HostToken);
		Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Req->SetContentAsString(Text);
		Req->SetTimeout(30.f);
		Req->ProcessRequest();
	}

	/** Runs one agent request against the local MCP server and sends the answer back to the relay. */
	void RunJob(const TSharedPtr<FJsonObject>& Job)
	{
		const FString Id = Job->GetStringField(TEXT("id"));
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(LocalUrl);
		Req->SetVerb(Job->HasField(TEXT("method")) ? Job->GetStringField(TEXT("method")) : TEXT("POST"));
		const TSharedPtr<FJsonObject>* Headers = nullptr;
		if (Job->TryGetObjectField(TEXT("headers"), Headers))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& H : (*Headers)->Values)
			{
				Req->SetHeader(H.Key, H.Value->AsString());
			}
		}
		Req->SetContentAsString(Job->GetStringField(TEXT("body")));
		Req->SetTimeout(115.f);
		Req->OnProcessRequestComplete().BindLambda([Id](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			if (!bOk || !Resp.IsValid())
			{
				Out->SetStringField(TEXT("content-type"), TEXT("application/json"));
				PostResult(Id, 502, Out, TEXT("{\"error\":\"the game's MCP server did not answer\"}"));
				return;
			}
			for (const TCHAR* Name : {TEXT("Content-Type"), TEXT("Mcp-Session-Id")})
			{
				const FString V = Resp->GetHeader(Name);
				if (!V.IsEmpty())
				{
					Out->SetStringField(FString(Name).ToLower(), V);
				}
			}
			PostResult(Id, Resp->GetResponseCode(), Out, Resp->GetContentAsString());
		});
		Req->ProcessRequest();
	}

	void Poll()
	{
		if (!bRunning)
		{
			return;
		}
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(FString::Printf(TEXT("%s/host/%s/poll"), *Relay, *SessionCode));
		Req->SetVerb(TEXT("GET"));
		Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + HostToken);
		Req->SetTimeout(40.f);
		Req->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			if (!bRunning)
			{
				return;
			}
			const int32 Status = bOk && Resp.IsValid() ? Resp->GetResponseCode() : 0;
			if (Status == 200 || Status == 204)
			{
				if (!bOnline)
				{
					UE_LOG(LogMcpRelay, Display, TEXT("Relay connected. AI code: %s"), *SessionCode);
				}
				bOnline = true;
				Failures = 0;
				TSharedPtr<FJsonObject> Job;
				if (Status == 200 && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Resp->GetContentAsString()), Job) && Job.IsValid())
				{
					RunJob(Job);
				}
				Poll();
				return;
			}
			bOnline = false;
			if (++Failures == 1 || Failures % 20 == 0)
			{
				UE_LOG(LogMcpRelay, Warning, TEXT("Relay poll failed (%d, try %d): %s"), Status, Failures, Status == 401 ? TEXT("wrong RelayHostToken") : TEXT("relay unreachable?"));
			}
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { Poll(); return false; }), FMath::Min(10.f, 1.f + Failures * 0.5f));
		});
		Req->ProcessRequest();
	}

	void ShowOverlay()
	{
		if (Overlay.IsValid() || !GEngine || !GEngine->GameViewport || !FSlateApplication::IsInitialized())
		{
			return;
		}
		static const FSlateRoundedBoxBrush Brush(FLinearColor(0.02f, 0.03f, 0.05f, 0.7f), 6.f);
		TSharedRef<SWidget> W = SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f)[SNullWidget::NullWidget]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(14.f, 0.f, 0.f, 12.f))
			[
				SNew(SBorder)
				.BorderImage(&Brush)
				.Padding(FMargin(10.f, 5.f))
				.Visibility(EVisibility::HitTestInvisible)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
					.ColorAndOpacity_Lambda([]() { return FSlateColor(bOnline ? FLinearColor(0.55f, 0.9f, 1.f) : FLinearColor(1.f, 0.6f, 0.4f)); })
					.Text_Lambda([]() { return FText::FromString(FString::Printf(TEXT("AI code: %s%s"), *SessionCode, bOnline ? TEXT("") : TEXT("  (connecting...)"))); })
				]
			];
		GEngine->GameViewport->AddViewportWidgetContent(W, 1000);
		Overlay = W;
		OverlayViewport = GEngine->GameViewport;
	}

	FString RelayUrl()
	{
		FString Url = GetDefault<URuntimeToolsetsMCPSettings>()->RelayUrl;
		FParse::Value(FCommandLine::Get(), TEXT("RuntimeToolsetsRelay="), Url);
		return Url;
	}
}

bool McpPublicAccess::IsEnabled()
{
	using namespace McpPublicAccessPrivate;
	if (RelayUrl().IsEmpty() || FParse::Param(FCommandLine::Get(), TEXT("NoRuntimeToolsetsRelay")))
	{
		return false;
	}
	const bool bAsked = FString(FCommandLine::Get()).Contains(TEXT("RuntimeToolsetsRelay"));
#if WITH_EDITOR
	return bAsked;
#else
	return bAsked || GetDefault<URuntimeToolsetsMCPSettings>()->bRelayInPackagedBuilds;
#endif
}

void McpPublicAccess::Start(int32 Port, const FString& Path)
{
	using namespace McpPublicAccessPrivate;
	if (!IsEnabled())
	{
		return;
	}
	const URuntimeToolsetsMCPSettings& S = *GetDefault<URuntimeToolsetsMCPSettings>();
	Relay = RelayUrl();
	Relay.RemoveFromEnd(TEXT("/"));
	HostToken = S.RelayHostToken;
	FParse::Value(FCommandLine::Get(), TEXT("RuntimeToolsetsHostToken="), HostToken);
	FParse::Value(FCommandLine::Get(), TEXT("RuntimeToolsetsCode="), SessionCode);
	SessionCode = SessionCode.IsEmpty() ? MakeCode() : SessionCode.ToUpper();
	LocalUrl = FString::Printf(TEXT("http://127.0.0.1:%d%s"), Port, *Path);
	bRunning = true;
	// session_get_build_info reports this, so agents find the Python client on their first call.
	FPlatformMisc::SetEnvironmentVar(TEXT("RUNTIMETOOLSETS_SCRIPT_CLIENT"), *FString::Printf(TEXT("%s/s/%s/client.py"), *Relay, *SessionCode));
	FFileHelper::SaveStringToFile(SessionCode + LINE_TERMINATOR, *(FPaths::ProjectSavedDir() / TEXT("RuntimeToolsets/code.txt")));
	UE_LOG(LogMcpRelay, Display, TEXT("Agent relay %s, AI code %s"), *Relay, *SessionCode);
	Poll();
	if (S.bShowConnectOverlay)
	{
		OverlayTick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			ShowOverlay();
			return !Overlay.IsValid();
		}), 0.5f);
	}
}

void McpPublicAccess::Stop()
{
	using namespace McpPublicAccessPrivate;
	bRunning = false;
	if (OverlayTick.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(OverlayTick);
		OverlayTick.Reset();
	}
	if (Overlay.IsValid())
	{
		if (UGameViewportClient* VP = OverlayViewport.Get())
		{
			VP->RemoveViewportWidgetContent(Overlay.ToSharedRef());
		}
		Overlay.Reset();
	}
}

FString McpPublicAccess::Code()
{
	return McpPublicAccessPrivate::SessionCode;
}
