#include "EditorHandlers.h"
#include "HandlerRegistry.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IPythonScriptPlugin.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "LevelEditorViewport.h"
#include "UnrealClient.h"
#include "Slate/SceneViewport.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "Logging/MessageLog.h"
#include "HighResScreenshot.h"
#include "Misc/OutputDeviceRedirector.h"
#include "FileHelpers.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "EditorValidatorSubsystem.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif
#include "LevelEditorSubsystem.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "DesktopPlatformModule.h"
#include "HAL/PlatformProcess.h"
#include "Misc/MonitoredProcess.h"

void FEditorHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Install log capture ring buffer (#82)
	FMCPLogCapture::Get().Install();

	Registry.RegisterHandler(TEXT("execute_command"), &ExecuteCommand);
	Registry.RegisterHandler(TEXT("execute_python"), &ExecutePython);
	Registry.RegisterHandler(TEXT("set_property"), &SetProperty);
	Registry.RegisterHandler(TEXT("set_config"), &SetConfig);
	Registry.RegisterHandler(TEXT("read_config"), &ReadConfig);
	Registry.RegisterHandler(TEXT("get_viewport_info"), &GetViewportInfo);
	Registry.RegisterHandler(TEXT("get_editor_performance_stats"), &GetEditorPerformanceStats);
	Registry.RegisterHandler(TEXT("get_output_log"), &GetOutputLog);
	Registry.RegisterHandler(TEXT("search_log"), &SearchLog);
	Registry.RegisterHandler(TEXT("get_message_log"), &GetMessageLog);
	Registry.RegisterHandler(TEXT("get_build_status"), &GetBuildStatus);
	Registry.RegisterHandler(TEXT("pie_control"), &PieControl);
	Registry.RegisterHandler(TEXT("capture_screenshot"), &CaptureScreenshot);
	Registry.RegisterHandler(TEXT("set_viewport_camera"), &SetViewportCamera);
	Registry.RegisterHandler(TEXT("undo"), &Undo);
	Registry.RegisterHandler(TEXT("redo"), &Redo);
	Registry.RegisterHandler(TEXT("reload_handlers"), &ReloadHandlers);
	Registry.RegisterHandler(TEXT("save_asset"), &SaveAsset);
	Registry.RegisterHandler(TEXT("save_all"), &SaveAll);
	Registry.RegisterHandler(TEXT("get_crash_reports"), &GetCrashReports);
	Registry.RegisterHandler(TEXT("read_editor_log"), &ReadEditorLog);
	Registry.RegisterHandler(TEXT("pie_get_runtime_value"), &PieGetRuntimeValue);
	Registry.RegisterHandler(TEXT("build_lighting"), &BuildLighting);
	Registry.RegisterHandler(TEXT("build_all"), &BuildAll);
	Registry.RegisterHandler(TEXT("validate_assets"), &ValidateAssets);
	Registry.RegisterHandler(TEXT("cook_content"), &CookContent);
	Registry.RegisterHandler(TEXT("focus_viewport_on_actor"), &FocusViewportOnActor);
	Registry.RegisterHandler(TEXT("hot_reload"), &HotReload);
	Registry.RegisterHandler(TEXT("create_new_level"), &CreateNewLevel);
	Registry.RegisterHandler(TEXT("save_current_level"), &SaveCurrentLevel);
	Registry.RegisterHandler(TEXT("open_asset"), &OpenAsset);
	// Aliases for TS tool compatibility
	Registry.RegisterHandler(TEXT("get_runtime_value"), &PieGetRuntimeValue);
	// New handlers
	Registry.RegisterHandler(TEXT("run_stat_command"), &RunStatCommand);
	Registry.RegisterHandler(TEXT("set_scalability"), &SetScalability);
	Registry.RegisterHandler(TEXT("build_geometry"), &BuildGeometry);
	Registry.RegisterHandler(TEXT("build_hlod"), &BuildHlod);
	Registry.RegisterHandler(TEXT("list_crashes"), &ListCrashes);
	Registry.RegisterHandler(TEXT("get_crash_info"), &GetCrashInfo);
	Registry.RegisterHandler(TEXT("check_for_crashes"), &CheckForCrashes);
	// #14: Build project
	Registry.RegisterHandler(TEXT("build_project"), &BuildProject);
	// #49: Generate project files
	Registry.RegisterHandler(TEXT("generate_project_files"), &GenerateProjectFiles);
}

