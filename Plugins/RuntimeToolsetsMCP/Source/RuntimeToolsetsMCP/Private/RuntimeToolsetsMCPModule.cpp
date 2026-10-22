#include "IModelContextProtocolModule.h"
#include "McpPublicAccess.h"
#include "McpToolAdapter.h"
#include "ModelContextProtocolServer.h"
#include "RuntimeToolsetsCore.h"
#include "RuntimeToolsetsMCPSettings.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuntimeToolsetsMCP, Log, All);

/** Mirrors the RuntimeToolsets catalogue into Epic's MCP server and starts it on loopback. */
class FRuntimeToolsetsMCPModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (IsRunningCommandlet())
		{
			return;
		}
		IModelContextProtocolModule* Mcp = IModelContextProtocolModule::Get();
		if (!Mcp)
		{
			UE_LOG(LogRuntimeToolsetsMCP, Error, TEXT("ModelContextProtocol module is not available; adapter disabled"));
			return;
		}
		// Make sure the toolsets are registered before mirroring them.
		FModuleManager::Get().LoadModule(TEXT("RuntimeToolsetsCoreTools"));

		RefreshHandle = Mcp->OnRefreshTools().AddRaw(this, &FRuntimeToolsetsMCPModule::Register);
		ToolsetsChangedHandle = IRuntimeToolsetsCore::Get().OnToolsetsChanged().AddRaw(this, &FRuntimeToolsetsMCPModule::RegisterAndNotify);
		Register();

		const URuntimeToolsetsMCPSettings& Settings = *GetDefault<URuntimeToolsetsMCPSettings>();
		FModelContextProtocolServer* Server = Mcp->GetServer();
		if (Settings.bAutoStart && (!Server || !Server->IsServerRunning()))
		{
			// -RuntimeToolsetsPort=8019 overrides the configured port (several game instances on one machine).
			int32 Port = Settings.Port;
			FParse::Value(FCommandLine::Get(), TEXT("RuntimeToolsetsPort="), Port);
			ServerPath = Settings.Path;
			Mcp->StartServer(static_cast<uint32>(Port), ServerPath);
			bStartedServer = true;
		}
		Server = Mcp->GetServer();
		if (Server && Server->IsServerRunning())
		{
			UE_LOG(LogRuntimeToolsetsMCP, Display, TEXT("MCP server: http://127.0.0.1:%u%s (%d tools, %s mode)"), Server->GetServerPort(), *ServerPath,
				Registered.Num(), Settings.ExposureMode == ERuntimeToolsetsMCPExposure::Meta ? TEXT("meta") : TEXT("eager"));
			McpPublicAccess::Start(static_cast<int32>(Server->GetServerPort()), ServerPath);
		}
		else
		{
			UE_LOG(LogRuntimeToolsetsMCP, Warning, TEXT("MCP server is not running"));
		}
	}

	virtual void ShutdownModule() override
	{
		McpPublicAccess::Stop();
		IModelContextProtocolModule* Mcp = IModelContextProtocolModule::Get();
		if (IRuntimeToolsetsCore::IsAvailable())
		{
			IRuntimeToolsetsCore::Get().OnToolsetsChanged().Remove(ToolsetsChangedHandle);
		}
		if (Mcp)
		{
			Mcp->OnRefreshTools().Remove(RefreshHandle);
			Unregister(*Mcp);
			if (bStartedServer)
			{
				Mcp->StopServer();
			}
		}
	}

private:
	void Unregister(IModelContextProtocolModule& Mcp)
	{
		for (const TSharedRef<IModelContextProtocolTool>& Tool : Registered)
		{
			Mcp.RemoveTool(Tool);
		}
		Registered.Reset();
	}

	void Register()
	{
		IModelContextProtocolModule* Mcp = IModelContextProtocolModule::Get();
		if (!Mcp || !IRuntimeToolsetsCore::IsAvailable())
		{
			return;
		}
		Unregister(*Mcp);
		const URuntimeToolsetsMCPSettings& Settings = *GetDefault<URuntimeToolsetsMCPSettings>();
		const bool bMeta = Settings.ExposureMode == ERuntimeToolsetsMCPExposure::Meta;
		for (const TSharedRef<const FRuntimeToolDescriptor>& Tool : IRuntimeToolsetsCore::Get().GetAllTools())
		{
			if (bMeta && Tool->Toolset != TEXT("session"))
			{
				continue;
			}
			const FString Name = FMcpToolAdapter::MakeMcpName(Tool->QualifiedName, Settings.ToolNamePrefix);
			TSharedRef<FMcpToolAdapter> Adapter = MakeShared<FMcpToolAdapter>(Tool, Name, Settings.SessionId);
			if (Mcp->AddTool(Adapter))
			{
				Registered.Add(Adapter);
			}
			else
			{
				UE_LOG(LogRuntimeToolsetsMCP, Warning, TEXT("MCP tool name '%s' is taken or invalid; set ToolNamePrefix"), *Name);
			}
		}
	}

	void RegisterAndNotify()
	{
		Register();
		if (IModelContextProtocolModule* Mcp = IModelContextProtocolModule::Get())
		{
			if (FModelContextProtocolServer* Server = Mcp->GetServer())
			{
				Server->ScheduleToolsListChangedBroadcast();
			}
		}
	}

	TArray<TSharedRef<IModelContextProtocolTool>> Registered;
	FDelegateHandle RefreshHandle;
	FDelegateHandle ToolsetsChangedHandle;
	bool bStartedServer = false;
	FString ServerPath = TEXT("/mcp");
};

IMPLEMENT_MODULE(FRuntimeToolsetsMCPModule, RuntimeToolsetsMCP)
