#include "RuntimeToolset.h"

TSharedRef<FJsonObject> FRuntimeToolDescriptor::ToJson(bool bIncludeSchema) const
{
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("name"), QualifiedName);
	Out->SetStringField(TEXT("description"), Description);
	if (bMutating)
	{
		Out->SetBoolField(TEXT("mutating"), true);
		Out->SetBoolField(TEXT("undoable"), bUndoable);
	}
	if (bAsync)
	{
		Out->SetBoolField(TEXT("async"), true);
	}
	if (bIncludeSchema)
	{
		Out->SetObjectField(TEXT("input_schema"), InputSchema);
		if (OutputSchema.IsValid())
		{
			Out->SetObjectField(TEXT("output_schema"), OutputSchema);
		}
	}
	return Out;
}

FRuntimeToolBuilder& FRuntimeToolBuilder::Args(const FToolSchema& Schema)
{
	Descriptor->InputSchema = Schema.Build();
	return *this;
}

FRuntimeToolBuilder& FRuntimeToolBuilder::ArgsJson(const TSharedRef<FJsonObject>& Schema)
{
	Descriptor->InputSchema = Schema;
	return *this;
}

FRuntimeToolBuilder& FRuntimeToolBuilder::Returns(const TSharedRef<FJsonObject>& Schema)
{
	Descriptor->OutputSchema = Schema;
	return *this;
}

FRuntimeToolBuilder& FRuntimeToolBuilder::Undoable()
{
	Descriptor->bMutating = true;
	Descriptor->bUndoable = true;
	return *this;
}

FRuntimeToolBuilder& FRuntimeToolBuilder::Irreversible()
{
	Descriptor->bMutating = true;
	Descriptor->bUndoable = false;
	return *this;
}

FRuntimeToolBuilder& FRuntimeToolBuilder::Async()
{
	Descriptor->bAsync = true;
	return *this;
}

FRuntimeToolBuilder& FRuntimeToolBuilder::RequiresWorld()
{
	Descriptor->bRequiresWorld = true;
	return *this;
}

FRuntimeToolBuilder& FRuntimeToolBuilder::Handle(FRuntimeToolHandler InHandler)
{
	Descriptor->Handler = MoveTemp(InHandler);
	return *this;
}

FRuntimeToolBuilder FRuntimeToolsetBuilder::Tool(const FString& Name, const FString& Description)
{
	TSharedRef<FRuntimeToolDescriptor> D = MakeShared<FRuntimeToolDescriptor>();
	D->Toolset = Toolset;
	D->Name = Name;
	// FName keeps the first-registered casing ("Session" may already exist), so normalise.
	D->QualifiedName = Toolset.ToString().ToLower() + TEXT(".") + Name;
	D->Description = Description;
	Tools.Add(D);
	return FRuntimeToolBuilder(D);
}