TSharedPtr<FJsonValue> FEditorHandlers::ExecuteCommand(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Command;
	if (!Params->TryGetStringField(TEXT("command"), Command))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'command' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UKismetSystemLibrary::ExecuteConsoleCommand(
			GEditor->GetEditorWorldContext().World(),
			Command,
			nullptr
		);
		Result->SetStringField(TEXT("command"), Command);
		Result->SetBoolField(TEXT("success"), true);
	}
	else
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::ExecutePython(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Code;
	if (!Params->TryGetStringField(TEXT("code"), Code))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'code' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
	{
		Result->SetStringField(TEXT("error"), TEXT("Python scripting is not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FPythonCommandEx PythonCommand;
	PythonCommand.Command = Code;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;

	bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("result"), PythonCommand.CommandResult);

	TArray<TSharedPtr<FJsonValue>> LogArray;
	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		TSharedPtr<FJsonObject> LogEntry = MakeShared<FJsonObject>();
		LogEntry->SetStringField(TEXT("type"), LexToString(Entry.Type));
		LogEntry->SetStringField(TEXT("output"), Entry.Output);
		LogArray.Add(MakeShared<FJsonValueObject>(LogEntry));
	}
	Result->SetArrayField(TEXT("log_output"), LogArray);

	FString CombinedOutput;
	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		if (!CombinedOutput.IsEmpty()) CombinedOutput += TEXT("\n");
		CombinedOutput += Entry.Output;
	}
	Result->SetStringField(TEXT("output"), CombinedOutput);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SetProperty(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("propertyName"), PropertyName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'propertyName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Load asset
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get property
	FProperty* Property = Asset->GetClass()->FindPropertyByName(*PropertyName);
	if (!Property)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Property not found: %s"), *PropertyName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get value from params
	TSharedPtr<FJsonValue> ValueJsonRef = Params->TryGetField(TEXT("value"));
	if (!ValueJsonRef.IsValid())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'value' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set property value — use ImportText_Direct for full UE text format support (#29)
	// This handles nested struct arrays, FVector, FGameplayTag, TArray<>, etc.
	void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Asset);

	FString ValueStr;
	if (ValueJsonRef->Type == EJson::String)
	{
		ValueStr = ValueJsonRef->AsString();
	}
	else if (ValueJsonRef->Type == EJson::Boolean)
	{
		ValueStr = ValueJsonRef->AsBool() ? TEXT("true") : TEXT("false");
	}
	else if (ValueJsonRef->Type == EJson::Number)
	{
		ValueStr = FString::SanitizeFloat(ValueJsonRef->AsNumber());
	}
	else
	{
		// For objects/arrays, serialize back to string for ImportText
		// This lets callers pass UE text format as a string value
		Result->SetStringField(TEXT("error"), TEXT("Value must be a string (UE text format), number, or boolean. For complex types, pass UE text format as a string (e.g. '((Key=1,Value=\"Hello\"))' for struct arrays)."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	const TCHAR* ImportResult = Property->ImportText_Direct(*ValueStr, PropertyValue, Asset, PPF_None);
	if (!ImportResult)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("ImportText failed for property '%s' with value '%s'. Check UE text format."), *PropertyName, *ValueStr));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Asset->MarkPackageDirty();

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SetConfig(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ConfigName;
	if (!Params->TryGetStringField(TEXT("configName"), ConfigName))
	{
		Params->TryGetStringField(TEXT("configFile"), ConfigName);
	}
	FString Section;
	if (!Params->TryGetStringField(TEXT("section"), Section))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'section' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}
	FString Key;
	if (!Params->TryGetStringField(TEXT("key"), Key))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'key' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}
	FString Value;
	Params->TryGetStringField(TEXT("value"), Value);

	if (ConfigName.IsEmpty())
	{
		ConfigName = TEXT("DefaultEngine.ini");
	}
	else if (!ConfigName.EndsWith(TEXT(".ini")))
	{
		ConfigName = FString::Printf(TEXT("Default%s.ini"), *ConfigName);
	}

	FString ConfigDir = FPaths::ProjectConfigDir();
	FString IniPath = FPaths::Combine(ConfigDir, ConfigName);

	GConfig->SetString(*Section, *Key, *Value, IniPath);
	GConfig->Flush(false, IniPath);

	Result->SetStringField(TEXT("configFile"), ConfigName);
	Result->SetStringField(TEXT("section"), Section);
	Result->SetStringField(TEXT("key"), Key);
	Result->SetStringField(TEXT("value"), Value);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::ReadConfig(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ConfigName;
	if (!Params->TryGetStringField(TEXT("configFile"), ConfigName))
	{
		Params->TryGetStringField(TEXT("configName"), ConfigName);
	}
	FString Section;
	if (!Params->TryGetStringField(TEXT("section"), Section))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'section' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}
	FString Key;
	if (!Params->TryGetStringField(TEXT("key"), Key))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'key' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (ConfigName.IsEmpty())
	{
		ConfigName = TEXT("DefaultEngine.ini");
	}
	else if (!ConfigName.EndsWith(TEXT(".ini")))
	{
		ConfigName = FString::Printf(TEXT("Default%s.ini"), *ConfigName);
	}

	FString ConfigDir = FPaths::ProjectConfigDir();
	FString IniPath = FPaths::Combine(ConfigDir, ConfigName);

	FString Value;
	bool bFound = GConfig->GetString(*Section, *Key, Value, IniPath);

	Result->SetStringField(TEXT("configFile"), ConfigName);
	Result->SetStringField(TEXT("section"), Section);
	Result->SetStringField(TEXT("key"), Key);
	Result->SetBoolField(TEXT("found"), bFound);
	if (bFound)
	{
		Result->SetStringField(TEXT("value"), Value);
	}
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetViewportInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient)
	{
		// Try to get from level viewport clients list
		const TArray<FLevelEditorViewportClient*>& ViewportClients = GEditor->GetLevelViewportClients();
		if (ViewportClients.Num() > 0)
		{
			ViewportClient = ViewportClients[0];
		}
	}

	if (!ViewportClient)
	{
		Result->SetStringField(TEXT("error"), TEXT("No viewport client available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FVector Location = ViewportClient->GetViewLocation();
	FRotator Rotation = ViewportClient->GetViewRotation();
	float FOV = ViewportClient->ViewFOV;

	TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	LocationObj->SetNumberField(TEXT("x"), Location.X);
	LocationObj->SetNumberField(TEXT("y"), Location.Y);
	LocationObj->SetNumberField(TEXT("z"), Location.Z);
	Result->SetObjectField(TEXT("location"), LocationObj);

	TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
	RotationObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
	RotationObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
	RotationObj->SetNumberField(TEXT("roll"), Rotation.Roll);
	Result->SetObjectField(TEXT("rotation"), RotationObj);

	Result->SetNumberField(TEXT("fov"), FOV);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetEditorPerformanceStats(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// FPS from delta time
	double DeltaTime = FApp::GetDeltaTime();
	double FPS = (DeltaTime > 0.0) ? (1.0 / DeltaTime) : 0.0;
	Result->SetNumberField(TEXT("fps"), FPS);
	Result->SetNumberField(TEXT("deltaTime"), DeltaTime);

	// Memory stats
	FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	TSharedPtr<FJsonObject> MemoryObj = MakeShared<FJsonObject>();
	MemoryObj->SetNumberField(TEXT("usedPhysical"), static_cast<double>(MemStats.UsedPhysical));
	MemoryObj->SetNumberField(TEXT("availablePhysical"), static_cast<double>(MemStats.AvailablePhysical));
	MemoryObj->SetNumberField(TEXT("usedVirtual"), static_cast<double>(MemStats.UsedVirtual));
	MemoryObj->SetNumberField(TEXT("availableVirtual"), static_cast<double>(MemStats.AvailableVirtual));
	Result->SetObjectField(TEXT("memory"), MemoryObj);

	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetOutputLog(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	int32 MaxLines = 100;
	if (Params->HasField(TEXT("maxLines")))
	{
		MaxLines = static_cast<int32>(Params->GetNumberField(TEXT("maxLines")));
	}

	FString Filter;
	Params->TryGetStringField(TEXT("filter"), Filter);
	FString Category;
	Params->TryGetStringField(TEXT("category"), Category);

	// Read from ring-buffer log capture (#82)
	TArray<FMCPLogCapture::FMCPLogLine> RecentLines = FMCPLogCapture::Get().GetRecentLines(MaxLines * 2); // over-fetch for filtering

	TArray<TSharedPtr<FJsonValue>> LinesArray;
	for (const FMCPLogCapture::FMCPLogLine& Line : RecentLines)
	{
		if (!Filter.IsEmpty() && !Line.Message.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (!Category.IsEmpty() && !Line.Category.Contains(Category, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TSharedPtr<FJsonObject> LineObj = MakeShared<FJsonObject>();
		LineObj->SetStringField(TEXT("message"), Line.Message);
		LineObj->SetStringField(TEXT("category"), Line.Category);
		LineObj->SetStringField(TEXT("verbosity"), Line.Verbosity);
		LinesArray.Add(MakeShared<FJsonValueObject>(LineObj));

		if (LinesArray.Num() >= MaxLines) break;
	}

	Result->SetArrayField(TEXT("lines"), LinesArray);
	Result->SetNumberField(TEXT("lineCount"), LinesArray.Num());
	Result->SetNumberField(TEXT("maxLines"), MaxLines);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SearchLog(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Query;
	if (!Params->TryGetStringField(TEXT("query"), Query))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'query' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	int32 MaxResults = 100;
	if (Params->HasField(TEXT("maxResults")))
	{
		MaxResults = static_cast<int32>(Params->GetNumberField(TEXT("maxResults")));
	}

	// Search ring-buffer log capture (#82)
	TArray<FMCPLogCapture::FMCPLogLine> Matches = FMCPLogCapture::Get().Search(Query, MaxResults);

	TArray<TSharedPtr<FJsonValue>> MatchesArray;
	for (const FMCPLogCapture::FMCPLogLine& Line : Matches)
	{
		TSharedPtr<FJsonObject> MatchObj = MakeShared<FJsonObject>();
		MatchObj->SetStringField(TEXT("message"), Line.Message);
		MatchObj->SetStringField(TEXT("category"), Line.Category);
		MatchObj->SetStringField(TEXT("verbosity"), Line.Verbosity);
		MatchesArray.Add(MakeShared<FJsonValueObject>(MatchObj));
	}

	Result->SetArrayField(TEXT("matches"), MatchesArray);
	Result->SetNumberField(TEXT("matchCount"), MatchesArray.Num());
	Result->SetStringField(TEXT("query"), Query);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetMessageLog(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// FMessageLog does not expose a simple API to read back entries in C++.
	// Return success with an empty messages array as a baseline implementation.
	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	Result->SetArrayField(TEXT("messages"), MessagesArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetBuildStatus(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// Basic build status - report as idle since we cannot easily query
	// the live compilation state from within the editor module.
	Result->SetStringField(TEXT("status"), TEXT("idle"));
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::PieControl(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Action;
	if (!Params->TryGetStringField(TEXT("action"), Action))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'action' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (!GEditor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (Action == TEXT("status"))
	{
		bool bIsPlaying = (GEditor->PlayWorld != nullptr);
		Result->SetBoolField(TEXT("isPlaying"), bIsPlaying);
		Result->SetStringField(TEXT("action"), Action);
		Result->SetBoolField(TEXT("success"), true);
	}
	else if (Action == TEXT("start"))
	{
		if (GEditor->PlayWorld != nullptr)
		{
			Result->SetStringField(TEXT("error"), TEXT("PIE session already active"));
			Result->SetBoolField(TEXT("success"), false);
		}
		else
		{
			FRequestPlaySessionParams SessionParams;
			GEditor->RequestPlaySession(SessionParams);
			Result->SetStringField(TEXT("action"), Action);
			Result->SetBoolField(TEXT("success"), true);
		}
	}
	else if (Action == TEXT("stop"))
	{
		if (GEditor->PlayWorld == nullptr)
		{
			Result->SetStringField(TEXT("error"), TEXT("No PIE session active"));
			Result->SetBoolField(TEXT("success"), false);
		}
		else
		{
			GEditor->RequestEndPlayMap();
			Result->SetStringField(TEXT("action"), Action);
			Result->SetBoolField(TEXT("success"), true);
		}
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s. Expected 'status', 'start', or 'stop'"), *Action));
		Result->SetBoolField(TEXT("success"), false);
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::CaptureScreenshot(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Filename;
	if (!Params->TryGetStringField(TEXT("filename"), Filename))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'filename' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Ensure the filename has a proper extension
	if (!Filename.EndsWith(TEXT(".png")) && !Filename.EndsWith(TEXT(".jpg")) && !Filename.EndsWith(TEXT(".bmp")))
	{
		Filename += TEXT(".png");
	}

	// #64: Force the active level viewport to render even if the editor window is not focused
	if (!GEditor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient)
	{
		const TArray<FLevelEditorViewportClient*>& ViewportClients = GEditor->GetLevelViewportClients();
		if (ViewportClients.Num() > 0)
		{
			ViewportClient = ViewportClients[0];
		}
	}

	if (!ViewportClient)
	{
		Result->SetStringField(TEXT("error"), TEXT("No level viewport available for screenshot"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Force a viewport redraw to ensure we capture current state
	ViewportClient->Invalidate();

	// Make the viewport's output path explicit so FScreenshotRequest picks it up
	FString FullPath = Filename;
	if (!FPaths::IsRelative(Filename))
	{
		// Already absolute
	}
	else
	{
		FullPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), Filename);
	}

	// Request the screenshot
	FScreenshotRequest::RequestScreenshot(FullPath, false, false);

	Result->SetStringField(TEXT("filename"), FullPath);
	Result->SetStringField(TEXT("note"), TEXT("Screenshot queued. The file will be written asynchronously by the renderer."));
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SetViewportCamera(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient)
	{
		const TArray<FLevelEditorViewportClient*>& ViewportClients = GEditor->GetLevelViewportClients();
		if (ViewportClients.Num() > 0)
		{
			ViewportClient = ViewportClients[0];
		}
	}

	if (!ViewportClient)
	{
		Result->SetStringField(TEXT("error"), TEXT("No viewport client available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set location if provided
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocationObj) && LocationObj)
	{
		FVector Location;
		Location.X = (*LocationObj)->GetNumberField(TEXT("x"));
		Location.Y = (*LocationObj)->GetNumberField(TEXT("y"));
		Location.Z = (*LocationObj)->GetNumberField(TEXT("z"));
		ViewportClient->SetViewLocation(Location);
	}

	// Set rotation if provided
	const TSharedPtr<FJsonObject>* RotationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation"), RotationObj) && RotationObj)
	{
		FRotator Rotation;
		Rotation.Pitch = (*RotationObj)->GetNumberField(TEXT("pitch"));
		Rotation.Yaw = (*RotationObj)->GetNumberField(TEXT("yaw"));
		Rotation.Roll = (*RotationObj)->GetNumberField(TEXT("roll"));
		ViewportClient->SetViewRotation(Rotation);
	}

	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::Undo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bSuccess = GEditor->UndoTransaction();
	Result->SetBoolField(TEXT("success"), bSuccess);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::Redo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bSuccess = GEditor->RedoTransaction();
	Result->SetBoolField(TEXT("success"), bSuccess);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::ReloadHandlers(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// No-op in C++ bridge - this was used in the Python bridge to reload Python handler modules.
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SaveAsset(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bSuccess = UEditorAssetLibrary::SaveAsset(AssetPath);

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("success"), bSuccess);
	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath));
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SaveAll(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// Save all dirty packages using FEditorFileUtils
	bool bPromptUserToSave = false;
	bool bSaveMapPackages = true;
	bool bSaveContentPackages = true;
	bool bSuccess = FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages);

	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("message"), bSuccess ? TEXT("All dirty packages saved") : TEXT("Some packages may have failed to save"));

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetCrashReports(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString CrashesDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Crashes"));
	IFileManager& FileManager = IFileManager::Get();

	TArray<TSharedPtr<FJsonValue>> CrashesArray;

	if (FileManager.DirectoryExists(*CrashesDir))
	{
		// Find all subdirectories in Crashes folder
		TArray<FString> CrashFolders;
		FileManager.FindFiles(CrashFolders, *FPaths::Combine(CrashesDir, TEXT("*")), false, true);

		for (const FString& FolderName : CrashFolders)
		{
			FString FolderPath = FPaths::Combine(CrashesDir, FolderName);

			TSharedPtr<FJsonObject> CrashObj = MakeShared<FJsonObject>();
			CrashObj->SetStringField(TEXT("folder"), FolderName);
			CrashObj->SetStringField(TEXT("path"), FolderPath);

			// Get folder timestamp
			FDateTime TimeStamp = FileManager.GetTimeStamp(*FolderPath);
			if (TimeStamp != FDateTime::MinValue())
			{
				CrashObj->SetStringField(TEXT("timestamp"), TimeStamp.ToString());
			}

			// List files inside the crash folder
			TArray<FString> CrashFiles;
			FileManager.FindFiles(CrashFiles, *FPaths::Combine(FolderPath, TEXT("*")), true, false);

			TArray<TSharedPtr<FJsonValue>> FilesArray;
			for (const FString& FileName : CrashFiles)
			{
				FilesArray.Add(MakeShared<FJsonValueString>(FileName));
			}
			CrashObj->SetArrayField(TEXT("files"), FilesArray);

			CrashesArray.Add(MakeShared<FJsonValueObject>(CrashObj));
		}
	}

	Result->SetStringField(TEXT("crashesDir"), CrashesDir);
	Result->SetNumberField(TEXT("crashCount"), CrashesArray.Num());
	Result->SetArrayField(TEXT("crashes"), CrashesArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::ReadEditorLog(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// Parameters
	int32 LastN = 100;
	if (Params->HasField(TEXT("lastN")))
	{
		LastN = static_cast<int32>(Params->GetNumberField(TEXT("lastN")));
	}

	FString Filter;
	Params->TryGetStringField(TEXT("filter"), Filter);

	// Locate the editor log file
	FString LogDir = FPaths::ProjectLogDir();
	FString LogFilePath = FPaths::Combine(LogDir, TEXT("Editor.log"));

	// If Editor.log doesn't exist, try the current log file
	if (!FPaths::FileExists(LogFilePath))
	{
		LogFilePath = FPaths::Combine(LogDir, FString(FApp::GetProjectName()) + TEXT(".log"));
	}

	if (!FPaths::FileExists(LogFilePath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor log file not found"));
		Result->SetStringField(TEXT("logDir"), LogDir);
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Read the log file into lines
	TArray<FString> AllLines;
	if (!FFileHelper::LoadFileToStringArray(AllLines, *LogFilePath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to read editor log file"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Apply filter and take last N lines
	TArray<FString> ResultLines;
	if (Filter.IsEmpty())
	{
		// No filter - take the last N lines directly
		int32 StartIndex = FMath::Max(0, AllLines.Num() - LastN);
		for (int32 i = StartIndex; i < AllLines.Num(); ++i)
		{
			ResultLines.Add(AllLines[i]);
		}
	}
	else
	{
		// Filter lines (case-insensitive) then take last N
		FString FilterLower = Filter.ToLower();
		TArray<FString> FilteredLines;
		for (const FString& Line : AllLines)
		{
			if (Line.ToLower().Contains(FilterLower))
			{
				FilteredLines.Add(Line);
			}
		}
		int32 StartIndex = FMath::Max(0, FilteredLines.Num() - LastN);
		for (int32 i = StartIndex; i < FilteredLines.Num(); ++i)
		{
			ResultLines.Add(FilteredLines[i]);
		}
	}

	// Convert to JSON array
	TArray<TSharedPtr<FJsonValue>> LinesArray;
	for (const FString& Line : ResultLines)
	{
		LinesArray.Add(MakeShared<FJsonValueString>(Line));
	}

	Result->SetStringField(TEXT("logFile"), LogFilePath);
	Result->SetNumberField(TEXT("lineCount"), ResultLines.Num());
	Result->SetNumberField(TEXT("totalLines"), AllLines.Num());
	Result->SetArrayField(TEXT("lines"), LinesArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::PieGetRuntimeValue(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Check if PIE is active
	if (GEditor->PlayWorld == nullptr)
	{
		Result->SetStringField(TEXT("error"), TEXT("PIE is not active. Start a PIE session first."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ActorPath;
	if (!Params->TryGetStringField(TEXT("actorPath"), ActorPath))
	{
		// Also accept actorLabel as a fallback
		if (!Params->TryGetStringField(TEXT("actorLabel"), ActorPath))
		{
			Result->SetStringField(TEXT("error"), TEXT("Missing 'actorPath' parameter"));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}
	}

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("propertyName"), PropertyName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'propertyName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Search for the actor in the PIE world
	UWorld* PIEWorld = GEditor->PlayWorld;
	AActor* TargetActor = nullptr;

	for (TActorIterator<AActor> It(PIEWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetName() == ActorPath ||
			Actor->GetActorLabel() == ActorPath ||
			Actor->GetPathName() == ActorPath)
		{
			TargetActor = Actor;
			break;
		}
	}

	if (!TargetActor)
	{
		// Collect available actor names for the error message
		TArray<TSharedPtr<FJsonValue>> AvailableActors;
		int32 Count = 0;
		for (TActorIterator<AActor> It(PIEWorld); It && Count < 20; ++It, ++Count)
		{
			AvailableActors.Add(MakeShared<FJsonValueString>((*It)->GetActorLabel()));
		}
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' not found in PIE world"), *ActorPath));
		Result->SetArrayField(TEXT("availableActors"), AvailableActors);
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the property via reflection
	FProperty* Property = TargetActor->GetClass()->FindPropertyByName(*PropertyName);
	if (!Property)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Property '%s' not found on actor '%s'"), *PropertyName, *ActorPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Read property value and serialize based on type
	const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(TargetActor);

	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString Value = StrProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("String"));
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool Value = BoolProp->GetPropertyValue(PropertyValue);
		Result->SetBoolField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Bool"));
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		int32 Value = IntProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Int"));
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		float Value = FloatProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Float"));
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		double Value = DoubleProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Double"));
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FName Value = NameProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value.ToString());
		Result->SetStringField(TEXT("type"), TEXT("Name"));
	}
	else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		FText Value = TextProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value.ToString());
		Result->SetStringField(TEXT("type"), TEXT("Text"));
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		// Handle common struct types: FVector, FRotator, FLinearColor
		if (StructProp->Struct == TBaseStructure<FVector>::Get())
		{
			const FVector* Vec = reinterpret_cast<const FVector*>(PropertyValue);
			TSharedPtr<FJsonObject> VecObj = MakeShared<FJsonObject>();
			VecObj->SetNumberField(TEXT("x"), Vec->X);
			VecObj->SetNumberField(TEXT("y"), Vec->Y);
			VecObj->SetNumberField(TEXT("z"), Vec->Z);
			Result->SetObjectField(TEXT("value"), VecObj);
			Result->SetStringField(TEXT("type"), TEXT("Vector"));
		}
		else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
		{
			const FRotator* Rot = reinterpret_cast<const FRotator*>(PropertyValue);
			TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
			RotObj->SetNumberField(TEXT("pitch"), Rot->Pitch);
			RotObj->SetNumberField(TEXT("yaw"), Rot->Yaw);
			RotObj->SetNumberField(TEXT("roll"), Rot->Roll);
			Result->SetObjectField(TEXT("value"), RotObj);
			Result->SetStringField(TEXT("type"), TEXT("Rotator"));
		}
		else if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
		{
			const FLinearColor* Color = reinterpret_cast<const FLinearColor*>(PropertyValue);
			TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
			ColorObj->SetNumberField(TEXT("r"), Color->R);
			ColorObj->SetNumberField(TEXT("g"), Color->G);
			ColorObj->SetNumberField(TEXT("b"), Color->B);
			ColorObj->SetNumberField(TEXT("a"), Color->A);
			Result->SetObjectField(TEXT("value"), ColorObj);
			Result->SetStringField(TEXT("type"), TEXT("LinearColor"));
		}
		else
		{
			// Generic struct: export to string
			FString ExportedValue;
			Property->ExportTextItem_Direct(ExportedValue, PropertyValue, nullptr, nullptr, PPF_None);
			Result->SetStringField(TEXT("value"), ExportedValue);
			Result->SetStringField(TEXT("type"), StructProp->Struct->GetName());
		}
	}
	else
	{
		// Fallback: export property value as string
		FString ExportedValue;
		Property->ExportTextItem_Direct(ExportedValue, PropertyValue, nullptr, nullptr, PPF_None);
		Result->SetStringField(TEXT("value"), ExportedValue);
		Result->SetStringField(TEXT("type"), Property->GetCPPType());
	}

	Result->SetStringField(TEXT("actorPath"), ActorPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::BuildLighting(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString Quality;
	if (!Params->TryGetStringField(TEXT("quality"), Quality))
	{
		Quality = TEXT("Preview");
	}

	// Map quality string to console command
	FString Command;
	if (Quality == TEXT("Preview"))
	{
		Command = TEXT("BUILD LIGHTING QUALITY=Preview");
	}
	else if (Quality == TEXT("Medium"))
	{
		Command = TEXT("BUILD LIGHTING QUALITY=Medium");
	}
	else if (Quality == TEXT("High"))
	{
		Command = TEXT("BUILD LIGHTING QUALITY=High");
	}
	else if (Quality == TEXT("Production"))
	{
		Command = TEXT("BUILD LIGHTING QUALITY=Production");
	}
	else
	{
		Command = TEXT("BUILD LIGHTING QUALITY=Preview");
	}

	UKismetSystemLibrary::ExecuteConsoleCommand(
		GEditor->GetEditorWorldContext().World(),
		Command,
		nullptr
	);

	Result->SetStringField(TEXT("quality"), Quality);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Lighting build triggered (%s)"), *Quality));
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::BuildAll(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();

	// Execute full build: geometry, lighting, and paths
	UKismetSystemLibrary::ExecuteConsoleCommand(World, TEXT("MAP REBUILD"), nullptr);
	UKismetSystemLibrary::ExecuteConsoleCommand(World, TEXT("BUILD LIGHTING"), nullptr);
	UKismetSystemLibrary::ExecuteConsoleCommand(World, TEXT("RebuildNavigation"), nullptr);

	Result->SetStringField(TEXT("message"), TEXT("Build All triggered (geometry + lighting + navigation)"));
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::ValidateAssets(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Directory;
	if (!Params->TryGetStringField(TEXT("directory"), Directory))
	{
		Directory = TEXT("/Game/");
	}

	// Try to use the EditorValidatorSubsystem if available
	UEditorValidatorSubsystem* ValidatorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>() : nullptr;

	if (ValidatorSubsystem)
	{
		// Use the DataValidation console command for broad validation
		if (GEditor && GEditor->GetEditorWorldContext().World())
		{
			FString Command = FString::Printf(TEXT("DataValidation.ValidateAssets %s"), *Directory);
			UKismetSystemLibrary::ExecuteConsoleCommand(
				GEditor->GetEditorWorldContext().World(),
				Command,
				nullptr
			);
		}

		Result->SetStringField(TEXT("directory"), Directory);
		Result->SetStringField(TEXT("message"), TEXT("Asset validation triggered via EditorValidatorSubsystem"));
		Result->SetBoolField(TEXT("success"), true);
	}
	else
	{
		// Fallback: trigger via console command
		if (GEditor && GEditor->GetEditorWorldContext().World())
		{
			UKismetSystemLibrary::ExecuteConsoleCommand(
				GEditor->GetEditorWorldContext().World(),
				TEXT("DataValidation.ValidateAssets"),
				nullptr
			);
		}

		Result->SetStringField(TEXT("directory"), Directory);
		Result->SetStringField(TEXT("message"), TEXT("Asset validation triggered via console command"));
		Result->SetBoolField(TEXT("success"), true);
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::CookContent(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString Platform;
	if (!Params->TryGetStringField(TEXT("platform"), Platform))
	{
		Platform = TEXT("Windows");
	}

	FString Command = FString::Printf(TEXT("CookOnTheFly -TargetPlatform=%s"), *Platform);
	UKismetSystemLibrary::ExecuteConsoleCommand(
		GEditor->GetEditorWorldContext().World(),
		Command,
		nullptr
	);

	Result->SetStringField(TEXT("platform"), Platform);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Cook triggered for %s"), *Platform));
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::FocusViewportOnActor(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the actor by label
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorLabel)
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get the viewport client
	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient)
	{
		const TArray<FLevelEditorViewportClient*>& ViewportClients = GEditor->GetLevelViewportClients();
		if (ViewportClients.Num() > 0)
		{
			ViewportClient = ViewportClients[0];
		}
	}

	if (!ViewportClient)
	{
		Result->SetStringField(TEXT("error"), TEXT("No viewport client available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Focus on the actor's bounding box
	FBox ActorBounds = TargetActor->GetComponentsBoundingBox(true);
	if (ActorBounds.IsValid)
	{
		ViewportClient->FocusViewportOnBox(ActorBounds);
	}
	else
	{
		// Fallback: just move the camera to the actor's location
		FVector ActorLocation = TargetActor->GetActorLocation();
		FVector CameraOffset(0.0, -500.0, 200.0);
		ViewportClient->SetViewLocation(ActorLocation + CameraOffset);
		FRotator LookAt = (ActorLocation - (ActorLocation + CameraOffset)).Rotation();
		ViewportClient->SetViewRotation(LookAt);
	}

	FVector FinalLocation = ViewportClient->GetViewLocation();
	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), FinalLocation.X);
	LocObj->SetNumberField(TEXT("y"), FinalLocation.Y);
	LocObj->SetNumberField(TEXT("z"), FinalLocation.Z);
	Result->SetObjectField(TEXT("viewLocation"), LocObj);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::HotReload(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

#if PLATFORM_WINDOWS
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (LiveCoding && LiveCoding->IsEnabledForSession())
	{
		if (LiveCoding->IsCompiling())
		{
			Result->SetStringField(TEXT("message"), TEXT("Live Coding compile already in progress"));
			Result->SetBoolField(TEXT("success"), true);
			return MakeShared<FJsonValueObject>(Result);
		}

		LiveCoding->EnableByDefault(true);
		LiveCoding->Compile();
		Result->SetStringField(TEXT("message"), TEXT("Live Coding compile triggered"));
		Result->SetBoolField(TEXT("success"), true);
	}
	else
#endif
	{
		// Live Coding not available (or not on Windows) - fall back to console command
		if (GEditor && GEditor->GetEditorWorldContext().World())
		{
			UKismetSystemLibrary::ExecuteConsoleCommand(
				GEditor->GetEditorWorldContext().World(),
				TEXT("LiveCoding.Compile"),
				nullptr
			);
			Result->SetStringField(TEXT("message"), TEXT("Hot reload triggered via console command (Live Coding module not active in session)"));
			Result->SetBoolField(TEXT("success"), true);
		}
		else
		{
			Result->SetStringField(TEXT("error"), TEXT("Neither Live Coding module nor editor world available for hot reload"));
			Result->SetBoolField(TEXT("success"), false);
		}
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::CreateNewLevel(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString LevelPath;
	if (!Params->TryGetStringField(TEXT("levelPath"), LevelPath))
	{
		LevelPath = TEXT("");
	}

	FString TemplateLevel;
	Params->TryGetStringField(TEXT("templateLevel"), TemplateLevel);

	ULevelEditorSubsystem* LevelEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!LevelEditorSubsystem)
	{
		Result->SetStringField(TEXT("error"), TEXT("LevelEditorSubsystem not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bSuccess = false;
	if (TemplateLevel.IsEmpty())
	{
		bSuccess = LevelEditorSubsystem->NewLevel(LevelPath);
	}
	else
	{
		bSuccess = LevelEditorSubsystem->NewLevelFromTemplate(LevelPath, TemplateLevel);
	}

	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to create new level at: %s"), *LevelPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get info about the new world
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		Result->SetStringField(TEXT("worldName"), World->GetName());
		Result->SetStringField(TEXT("worldPath"), World->GetPathName());
	}

	Result->SetStringField(TEXT("levelPath"), LevelPath);
	Result->SetStringField(TEXT("message"), TEXT("New level created"));
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SaveCurrentLevel(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LevelEditorSubsystem)
	{
		Result->SetStringField(TEXT("error"), TEXT("LevelEditorSubsystem not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bSuccess = LevelEditorSubsystem->SaveCurrentLevel();

	Result->SetStringField(TEXT("levelName"), World->GetName());
	Result->SetStringField(TEXT("levelPath"), World->GetPathName());
	Result->SetBoolField(TEXT("success"), bSuccess);

	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to save current level"));
	}
	else
	{
		Result->SetStringField(TEXT("message"), TEXT("Current level saved"));
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::OpenAsset(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if ((!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath)) || AssetPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Asset)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load asset at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (!GEditor)
	{
		Result->SetStringField(TEXT("error"), TEXT("GEditor not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		Result->SetStringField(TEXT("error"), TEXT("AssetEditorSubsystem not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bOpened = AssetEditorSubsystem->OpenEditorForAsset(Asset);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
	Result->SetBoolField(TEXT("success"), bOpened);
	if (!bOpened)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to open editor for '%s' (%s)"), *AssetPath, *Asset->GetClass()->GetName()));
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::RunStatCommand(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString Command = TEXT("stat fps");
	Params->TryGetStringField(TEXT("command"), Command);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world"));
		return MakeShared<FJsonValueObject>(Result);
	}

	GEditor->Exec(World, *Command);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SetScalability(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString Level = TEXT("Epic");
	Params->TryGetStringField(TEXT("level"), Level);

	int32 Idx = 3; // Default to Epic
	if (Level == TEXT("Low")) Idx = 0;
	else if (Level == TEXT("Medium")) Idx = 1;
	else if (Level == TEXT("High")) Idx = 2;
	else if (Level == TEXT("Epic")) Idx = 3;
	else if (Level == TEXT("Cinematic")) Idx = 4;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world"));
		return MakeShared<FJsonValueObject>(Result);
	}

	TArray<FString> Commands = {
		FString::Printf(TEXT("sg.ViewDistanceQuality %d"), Idx),
		FString::Printf(TEXT("sg.AntiAliasingQuality %d"), Idx),
		FString::Printf(TEXT("sg.ShadowQuality %d"), Idx),
		FString::Printf(TEXT("sg.GlobalIlluminationQuality %d"), Idx),
		FString::Printf(TEXT("sg.ReflectionQuality %d"), Idx),
		FString::Printf(TEXT("sg.PostProcessQuality %d"), Idx),
		FString::Printf(TEXT("sg.TextureQuality %d"), Idx),
		FString::Printf(TEXT("sg.EffectsQuality %d"), Idx),
		FString::Printf(TEXT("sg.FoliageQuality %d"), Idx),
		FString::Printf(TEXT("sg.ShadingQuality %d"), Idx),
	};

	for (const FString& Cmd : Commands)
	{
		GEditor->Exec(World, *Cmd);
	}

	Result->SetStringField(TEXT("level"), Level);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::BuildGeometry(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world"));
		return MakeShared<FJsonValueObject>(Result);
	}
	GEditor->Exec(World, TEXT("MAP REBUILD"));
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("Geometry rebuild triggered"));
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::BuildHlod(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world"));
		return MakeShared<FJsonValueObject>(Result);
	}
	GEditor->Exec(World, TEXT("BuildHLOD"));
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("HLOD build triggered"));
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::ListCrashes(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString CrashesDir = FPaths::ProjectSavedDir() / TEXT("Crashes");
	Result->SetStringField(TEXT("crashesDir"), CrashesDir);

	TArray<TSharedPtr<FJsonValue>> CrashArray;
	IFileManager& FileManager = IFileManager::Get();

	TArray<FString> CrashFolders;
	FileManager.FindFiles(CrashFolders, *(CrashesDir / TEXT("*")), false, true);

	for (const FString& Folder : CrashFolders)
	{
		TSharedPtr<FJsonObject> CrashObj = MakeShared<FJsonObject>();
		FString FullPath = CrashesDir / Folder;
		CrashObj->SetStringField(TEXT("folder"), Folder);
		CrashObj->SetStringField(TEXT("path"), FullPath);

		FFileStatData StatData = FileManager.GetStatData(*FullPath);
		if (StatData.bIsValid)
		{
			CrashObj->SetNumberField(TEXT("modified"), StatData.ModificationTime.ToUnixTimestamp());
		}

		TArray<FString> Files;
		FileManager.FindFiles(Files, *(FullPath / TEXT("*")), true, false);
		TArray<TSharedPtr<FJsonValue>> FileArray;
		for (const FString& File : Files)
		{
			FileArray.Add(MakeShared<FJsonValueString>(File));
		}
		CrashObj->SetArrayField(TEXT("files"), FileArray);
		CrashArray.Add(MakeShared<FJsonValueObject>(CrashObj));
	}

	Result->SetNumberField(TEXT("crashCount"), CrashArray.Num());
	Result->SetArrayField(TEXT("crashes"), CrashArray);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetCrashInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString CrashFolder;
	if (!Params->TryGetStringField(TEXT("crashFolder"), CrashFolder))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'crashFolder' parameter"));
		return MakeShared<FJsonValueObject>(Result);
	}

	FString CrashPath = FPaths::ProjectSavedDir() / TEXT("Crashes") / CrashFolder;
	if (!IFileManager::Get().DirectoryExists(*CrashPath))
	{
		Result->SetBoolField(TEXT("available"), false);
		Result->SetStringField(TEXT("note"), FString::Printf(TEXT("Crash folder not found: %s"), *CrashFolder));
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("folder"), CrashFolder);
	Result->SetStringField(TEXT("path"), CrashPath);

	TSharedPtr<FJsonObject> FilesObj = MakeShared<FJsonObject>();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(CrashPath / TEXT("*")), true, false);

	for (const FString& File : Files)
	{
		TSharedPtr<FJsonObject> FileInfo = MakeShared<FJsonObject>();
		FString FilePath = CrashPath / File;
		FFileStatData StatData = IFileManager::Get().GetStatData(*FilePath);
		if (StatData.bIsValid)
		{
			FileInfo->SetNumberField(TEXT("size"), StatData.FileSize);
			FileInfo->SetNumberField(TEXT("modified"), StatData.ModificationTime.ToUnixTimestamp());
		}

		// Read text files
		if (File.EndsWith(TEXT(".log")) || File.EndsWith(TEXT(".txt")) || File.EndsWith(TEXT(".xml")) || File.EndsWith(TEXT(".json")))
		{
			FString Content;
			if (FFileHelper::LoadFileToString(Content, *FilePath))
			{
				// Limit content to 50KB
				if (Content.Len() > 50000)
				{
					Content = Content.Left(50000) + TEXT("\n... [truncated]");
				}
				FileInfo->SetStringField(TEXT("content"), Content);
			}
		}
		FilesObj->SetObjectField(File, FileInfo);
	}

	Result->SetObjectField(TEXT("files"), FilesObj);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::CheckForCrashes(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString CrashesDir = FPaths::ProjectSavedDir() / TEXT("Crashes");

	TArray<TSharedPtr<FJsonValue>> RecentCrashes;
	IFileManager& FileManager = IFileManager::Get();
	FDateTime Now = FDateTime::UtcNow();
	FDateTime Threshold = Now - FTimespan::FromHours(24);

	TArray<FString> CrashFolders;
	FileManager.FindFiles(CrashFolders, *(CrashesDir / TEXT("*")), false, true);

	for (const FString& Folder : CrashFolders)
	{
		FString FullPath = CrashesDir / Folder;
		FFileStatData StatData = FileManager.GetStatData(*FullPath);
		if (StatData.bIsValid && StatData.ModificationTime > Threshold)
		{
			TSharedPtr<FJsonObject> CrashObj = MakeShared<FJsonObject>();
			CrashObj->SetStringField(TEXT("folder"), Folder);
			CrashObj->SetStringField(TEXT("path"), FullPath);
			CrashObj->SetNumberField(TEXT("timestamp"), StatData.ModificationTime.ToUnixTimestamp());
			RecentCrashes.Add(MakeShared<FJsonValueObject>(CrashObj));
		}
	}

	Result->SetNumberField(TEXT("recentCrashCount"), RecentCrashes.Num());
	Result->SetArrayField(TEXT("recentCrashes"), RecentCrashes);
	return MakeShared<FJsonValueObject>(Result);
}

// #14: Build project via UnrealBuildTool
TSharedPtr<FJsonValue> FEditorHandlers::BuildProject(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString Configuration = TEXT("Development");
	Params->TryGetStringField(TEXT("configuration"), Configuration);

	FString Platform = TEXT("Win64");
	Params->TryGetStringField(TEXT("platform"), Platform);

	bool bClean = false;
	Params->TryGetBoolField(TEXT("clean"), bClean);

	// Build the project by invoking the engine's build tool
	// Use the project path from the running editor
	FString ProjectPath = FPaths::GetProjectFilePath();
	if (ProjectPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("No project file path available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find UnrealBuildTool
	FString EngineDir = FPaths::EngineDir();
	FString UBTPath;

#if PLATFORM_WINDOWS
	UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool"), TEXT("UnrealBuildTool.exe"));
	if (!FPaths::FileExists(UBTPath))
	{
		// Try legacy path
		UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool.exe"));
	}
#else
	UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool"), TEXT("UnrealBuildTool"));
#endif

	if (!FPaths::FileExists(UBTPath))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("UnrealBuildTool not found at '%s'"), *UBTPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Build the command line
	FString ProjectName = FPaths::GetBaseFilename(ProjectPath);
	FString Args = FString::Printf(
		TEXT("%sEditor %s %s -Project=\"%s\" -WaitMutex -FromMsBuild"),
		*ProjectName, *Platform, *Configuration, *ProjectPath);

	if (bClean)
	{
		Args += TEXT(" -Clean");
	}

	// Launch the process asynchronously
	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		*UBTPath, *Args, true, false, false, nullptr, 0, nullptr, nullptr);

	if (!ProcHandle.IsValid())
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to launch UnrealBuildTool"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("ubtPath"), UBTPath);
	Result->SetStringField(TEXT("args"), Args);
	Result->SetStringField(TEXT("configuration"), Configuration);
	Result->SetStringField(TEXT("platform"), Platform);
	Result->SetStringField(TEXT("note"), TEXT("Build launched asynchronously. Check output log for progress."));
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

// #49: Generate VS project files
TSharedPtr<FJsonValue> FEditorHandlers::GenerateProjectFiles(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ProjectPath = FPaths::GetProjectFilePath();
	if (ProjectPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("No project file path available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the GenerateProjectFiles script
	FString EngineDir = FPaths::EngineDir();
	FString ScriptPath;
#if PLATFORM_WINDOWS
	ScriptPath = FPaths::Combine(EngineDir, TEXT("Build"), TEXT("BatchFiles"), TEXT("GenerateProjectFiles.bat"));
#elif PLATFORM_MAC
	ScriptPath = FPaths::Combine(EngineDir, TEXT("Build"), TEXT("BatchFiles"), TEXT("Mac"), TEXT("GenerateProjectFiles.sh"));
#else
	ScriptPath = FPaths::Combine(EngineDir, TEXT("Build"), TEXT("BatchFiles"), TEXT("Linux"), TEXT("GenerateProjectFiles.sh"));
#endif

	if (!FPaths::FileExists(ScriptPath))
	{
		// Alternative: use UnrealBuildTool directly with -projectfiles flag
		FString UBTPath;
#if PLATFORM_WINDOWS
		UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool"), TEXT("UnrealBuildTool.exe"));
		if (!FPaths::FileExists(UBTPath))
		{
			UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool.exe"));
		}
#else
		UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool"), TEXT("UnrealBuildTool"));
#endif
		if (!FPaths::FileExists(UBTPath))
		{
			Result->SetStringField(TEXT("error"), TEXT("Neither GenerateProjectFiles script nor UnrealBuildTool found"));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		FString Args = FString::Printf(TEXT("-projectfiles -project=\"%s\" -game -rocket -progress"), *ProjectPath);
		FProcHandle ProcHandle = FPlatformProcess::CreateProc(
			*UBTPath, *Args, true, false, false, nullptr, 0, nullptr, nullptr);

		if (!ProcHandle.IsValid())
		{
			Result->SetStringField(TEXT("error"), TEXT("Failed to launch UnrealBuildTool for project file generation"));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		Result->SetStringField(TEXT("tool"), UBTPath);
		Result->SetStringField(TEXT("args"), Args);
	}
	else
	{
		FString Args = FString::Printf(TEXT("-project=\"%s\" -game"), *ProjectPath);
		FProcHandle ProcHandle = FPlatformProcess::CreateProc(
			*ScriptPath, *Args, true, false, false, nullptr, 0, nullptr, nullptr);

		if (!ProcHandle.IsValid())
		{
			Result->SetStringField(TEXT("error"), TEXT("Failed to launch GenerateProjectFiles"));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		Result->SetStringField(TEXT("tool"), ScriptPath);
		Result->SetStringField(TEXT("args"), Args);
	}

	Result->SetStringField(TEXT("projectPath"), ProjectPath);
	Result->SetStringField(TEXT("note"), TEXT("Project file generation launched. Check output log for progress."));
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}
