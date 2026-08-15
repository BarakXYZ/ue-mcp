import { afterEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

import {
  buildProject,
  isProjectEditorProcessCandidate,
  resolveEditorTarget,
} from "../../src/editor-control.js";

const originalBuildToolPath = process.env.UE_BUILD_TOOL_PATH;
const originalCapturePath = process.env.UE_MCP_TEST_BUILD_ARGS_PATH;

afterEach(() => {
  if (originalBuildToolPath === undefined) delete process.env.UE_BUILD_TOOL_PATH;
  else process.env.UE_BUILD_TOOL_PATH = originalBuildToolPath;

  if (originalCapturePath === undefined) delete process.env.UE_MCP_TEST_BUILD_ARGS_PATH;
  else process.env.UE_MCP_TEST_BUILD_ARGS_PATH = originalCapturePath;
});

describe("buildProject", () => {
  it("fails closed when multiple editor targets cannot be disambiguated", () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-targets-"));
    const projectPath = path.join(root, "RenamedProject.uproject");
    fs.mkdirSync(path.join(root, "Source"), { recursive: true });
    fs.writeFileSync(projectPath, JSON.stringify({ FileVersion: 3 }));
    fs.writeFileSync(path.join(root, "Source", "GameEditor.Target.cs"), "");
    fs.writeFileSync(path.join(root, "Source", "ToolsEditor.Target.cs"), "");

    expect(() => resolveEditorTarget(projectPath)).toThrow(/Multiple editor targets/);
    expect(resolveEditorTarget(projectPath, "ToolsEditor")).toBe("ToolsEditor");
  });

  it.skipIf(process.platform !== "win32")(
    "invokes a spaced Build.bat path and discovers the real editor target",
    async () => {
      const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-build target-"));
      const projectDir = path.join(root, "City Sample Project");
      const sourceDir = path.join(projectDir, "Source");
      const toolDir = path.join(root, "Engine With Spaces", "Engine", "Build", "BatchFiles");
      fs.mkdirSync(sourceDir, { recursive: true });
      fs.mkdirSync(toolDir, { recursive: true });

      const projectPath = path.join(projectDir, "CitySample_5_8.uproject");
      fs.writeFileSync(
        projectPath,
        JSON.stringify({
          FileVersion: 3,
          Modules: [{ Name: "CitySample", Type: "Runtime" }],
        }),
      );
      fs.writeFileSync(
        path.join(sourceDir, "CitySampleEditor.Target.cs"),
        "public class CitySampleEditorTarget {}\n",
      );

      const capturePath = path.join(root, "captured-build-args.txt");
      const buildToolPath = path.join(toolDir, "Build.bat");
      fs.writeFileSync(
        buildToolPath,
        [
          "@echo off",
          "> \"%UE_MCP_TEST_BUILD_ARGS_PATH%\" echo %~1^|%~2^|%~3^|%~4^|%~5^|%~6",
          "exit /b 0",
          "",
        ].join("\r\n"),
      );

      process.env.UE_BUILD_TOOL_PATH = buildToolPath;
      process.env.UE_MCP_TEST_BUILD_ARGS_PATH = capturePath;

      const result = await buildProject(projectPath);

      expect(result).toMatchObject({ success: true, exitCode: 0 });
      expect(fs.readFileSync(capturePath, "utf8").trim()).toBe(
        [
          "CitySampleEditor",
          "Win64",
          "Development",
          `-Project=${path.resolve(projectPath)}`,
          "-WaitMutex",
          "-FromMsBuild",
        ].join("|"),
      );
    },
  );
});

describe("project editor process discovery", () => {
  const projectPath = "C:\\Users\\Barak\\personal\\xyz\\unreal\\DesktopAvatar.uproject";

  it("accepts the engine editor and discovered project editor targets for the exact project", () => {
    expect(
      isProjectEditorProcessCandidate(
        "UnrealEditor.exe",
        `\"C:\\UE-Source-Builds\\UE_Source\\Engine\\Binaries\\Win64\\UnrealEditor.exe\" \"${projectPath}\"`,
        projectPath,
        ["DesktopAvatarEditor"],
      ),
    ).toBe(true);

    expect(
      isProjectEditorProcessCandidate(
        "DesktopAvatarEditor.exe",
        `\"C:\\Users\\Barak\\personal\\xyz\\unreal\\Binaries\\Win64\\DesktopAvatarEditor.exe\" \"${projectPath}\"`,
        projectPath,
        ["DesktopAvatarEditor"],
      ),
    ).toBe(true);
  });

  it("rejects another target or another project", () => {
    expect(
      isProjectEditorProcessCandidate(
        "CitySampleEditor.exe",
        `\"C:\\CitySample\\Binaries\\Win64\\CitySampleEditor.exe\" \"${projectPath}\"`,
        projectPath,
        ["DesktopAvatarEditor"],
      ),
    ).toBe(false);

    expect(
      isProjectEditorProcessCandidate(
        "DesktopAvatarEditor.exe",
        '"C:\\Users\\Barak\\Documents\\Unreal Projects\\Other\\Other.uproject"',
        projectPath,
        ["DesktopAvatarEditor"],
      ),
    ).toBe(false);
  });
});
