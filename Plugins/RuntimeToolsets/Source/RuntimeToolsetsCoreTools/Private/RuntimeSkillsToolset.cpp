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

	FString SkillDir()
	{
		return FPaths::ProjectConfigDir() / TEXT("RuntimeToolsets") / TEXT("Skills");
	}

	/** Names of the compiled (C++/Blueprint) skills, which save_skill must not shadow. */
	TSet<FString> BuiltInSkillNames()
	{
		TSet<FString> Out;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(URuntimeAgentSkill::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
			{
				Out.Add(GetDefault<URuntimeAgentSkill>(*It)->GetSkillName());
			}
		}
		return Out;
	}

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
		const FString Dir = SkillDir();
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

TMap<FString, FRuntimeSkillDetails> URuntimeSkillsToolset::GetSkills(const TArray<FString>& SkillNames, const TArray<FString>& Names)
{
	TMap<FString, FRuntimeSkillDetails> Out;
	const TMap<FString, FSkillEntry> All = Collect();
	TArray<FString> Wanted = SkillNames;
	Wanted.Append(Names);
	if (Wanted.Num() == 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("give skill_names, e.g. [\"builder\"]"), TEXT("skills.list_skills lists them"));
		return {};
	}
	for (const FString& Name : Wanted)
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

FString URuntimeSkillsToolset::SaveSkill(const FString& Name, const FString& Description, const FString& Instructions, const TArray<FString>& Toolsets)
{
	bool bSnake = !Name.IsEmpty() && FChar::IsLower(Name[0]);
	for (TCHAR C : Name)
	{
		bSnake &= FChar::IsLower(C) || FChar::IsDigit(C) || C == '_';
	}
	if (!bSnake || Name.Len() > 64)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("skill name '%s' must be snake_case (a-z, 0-9, _), e.g. \"breakout_game\""), *Name));
		return FString();
	}
	if (BuiltInSkillNames().Contains(Name))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("'%s' is a built-in skill; save yours under another name"), *Name));
		return FString();
	}
	if (Description.TrimStartAndEnd().IsEmpty() || Instructions.TrimStartAndEnd().IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("description and instructions must not be empty"));
		return FString();
	}
	FString Front = TEXT("---\ndescription: ") + Description.Replace(TEXT("\n"), TEXT(" ")).TrimStartAndEnd() + TEXT("\n");
	if (Toolsets.Num() > 0)
	{
		Front += TEXT("toolsets: ") + FString::Join(Toolsets, TEXT(",")) + TEXT("\n");
	}
	Front += TEXT("---\n\n");
	const FString Path = FPaths::ConvertRelativePathToFull(SkillDir() / (Name + TEXT(".md")));
	if (!FFileHelper::SaveStringToFile(Front + Instructions, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Internal, FString::Printf(TEXT("could not write '%s'"), *Path));
		return FString();
	}
	return Path;
}

void URuntimeSkillsToolset::DeleteSkill(const FString& Name)
{
	const FString Path = SkillDir() / (Name + TEXT(".md"));
	if (!IFileManager::Get().FileExists(*Path))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no saved skill '%s'%s"), *Name,
			BuiltInSkillNames().Contains(Name) ? TEXT(" (built-in skills cannot be deleted)") : TEXT("")));
		return;
	}
	IFileManager::Get().Delete(*Path);
}
