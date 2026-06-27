#include "UE_MCP_BridgeModule.h"
#include "Modules/ModuleManager.h"
#include "BridgeServer.h"
#include "Handlers/DialogHandlers.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY(LogMCPBridge);
IMPLEMENT_MODULE(FUE_MCP_BridgeModule, UE_MCP_Bridge)

static TSharedPtr<FMCPBridgeServer> G_BridgeServer;
static constexpr int32 DefaultMCPBridgePort = 9877;

static bool IsValidMCPBridgePort(int32 Port)
{
	return Port > 0 && Port <= 65535;
}

static bool TryParseMCPBridgePortValue(const FString& RawValue, const FString& Source, int32& OutPort)
{
	FString Value = RawValue;
	int32 CommentIndex = INDEX_NONE;
	if (Value.FindChar(TEXT('#'), CommentIndex))
	{
		Value = Value.Left(CommentIndex);
	}

	Value = Value.TrimStartAndEnd();
	if (Value.Len() >= 2 &&
		((Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\""))) ||
		 (Value.StartsWith(TEXT("'")) && Value.EndsWith(TEXT("'")))))
	{
		Value = Value.Mid(1, Value.Len() - 2).TrimStartAndEnd();
	}

	if (!Value.IsNumeric())
	{
		return false;
	}

	const int32 Port = FCString::Atoi(*Value);
	if (!IsValidMCPBridgePort(Port))
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Ignoring invalid bridge.port %d in %s"), Port, *Source);
		return false;
	}

	OutPort = Port;
	return true;
}

static bool TryReadBridgePortFromLegacyJson(int32& OutPort)
{
	const FString ConfigPath = FPaths::Combine(FPaths::ProjectDir(), TEXT(".ue-mcp.json"));
	FString ConfigText;
	if (!FFileHelper::LoadFileToString(ConfigText, *ConfigPath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConfigText);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to parse bridge config at %s"), *ConfigPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* BridgeObject = nullptr;
	if (!RootObject->TryGetObjectField(TEXT("bridge"), BridgeObject) || !BridgeObject || !BridgeObject->IsValid())
	{
		return false;
	}

	double PortNumber = 0;
	if (!(*BridgeObject)->TryGetNumberField(TEXT("port"), PortNumber))
	{
		return false;
	}

	const int32 Port = static_cast<int32>(PortNumber);
	if (!IsValidMCPBridgePort(Port))
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Ignoring invalid bridge.port %d in %s"), Port, *ConfigPath);
		return false;
	}

	OutPort = Port;
	return true;
}

static int32 CountLeadingSpaces(const FString& Line)
{
	int32 Count = 0;
	while (Count < Line.Len() && Line[Count] == TCHAR(' '))
	{
		++Count;
	}
	return Count;
}

static bool TryReadBridgePortFromYaml(int32& OutPort)
{
	const FString ConfigPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("ue-mcp.yml"));
	FString ConfigText;
	if (!FFileHelper::LoadFileToString(ConfigText, *ConfigPath))
	{
		return false;
	}

	TArray<FString> Lines;
	ConfigText.ParseIntoArrayLines(Lines, false);

	bool bInUeMcp = false;
	bool bInBridge = false;
	int32 UeMcpIndent = -1;
	int32 BridgeIndent = -1;

	for (const FString& Line : Lines)
	{
		const FString Trimmed = Line.TrimStart();
		if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("#")))
		{
			continue;
		}

		const int32 Indent = CountLeadingSpaces(Line);
		if (bInBridge && Indent <= BridgeIndent)
		{
			bInBridge = false;
		}
		if (bInUeMcp && Indent <= UeMcpIndent && !Trimmed.StartsWith(TEXT("ue-mcp:")))
		{
			bInUeMcp = false;
			bInBridge = false;
		}

		if (!bInUeMcp && Trimmed.StartsWith(TEXT("ue-mcp:")))
		{
			bInUeMcp = true;
			UeMcpIndent = Indent;
			continue;
		}

		if (bInUeMcp && !bInBridge && Trimmed.StartsWith(TEXT("bridge:")))
		{
			bInBridge = true;
			BridgeIndent = Indent;
			continue;
		}

		if (bInBridge && Trimmed.StartsWith(TEXT("port:")))
		{
			return TryParseMCPBridgePortValue(Trimmed.RightChop(5), ConfigPath, OutPort);
		}
	}

	return false;
}

static int32 GetMCPBridgePort()
{
	int32 Port = DefaultMCPBridgePort;
	TryReadBridgePortFromLegacyJson(Port);
	TryReadBridgePortFromYaml(Port);

	int32 CommandLinePort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("MCPBridgePort="), CommandLinePort))
	{
		if (IsValidMCPBridgePort(CommandLinePort))
		{
			Port = CommandLinePort;
		}
		else
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Ignoring invalid -MCPBridgePort=%d"), CommandLinePort);
		}
	}

	return Port;
}

void FUE_MCP_BridgeModule::StartupModule()
{
	if (IsRunningCommandlet())
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server skipped while running commandlet"));
		return;
	}

	// Create and start bridge server
	const int32 BridgePort = GetMCPBridgePort();
	G_BridgeServer = MakeShared<FMCPBridgeServer>(BridgePort);
	FDialogHandlers::InstallDialogHook();
	// Safety net: auto-decline overwrite dialogs to prevent game thread blocking.
	// Handlers should check for existing assets before creating, but if a dialog
	// slips through, decline it rather than blocking the game thread forever.
	FDialogHandlers::AddDefaultPolicy(TEXT("already exists"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Overwrite"), EAppReturnType::No);
	// Safety-net for the editor's auto "save level / save unsaved" prompts —
	// when an agent session ends or the editor closes, these would otherwise
	// block the main thread waiting on a human. Default to "Discard".
	// (Agents that actually want to persist changes still call project(build)
	//  / level(save) / asset(save) explicitly.)
	FDialogHandlers::AddDefaultPolicy(TEXT("Save Changes"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Save Content"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Unsaved"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("Untitled"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("save your changes"), EAppReturnType::No);
	FDialogHandlers::AddDefaultPolicy(TEXT("save the level"), EAppReturnType::No);

	if (G_BridgeServer->Start())
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server started on port %d"), BridgePort);
	}
	else
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to start bridge server"));
	}

	// Defer the editor-ready signal until GEditor is available and has at least one world.
	// GetEditorWorldContext(false) can fail if no editor world context exists yet,
	// so we iterate all world contexts instead (#162).
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) -> bool
		{
			if (!GEditor)
			{
				return true; // keep ticking — not ready yet
			}

			// Accept any world context (editor or PIE) as proof the editor is usable.
			bool bHasWorld = false;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World())
				{
					bHasWorld = true;
					break;
				}
			}
			if (!bHasWorld)
			{
				return true; // keep ticking
			}

			if (G_BridgeServer.IsValid())
			{
				G_BridgeServer->GetGameThreadExecutor().SetEditorReady();
				UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Editor ready — accepting requests"));
			}

			return false; // done
		})
	);
}

void FUE_MCP_BridgeModule::ShutdownModule()
{
	FDialogHandlers::RemoveDialogHook();

	if (G_BridgeServer.IsValid())
	{
		G_BridgeServer->Shutdown();
		G_BridgeServer.Reset();
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server stopped"));
	}
}
