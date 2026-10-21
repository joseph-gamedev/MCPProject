#include "RuntimeSkillsToolset.h"
#include "RuntimeAgentSkill.h"
#include "RuntimeToolCall.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"

namespace
{
	struct FSkillEntry
	{
		FString Description;
		TArray<FString> Toolsets;
		TFunction<FString()> Instructions;
	};

	/** C++/Blueprint skill classes plus markdown skills from Config/RuntimeToolsets/Skills. */
	TMap<FString, FSkillEntry> Collect()
	{
		TMap<FString, FSkillEntry> Out;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(URuntimeAgentSkill::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
				|| It->GetName().StartsWith(TEXT("SKEL_")) || It->GetName().StartsWith(TEXT("REINST_")))
			{
				continue;
			}
			const URuntimeAgentSkill* CDO = GetDefault<URuntimeAgentSkill>(*It);
			FSkillEntry E;
			E.Description = CDO->Description;
			for (const FName& T : CDO->Toolsets)
			{
				E.Toolsets.Add(T.ToString());
			}
			TWeakObjectPtr<const URuntimeAgentSkill> Weak = CDO;
			E.Instructions = [Weak]() { return Weak.IsValid() ? Weak->GetInstructions() : FString(); };
			Out.Add(CDO->GetSkillName(), MoveTemp(E));
		}
		const FString Dir = FPaths::ProjectConfigDir() / TEXT("RuntimeToolsets") / TEXT("Skills");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.md")), true, false);
		for (const FString& F : Files)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *(Dir / F)))
			{
				continue;
			}
			Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
			FSkillEntry E;
			FString Body = Text;
			if (Text.StartsWith(TEXT("---\n")))
			{
				const int32 End = Text.Find(TEXT("\n---"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 4);
				if (End != INDEX_NONE)
				{
					TArray<FString> Lines;
					Text.Mid(4, End - 4).ParseIntoArrayLines(Lines);
					for (const FString& L : Lines)
					{
						FString K, V;
						if (L.Split(TEXT(":"), &K, &V))
						{
							K.TrimStartAndEndInline();
							V.TrimStartAndEndInline();
							if (K == TEXT("description"))
							{
								E.Description = V;
							}
							else if (K == TEXT("toolsets"))
							{
								V.ParseIntoArray(E.Toolsets, TEXT(","));
							}
						}
					}
					Body = Text.Mid(End + 4).TrimStart();
				}
			}
			E.Instructions = [Body]() { return Body; };
			Out.Add(FPaths::GetBaseFilename(F), MoveTemp(E));
		}
		return Out;
	}
}

TMap<FString, FString> URuntimeSkillsToolset::ListSkills()
{
	TMap<FString, FString> Out;
	for (const TPair<FString, FSkillEntry>& P : Collect())
	{
		Out.Add(P.Key, P.Value.Description);
	}
	Out.KeySort([](const FString& A, const FString& B) { return A < B; });
	return Out;
}

TMap<FString, FRuntimeSkillDetails> URuntimeSkillsToolset::GetSkills(const TArray<FString>& SkillNames)
{
	TMap<FString, FRuntimeSkillDetails> Out;
	const TMap<FString, FSkillEntry> All = Collect();
	for (const FString& Name : SkillNames)
	{
		const FSkillEntry* E = All.Find(Name);
		if (!E)
		{
			TArray<FString> Names;
			All.GetKeys(Names);
			Names.Sort();
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no skill '%s'"), *Name), TEXT("Skills: ") + FString::Join(Names, TEXT(", ")));
			return {};
		}
		FRuntimeSkillDetails& D = Out.Add(Name);
		D.Instructions = E->Instructions();
		D.Toolsets = E->Toolsets;
	}
	return Out;
}
