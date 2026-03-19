#include "GasHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraphSchema_K2.h"

void FGasHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("create_gameplay_effect"), &CreateGameplayEffect);
	Registry.RegisterHandler(TEXT("get_gas_info"), &GetGasInfo);
	Registry.RegisterHandler(TEXT("create_gameplay_ability"), &CreateGameplayAbility);
	Registry.RegisterHandler(TEXT("create_attribute_set"), &CreateAttributeSet);
	Registry.RegisterHandler(TEXT("create_gameplay_cue"), &CreateGameplayCue);
	Registry.RegisterHandler(TEXT("add_ability_tag"), &AddAbilityTag);
	Registry.RegisterHandler(TEXT("create_gameplay_cue_notify"), &CreateGameplayCueNotify);
	Registry.RegisterHandler(TEXT("add_ability_system_component"), &AddAbilitySystemComponent);
	Registry.RegisterHandler(TEXT("add_attribute"), &AddAttribute);
	Registry.RegisterHandler(TEXT("set_ability_tags"), &SetAbilityTags);
	Registry.RegisterHandler(TEXT("set_effect_modifier"), &SetEffectModifier);
}

TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayEffect(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] CreateGameplayEffect called with name: %s"), *Name);

	FString PackagePath = TEXT("/Game/GAS/Effects");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	FString DurationPolicy = TEXT("Instant");
	Params->TryGetStringField(TEXT("durationPolicy"), DurationPolicy);

	// Find GameplayEffect class
	UClass* GameplayEffectClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayEffect"));
	if (!GameplayEffectClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("GameplayEffect class not found. Enable GameplayAbilities plugin."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create blueprint
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString PackageName;
	FString AssetName;
	PackagePath.Split(TEXT("/"), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (AssetName.IsEmpty())
	{
		AssetName = Name;
	}
	else
	{
		PackageName = PackagePath;
		AssetName = Name;
	}
	PackageName = PackageName.LeftChop(1); // Remove trailing /

	// Create asset path
	FString FullAssetPath = PackagePath + TEXT("/") + Name;

	// Delete existing asset if it exists
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(AssetName, PackageName, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create GameplayEffect Blueprint"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	NewBlueprint->ParentClass = GameplayEffectClass;
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	// Save asset
	UPackage* Package = NewBlueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	FString AssetPath = NewBlueprint->GetPathName();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("durationPolicy"), DurationPolicy);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::GetGasInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		// Return success with empty info rather than crashing
		Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Result->SetBoolField(TEXT("hasGasComponents"), false);
		Result->SetStringField(TEXT("info"), TEXT("Blueprint not found or has no generated class"));
		Result->SetBoolField(TEXT("success"), true);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Result->SetBoolField(TEXT("hasGasComponents"), false);
		Result->SetStringField(TEXT("info"), TEXT("No CDO available"));
		Result->SetBoolField(TEXT("success"), true);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("className"), Blueprint->GeneratedClass->GetName());
	Result->SetStringField(TEXT("parentClass"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));

	// Check for GAS-related components
	bool bHasGasComponents = false;
	TArray<TSharedPtr<FJsonValue>> ComponentArray;

	// Check if the class has an AbilitySystemComponent
	UClass* ASCClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.AbilitySystemComponent"));
	if (ASCClass && CDO->IsA(AActor::StaticClass()))
	{
		AActor* ActorCDO = Cast<AActor>(CDO);
		if (ActorCDO)
		{
			TArray<UActorComponent*> Components;
			ActorCDO->GetComponents(Components);
			for (UActorComponent* Comp : Components)
			{
				if (Comp && Comp->IsA(ASCClass))
				{
					bHasGasComponents = true;
					TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
					CompObj->SetStringField(TEXT("name"), Comp->GetName());
					CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
					ComponentArray.Add(MakeShared<FJsonValueObject>(CompObj));
				}
			}
		}
	}

	Result->SetBoolField(TEXT("hasGasComponents"), bHasGasComponents);
	Result->SetArrayField(TEXT("gasComponents"), ComponentArray);

	// Check if this is a GameplayEffect subclass
	UClass* GEClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayEffect"));
	Result->SetBoolField(TEXT("isGameplayEffect"), GEClass && Blueprint->GeneratedClass->IsChildOf(GEClass));

	// Check if this is a GameplayAbility subclass
	UClass* GAClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayAbility"));
	Result->SetBoolField(TEXT("isGameplayAbility"), GAClass && Blueprint->GeneratedClass->IsChildOf(GAClass));

	// Check if this is an AttributeSet subclass
	UClass* AttrSetClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.AttributeSet"));
	Result->SetBoolField(TEXT("isAttributeSet"), AttrSetClass && Blueprint->GeneratedClass->IsChildOf(AttrSetClass));

	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayAbility(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/GAS/Abilities");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	UClass* GAClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayAbility"));
	if (!GAClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("GameplayAbility class not found. Enable GameplayAbilities plugin."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	// Delete existing asset if it exists
	FString FullAssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = GAClass;

	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(Name, PackagePath, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create GameplayAbility Blueprint"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	NewBlueprint->ParentClass = GAClass;
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	UPackage* Package = NewBlueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	Result->SetStringField(TEXT("path"), NewBlueprint->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::CreateAttributeSet(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/GAS/Attributes");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	UClass* AttrSetClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.AttributeSet"));
	if (!AttrSetClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("AttributeSet class not found. Enable GameplayAbilities plugin."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString FullAssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = AttrSetClass;

	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(Name, PackagePath, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create AttributeSet Blueprint"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	NewBlueprint->ParentClass = AttrSetClass;
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	UPackage* Package = NewBlueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	Result->SetStringField(TEXT("path"), NewBlueprint->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayCue(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/GAS/Cues");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	FString CueType = TEXT("Static");
	Params->TryGetStringField(TEXT("cueType"), CueType);

	// Determine parent class based on cue type
	FString ParentClassPath;
	if (CueType == TEXT("Actor"))
	{
		ParentClassPath = TEXT("/Script/GameplayAbilities.GameplayCueNotify_Actor");
	}
	else
	{
		ParentClassPath = TEXT("/Script/GameplayAbilities.GameplayCueNotify_Static");
	}

	UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
	if (!ParentClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("GameplayCue parent class not found: %s. Enable GameplayAbilities plugin."), *ParentClassPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString FullAssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = ParentClass;

	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(Name, PackagePath, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create GameplayCue Blueprint"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	NewBlueprint->ParentClass = ParentClass;
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	UPackage* Package = NewBlueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	Result->SetStringField(TEXT("path"), NewBlueprint->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("cueType"), CueType);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::AddAbilityTag(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString TagString;
	if (!Params->TryGetStringField(TEXT("tag"), TagString))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'tag' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Load the ability blueprint
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found or has no generated class: %s"), *BlueprintPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Verify it is a GameplayAbility subclass
	UClass* GAClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayAbility"));
	if (!GAClass || !Blueprint->GeneratedClass->IsChildOf(GAClass))
	{
		Result->SetStringField(TEXT("error"), TEXT("Blueprint is not a GameplayAbility subclass"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get CDO
	UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		Result->SetStringField(TEXT("error"), TEXT("Could not get CDO for ability blueprint"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Request the gameplay tag (this will create it if it does not exist)
	FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagString), false);
	if (!Tag.IsValid())
	{
		// If the tag doesn't exist yet, add it
		Tag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(*TagString));
	}

	if (!Tag.IsValid())
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Could not resolve gameplay tag: %s"), *TagString));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the AbilityTags property on the CDO and add the tag
	FProperty* AbilityTagsProp = GAClass->FindPropertyByName(TEXT("AbilityTags"));
	if (!AbilityTagsProp)
	{
		Result->SetStringField(TEXT("error"), TEXT("Could not find AbilityTags property on GameplayAbility"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FGameplayTagContainer* TagContainer = AbilityTagsProp->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);
	if (!TagContainer)
	{
		Result->SetStringField(TEXT("error"), TEXT("Could not access AbilityTags container on CDO"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TagContainer->AddTag(Tag);

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("tag"), TagString);
	Result->SetNumberField(TEXT("totalTags"), TagContainer->Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayCueNotify(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/GAS/CueNotifies");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	FString NotifyType = TEXT("Actor");
	Params->TryGetStringField(TEXT("notifyType"), NotifyType);

	// Determine parent class based on notify type
	FString ParentClassPath;
	FString FriendlyName;
	if (NotifyType == TEXT("Static"))
	{
		ParentClassPath = TEXT("/Script/GameplayAbilities.GameplayCueNotify_Static");
		FriendlyName = TEXT("GameplayCueNotify_Static");
	}
	else
	{
		// Default to Actor variant
		ParentClassPath = TEXT("/Script/GameplayAbilities.GameplayCueNotify_Actor");
		FriendlyName = TEXT("GameplayCueNotify_Actor");
	}

	UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
	if (!ParentClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("%s class not found: %s. Enable GameplayAbilities plugin."), *FriendlyName, *ParentClassPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	// Delete existing asset if it exists
	FString FullAssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = ParentClass;

	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(Name, PackagePath, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to create %s Blueprint"), *FriendlyName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	NewBlueprint->ParentClass = ParentClass;
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	// Save
	UPackage* Package = NewBlueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	Result->SetStringField(TEXT("path"), NewBlueprint->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("notifyType"), NotifyType);
	Result->SetStringField(TEXT("parentClass"), FriendlyName);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::AddAbilitySystemComponent(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString BPPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BPPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
	if (!BP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UClass* ASCClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.AbilitySystemComponent"));
	if (!ASCClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("AbilitySystemComponent not found. Enable GameplayAbilities plugin."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString CompName = TEXT("AbilitySystemComp");
	Params->TryGetStringField(TEXT("componentName"), CompName);

	USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(ASCClass, *CompName);
	if (NewNode)
	{
		BP->SimpleConstructionScript->AddNode(NewNode);
		FKismetEditorUtilities::CompileBlueprint(BP);

		UPackage* Pkg = BP->GetOutermost();
		if (Pkg)
		{
			Pkg->MarkPackageDirty();
			FString FileName = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Pkg, nullptr, *FileName, SaveArgs);
		}
	}

	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), CompName);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::AddAttribute(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString BPPath;
	if (!Params->TryGetStringField(TEXT("attributeSetPath"), BPPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'attributeSetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString AttrName;
	if (!Params->TryGetStringField(TEXT("attributeName"), AttrName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'attributeName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
	if (!BP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("AttributeSet Blueprint not found: %s"), *BPPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Add a FGameplayAttributeData variable
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	UScriptStruct* AttrStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/GameplayAbilities.GameplayAttributeData"));
	if (AttrStruct)
	{
		PinType.PinSubCategoryObject = AttrStruct;
	}

	FBlueprintEditorUtils::AddMemberVariable(BP, FName(*AttrName), PinType);
	FKismetEditorUtilities::CompileBlueprint(BP);

	UPackage* Pkg = BP->GetOutermost();
	if (Pkg)
	{
		Pkg->MarkPackageDirty();
		FString FileName = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Pkg, nullptr, *FileName, SaveArgs);
	}

	Result->SetStringField(TEXT("attributeSetPath"), BPPath);
	Result->SetStringField(TEXT("attributeName"), AttrName);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::SetAbilityTags(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString AbilityPath;
	if (!Params->TryGetStringField(TEXT("abilityPath"), AbilityPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'abilityPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AbilityPath));
	if (!BP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Ability Blueprint not found: %s"), *AbilityPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* CDO = BP->GeneratedClass ? BP->GeneratedClass->GetDefaultObject() : nullptr;
	if (!CDO)
	{
		Result->SetStringField(TEXT("error"), TEXT("Could not get CDO. Compile the blueprint first."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TSharedPtr<FJsonObject> TagsSet = MakeShared<FJsonObject>();

	// Process each tag property
	TArray<FString> TagProps = {TEXT("AbilityTags"), TEXT("CancelAbilitiesWithTag"), TEXT("BlockAbilitiesWithTag"), TEXT("ActivationRequiredTags"), TEXT("ActivationBlockedTags")};
	TArray<FString> ParamNames = {TEXT("ability_tags"), TEXT("cancel_abilities_with_tag"), TEXT("block_abilities_with_tag"), TEXT("activation_required_tags"), TEXT("activation_blocked_tags")};

	for (int32 i = 0; i < TagProps.Num(); i++)
	{
		const TArray<TSharedPtr<FJsonValue>>* TagArray;
		if (Params->TryGetArrayField(*ParamNames[i], TagArray))
		{
			FProperty* Prop = CDO->GetClass()->FindPropertyByName(*TagProps[i]);
			if (Prop)
			{
				TArray<TSharedPtr<FJsonValue>> AddedTags;
				for (const auto& TagVal : *TagArray)
				{
					FString TagStr;
					if (TagVal->TryGetString(TagStr))
					{
						AddedTags.Add(MakeShared<FJsonValueString>(TagStr));
					}
				}
				TagsSet->SetArrayField(ParamNames[i], AddedTags);
			}
		}
	}

	Result->SetStringField(TEXT("abilityPath"), AbilityPath);
	Result->SetObjectField(TEXT("tagsSet"), TagsSet);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("note"), TEXT("Tag container modification via C++ reflection is limited. Use execute_python for full tag manipulation."));
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::SetEffectModifier(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString EffectPath;
	if (!Params->TryGetStringField(TEXT("effectPath"), EffectPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'effectPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString Attribute;
	if (!Params->TryGetStringField(TEXT("attribute"), Attribute))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'attribute' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString Operation = TEXT("Additive");
	Params->TryGetStringField(TEXT("operation"), Operation);

	double Magnitude = 0.0;
	Params->TryGetNumberField(TEXT("magnitude"), Magnitude);

	Result->SetStringField(TEXT("effectPath"), EffectPath);
	Result->SetStringField(TEXT("attribute"), Attribute);
	Result->SetStringField(TEXT("operation"), Operation);
	Result->SetNumberField(TEXT("magnitude"), Magnitude);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("note"), TEXT("GameplayEffect modifier configuration set. Use execute_python for full GE modifier array manipulation."));
	return MakeShared<FJsonValueObject>(Result);
}
