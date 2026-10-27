#include "RuntimeToolsetsSettings.h"

URuntimeToolsetsSettings::URuntimeToolsetsSettings()
{
	// Safe defaults so the plugin is locked down even without project config. Projects may replace these lists in DefaultGame.ini.
	AllowedObjectRoots = {TEXT("/Script/"), TEXT("/Game/"), TEXT("/Engine/"), TEXT("/Temp/"), TEXT("/RuntimeToolsets/")};   // the plugin's own materials too
	SpawnClassAllowRoots = {TEXT("/Script/Engine."), TEXT("/Game/")};
	DeniedClasses = {TEXT("GameEngine"), TEXT("EditorEngine")};
	DeniedFunctions = {TEXT("QuitGame"), TEXT("ExecuteConsoleCommand"), TEXT("ConsoleCommand"), TEXT("ServerTravel"), TEXT("ClientTravel"),
		TEXT("OpenLevel"), TEXT("OpenLevelBySoftObjectPtr"), TEXT("K2_DestroyActor"), TEXT("LaunchURL"), TEXT("SetGlobalTimeDilation")};
	ConsoleDenyPrefixes = {TEXT("quit"), TEXT("exit"), TEXT("crash"), TEXT("debug "), TEXT("gpf"), TEXT("open "), TEXT("travel"), TEXT("servertravel"),
		TEXT("obj gc"), TEXT("exec "), TEXT("killall"), TEXT("disconnect"), TEXT("restartlevel"), TEXT("RuntimeToolsets.")};
}
