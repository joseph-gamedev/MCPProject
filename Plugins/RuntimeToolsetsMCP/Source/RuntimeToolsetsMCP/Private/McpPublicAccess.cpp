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
#include "Misc/Guid.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/Compression.h"
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
	FString SessionCode; // shown in the game (10 characters: the agent's credential)
	FString Instance;   // this launch, so another game cannot take the code over
	FString LocalUrl;   // http://127.0.0.1:8018/mcp
	bool bRunning = false;
	bool bOnline = false;
	int32 Failures = 0;
	// what the connected agent is doing (inferred from the relayed requests)
	bool bAgentSeen = false;
	int32 InFlight = 0;
	double LastJob = 0.0;
	FString LastTool;
	FTSTicker::FDelegateHandle OverlayTick;
	TSharedPtr<SWidget> Overlay;
	TWeakObjectPtr<UGameViewportClient> OverlayViewport;

	/** A 4-digit code (1000-9999) the player says to Claude Code: "connect to game 4821". */
	FString MakeCode()
	{
		const FGuid G = FGuid::NewGuid();
		return FString::FromInt(1000 + int32((G.A ^ G.B ^ G.C ^ G.D) % 9000u));
	}

	FString ShownCode()
	{
		return SessionCode;
	}

	/** The tool an MCP request runs (tools/call, or session_call_tool's inner name), for the overlay. */
	void NoteRequest(const FString& Body)
	{
		if (Body.Len() > 65536)
		{
			return;
		}
		if (Body.Contains(TEXT("\"initialize\"")))
		{
			bAgentSeen = true;
		}
		TSharedPtr<FJsonObject> Msg;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Msg) || !Msg.IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonObject>* Params = nullptr;
		FString Name;
		if (Msg->GetStringField(TEXT("method")) == TEXT("tools/call") && Msg->TryGetObjectField(TEXT("params"), Params) &&
			(*Params)->TryGetStringField(TEXT("name"), Name))
		{
			const TSharedPtr<FJsonObject>* Args = nullptr;
			FString Inner;
			if (Name.EndsWith(TEXT("call_tool")) && (*Params)->TryGetObjectField(TEXT("arguments"), Args) && (*Args)->TryGetStringField(TEXT("name"), Inner))
			{
				Name = Inner;
			}
			int32 Us = INDEX_NONE;
			if (!Name.Contains(TEXT(".")) && Name.FindChar(TEXT('_'), Us))
			{
				Name[Us] = TEXT('.');   // MCP names are toolset_tool
			}
			LastTool = Name;
			bAgentSeen = true;
		}
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
		// Big requests (hundreds of building parts) come gzip-compressed from the relay: about 10x less to download.
		FString Gz;
		if (Job->TryGetStringField(TEXT("body_gz"), Gz))
		{
			TArray<uint8> Packed, Raw;
			const int32 Len = static_cast<int32>(Job->GetNumberField(TEXT("body_len")));
			if (Len <= 0 || !FBase64::Decode(Gz, Packed) || (Raw.SetNumUninitialized(Len), !FCompression::UncompressMemory(NAME_Gzip, Raw.GetData(), Len, Packed.GetData(), Packed.Num())))
			{
				UE_LOG(LogMcpRelay, Warning, TEXT("Relay job %s: could not unpack the compressed body"), *Id);
				TSharedRef<FJsonObject> Hdr = MakeShared<FJsonObject>();
				Hdr->SetStringField(TEXT("content-type"), TEXT("application/json"));
				PostResult(Id, 400, Hdr, TEXT("{\"error\":\"could not unpack the compressed request\"}"));
				return;
			}
			Req->SetContent(MoveTemp(Raw));
		}
		else
		{
			const FString Body = Job->GetStringField(TEXT("body"));
			NoteRequest(Body);
			Req->SetContentAsString(Body);
		}
		Req->SetTimeout(115.f);
		++InFlight;
		LastJob = FPlatformTime::Seconds();
		Req->OnProcessRequestComplete().BindLambda([Id](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			InFlight = FMath::Max(0, InFlight - 1);
			LastJob = FPlatformTime::Seconds();
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
		Req->SetURL(FString::Printf(TEXT("%s/host/%s/poll?gz=1"), *Relay, *SessionCode));
		Req->SetVerb(TEXT("GET"));
		Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + HostToken);
		Req->SetHeader(TEXT("X-Host-Instance"), Instance);
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
			if (Status == 409)
			{
				// another running game owns this code (a copied code or a restarted relay session): take a fresh one
				SessionCode = MakeCode();
				bAgentSeen = false;
				UE_LOG(LogMcpRelay, Display, TEXT("AI code was in use by another game; new AI code %s"), *ShownCode());
				Poll();
				return;
			}
			if (++Failures == 1 || Failures % 20 == 0)
			{
				UE_LOG(LogMcpRelay, Warning, TEXT("Relay poll failed (%d, try %d): %s"), Status, Failures, Status == 401 ? TEXT("wrong RelayHostToken") : TEXT("relay unreachable?"));
			}
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { Poll(); return false; }), FMath::Min(10.f, 1.f + Failures * 0.5f));
		});
		Req->ProcessRequest();
	}

	/** The corner panel: connection state, and while no agent is connected, where to connect. */
	void ShowOverlay()
	{
		if (Overlay.IsValid() || !GEngine || !GEngine->GameViewport || !FSlateApplication::IsInitialized())
		{
			return;
		}
		static const FSlateRoundedBoxBrush Brush(FLinearColor(0.02f, 0.03f, 0.05f, 0.72f), 6.f);
		auto Working = []() { return InFlight > 0 || FPlatformTime::Seconds() - LastJob < 2.0; };
		TSharedRef<SWidget> W = SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f)[SNullWidget::NullWidget]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(14.f, 0.f, 0.f, 12.f))
			[
				SNew(SBorder)
				.BorderImage(&Brush)
				.Padding(FMargin(10.f, 6.f))
				.Visibility(EVisibility::HitTestInvisible)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
						.ColorAndOpacity_Lambda([Working]()
						{
							return FSlateColor(!bOnline ? FLinearColor(1.f, 0.62f, 0.38f) : !bAgentSeen ? FLinearColor(0.55f, 0.9f, 1.f)
								: Working() ? FLinearColor(1.f, 0.86f, 0.4f) : FLinearColor(0.45f, 0.92f, 0.55f));
						})
						.Text_Lambda([Working]()
						{
							if (!bOnline)
							{
								return FText::FromString(FString::Printf(TEXT("Game code %s  (connecting...)"), *ShownCode()));
							}
							if (!bAgentSeen)
							{
								return FText::FromString(FString::Printf(TEXT("Game code %s"), *ShownCode()));
							}
							return FText::FromString(Working() && !LastTool.IsEmpty() ? FString::Printf(TEXT("Claude working: %s"), *LastTool)
								: FString::Printf(TEXT("Claude connected  -  %s"), *ShownCode()));
						})
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f, 0.f, 0.f))
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.8f, 0.84f)))
						.Visibility_Lambda([]() { return bOnline && !bAgentSeen ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						.Text_Lambda([]() { return FText::FromString(FString::Printf(TEXT("In Claude Code say: connect to game %s"), *ShownCode())); })
					]
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
	SessionCode = SessionCode.IsEmpty() ? MakeCode() : SessionCode.ToUpper().Replace(TEXT("-"), TEXT(""));
	Instance = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	LocalUrl = FString::Printf(TEXT("http://127.0.0.1:%d%s"), Port, *Path);
	bRunning = true;
	FFileHelper::SaveStringToFile(SessionCode + LINE_TERMINATOR, *(FPaths::ProjectSavedDir() / TEXT("RuntimeToolsets/code.txt")));
	UE_LOG(LogMcpRelay, Display, TEXT("Agent relay %s, game code %s (players say: connect to game %s)"), *Relay, *ShownCode(), *ShownCode());
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
