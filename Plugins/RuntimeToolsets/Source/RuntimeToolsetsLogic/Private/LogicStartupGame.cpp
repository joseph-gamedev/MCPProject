// Plays a saved game at startup: a JSON list of tool calls ({"calls":[{"tool","arguments"}]}) recorded while an agent built
// the game, replayed in order once the first game world has begun play. Set it with -RuntimeGame=<file> or
// [RuntimeToolsets] StartupGame=<file> in DefaultGame.ini; relative paths are under Content/RuntimeGames (staged with the build).

#include "LogicStartupGame.h"
#include "RuntimeJson.h"
#include "RuntimeToolsetsCore.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuntimeGame, Log, All);

namespace LogicStartupGamePrivate
{
	FTSTicker::FDelegateHandle Handle;
	bool bDone = false;

	FString ResolveGameFile()
	{
		FString File;
		if (!FParse::Value(FCommandLine::Get(), TEXT("RuntimeGame="), File) && GConfig)
		{
			GConfig->GetString(TEXT("RuntimeToolsets"), TEXT("StartupGame"), File, GGameIni);
		}
		File.TrimStartAndEndInline();
#if !WITH_EDITOR
		// Packaged builds play Content/RuntimeGames/startup.json when it was staged with the build.
		if (File.IsEmpty() && FPaths::FileExists(FPaths::ProjectContentDir() / TEXT("RuntimeGames/startup.json")))
		{
			File = TEXT("startup");
		}
#endif
		if (File.IsEmpty())
		{
			return File;
		}
		if (FPaths::IsRelative(File))
		{
			File = FPaths::ProjectContentDir() / TEXT("RuntimeGames") / File;
		}
		if (FPaths::GetExtension(File).IsEmpty())
		{
			File += TEXT(".json");
		}
		return File;
	}

	UWorld* FindPlayingWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			UWorld* W = Ctx.World();
			if (W && W->IsGameWorld() && W->HasBegunPlay() && W->GetFirstPlayerController())
			{
				return W;
			}
		}
		return nullptr;
	}

	bool Tick(float)
	{
		if (bDone)
		{
			return false;
		}
		if (!FindPlayingWorld())
		{
			return true;
		}
		bDone = true;
		const FString File = ResolveGameFile();
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			UE_LOG(LogRuntimeGame, Error, TEXT("Startup game '%s' not found"), *File);
			return false;
		}
		const TSharedPtr<FJsonObject> Root = RuntimeJson::Parse(Text);
		const TArray<TSharedPtr<FJsonValue>>* Calls = nullptr;
		if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("calls"), Calls))
		{
			UE_LOG(LogRuntimeGame, Error, TEXT("Startup game '%s' has no \"calls\" list"), *File);
			return false;
		}
		FToolCallerContext Caller;
		Caller.SessionId = TEXT("startup_game");
		Caller.ClientName = TEXT("startup_game");
		int32 Ok = 0, Failed = 0;
		for (const TSharedPtr<FJsonValue>& V : *Calls)
		{
			const TSharedPtr<FJsonObject> C = V->AsObject();
			FString Tool;
			if (!C.IsValid() || !C->TryGetStringField(TEXT("tool"), Tool))
			{
				continue;
			}
			const TSharedPtr<FJsonObject>* Args = nullptr;
			const TSharedPtr<FJsonObject> A = C->TryGetObjectField(TEXT("arguments"), Args) ? *Args : MakeShared<FJsonObject>();
			const FToolResult R = IRuntimeToolsetsCore::Get().ExecuteInline(Tool, A, Caller, 1);
			if (R.IsOk())
			{
				++Ok;
			}
			else
			{
				++Failed;
				UE_LOG(LogRuntimeGame, Warning, TEXT("Startup game: %s failed: %s"), *Tool, *R.Message);
			}
		}
		UE_LOG(LogRuntimeGame, Log, TEXT("Startup game '%s': %d calls ok, %d failed"), *File, Ok, Failed);
		return false;
	}
}

void LogicStartupGame::Startup()
{
	using namespace LogicStartupGamePrivate;
	if (!ResolveGameFile().IsEmpty())
	{
		Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.5f);
	}
}

void LogicStartupGame::Shutdown()
{
	using namespace LogicStartupGamePrivate;
	if (Handle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Handle);
		Handle.Reset();
	}
}
