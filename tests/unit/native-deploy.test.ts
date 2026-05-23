import { describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { deployNativeModule, undeployNativeModule, writeNativeModulesState } from "../../src/plugin/native-deploy.js";

function makeTempRoot(): string {
  return fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-native-deploy-"));
}

function trySymlinkDir(target: string, link: string): boolean {
  try {
    fs.symlinkSync(target, link, process.platform === "win32" ? "junction" : "dir");
    return true;
  } catch {
    return false;
  }
}

describe("deployNativeModule", () => {
  it("copies only from inside the plugin package to project Plugins", () => {
    const root = makeTempRoot();
    const pkgDir = path.join(root, "pkg");
    const projectDir = path.join(root, "project");
    const sourceDir = path.join(pkgDir, "ue", "Plugins", "VoxelPCGBridge");
    fs.mkdirSync(sourceDir, { recursive: true });
    fs.mkdirSync(projectDir, { recursive: true });
    fs.writeFileSync(path.join(sourceDir, "VoxelPCGBridge.uplugin"), "{}");

    const result = deployNativeModule(pkgDir, "ue/Plugins/VoxelPCGBridge", "VoxelPCGBridge", projectDir);

    expect(result.fileList).toEqual(["Plugins/VoxelPCGBridge/VoxelPCGBridge.uplugin"]);
    expect(fs.existsSync(path.join(projectDir, result.fileList[0]))).toBe(true);
  });

  it("rejects source paths that escape the package", () => {
    const root = makeTempRoot();
    const pkgDir = path.join(root, "pkg");
    const projectDir = path.join(root, "project");
    fs.mkdirSync(path.join(root, "outside"), { recursive: true });
    fs.mkdirSync(pkgDir, { recursive: true });
    fs.mkdirSync(projectDir, { recursive: true });

    expect(() => deployNativeModule(pkgDir, "../outside", "VoxelPCGBridge", projectDir))
      .toThrow(/must stay inside/);
  });

  it("rejects plugin names that escape project Plugins", () => {
    const root = makeTempRoot();
    const pkgDir = path.join(root, "pkg");
    const projectDir = path.join(root, "project");
    const sourceDir = path.join(pkgDir, "src");
    fs.mkdirSync(sourceDir, { recursive: true });
    fs.mkdirSync(projectDir, { recursive: true });

    expect(() => deployNativeModule(pkgDir, "src", "../Escape", projectDir))
      .toThrow(/simple Unreal plugin identifier/);
  });

  it("rejects source paths that pass through a symlink or junction", () => {
    const root = makeTempRoot();
    const pkgDir = path.join(root, "pkg");
    const projectDir = path.join(root, "project");
    const outsideSource = path.join(root, "outside-source");
    const link = path.join(pkgDir, "linked");
    fs.mkdirSync(outsideSource, { recursive: true });
    fs.mkdirSync(pkgDir, { recursive: true });
    fs.mkdirSync(projectDir, { recursive: true });
    fs.writeFileSync(path.join(outsideSource, "VoxelPCGBridge.uplugin"), "{}");
    if (!trySymlinkDir(outsideSource, link)) return;

    expect(() => deployNativeModule(pkgDir, "linked", "VoxelPCGBridge", projectDir))
      .toThrow(/symbolic links|real path/);
  });

  it("rejects an existing destination plugin symlink or junction", () => {
    const root = makeTempRoot();
    const pkgDir = path.join(root, "pkg");
    const projectDir = path.join(root, "project");
    const sourceDir = path.join(pkgDir, "src");
    const outsideDest = path.join(root, "outside-dest");
    const destLink = path.join(projectDir, "Plugins", "VoxelPCGBridge");
    fs.mkdirSync(sourceDir, { recursive: true });
    fs.mkdirSync(outsideDest, { recursive: true });
    fs.mkdirSync(path.dirname(destLink), { recursive: true });
    fs.writeFileSync(path.join(sourceDir, "VoxelPCGBridge.uplugin"), "{}");
    if (!trySymlinkDir(outsideDest, destLink)) return;

    expect(() => deployNativeModule(pkgDir, "src", "VoxelPCGBridge", projectDir))
      .toThrow(/symbolic links|junctions|real path/);
  });

  it("preflights source symlinks before copying any files", () => {
    const root = makeTempRoot();
    const pkgDir = path.join(root, "pkg");
    const projectDir = path.join(root, "project");
    const sourceDir = path.join(pkgDir, "src");
    const outsideSource = path.join(root, "outside-source");
    fs.mkdirSync(sourceDir, { recursive: true });
    fs.mkdirSync(outsideSource, { recursive: true });
    fs.mkdirSync(projectDir, { recursive: true });
    fs.writeFileSync(path.join(sourceDir, "Safe.uplugin"), "{}");
    if (!trySymlinkDir(outsideSource, path.join(sourceDir, "linked"))) return;

    expect(() => deployNativeModule(pkgDir, "src", "VoxelPCGBridge", projectDir))
      .toThrow(/symbolic links/);
    expect(fs.existsSync(path.join(projectDir, "Plugins", "VoxelPCGBridge", "Safe.uplugin")))
      .toBe(false);
  });

  it("does not undeploy recorded files outside the project", () => {
    const root = makeTempRoot();
    const projectDir = path.join(root, "project");
    const safeFile = path.join(projectDir, "Plugins", "VoxelPCGBridge", "VoxelPCGBridge.uplugin");
    const outsideFile = path.join(root, "outside.txt");
    fs.mkdirSync(path.dirname(safeFile), { recursive: true });
    fs.writeFileSync(safeFile, "{}");
    fs.writeFileSync(outsideFile, "keep me");

    writeNativeModulesState(projectDir, {
      "ue-mcp-plugin-test": {
        uePluginName: "VoxelPCGBridge",
        pluginVersion: "1.0.0",
        installedAt: new Date(0).toISOString(),
        files: [
          "Plugins/VoxelPCGBridge/VoxelPCGBridge.uplugin",
          "../outside.txt",
        ],
      },
    });

    expect(undeployNativeModule(projectDir, "ue-mcp-plugin-test")).toBe(1);
    expect(fs.existsSync(safeFile)).toBe(false);
    expect(fs.readFileSync(outsideFile, "utf-8")).toBe("keep me");
  });

  it("does not undeploy recorded files outside the recorded plugin subtree", () => {
    const root = makeTempRoot();
    const projectDir = path.join(root, "project");
    const safeFile = path.join(projectDir, "Plugins", "VoxelPCGBridge", "VoxelPCGBridge.uplugin");
    const otherPluginFile = path.join(projectDir, "Plugins", "OtherPlugin", "OtherPlugin.uplugin");
    const configFile = path.join(projectDir, "Config", "DefaultGame.ini");
    fs.mkdirSync(path.dirname(safeFile), { recursive: true });
    fs.mkdirSync(path.dirname(otherPluginFile), { recursive: true });
    fs.mkdirSync(path.dirname(configFile), { recursive: true });
    fs.writeFileSync(safeFile, "{}");
    fs.writeFileSync(otherPluginFile, "{}");
    fs.writeFileSync(configFile, "[/Script/EngineSettings.GeneralProjectSettings]");

    writeNativeModulesState(projectDir, {
      "ue-mcp-plugin-test": {
        uePluginName: "VoxelPCGBridge",
        pluginVersion: "1.0.0",
        installedAt: new Date(0).toISOString(),
        files: [
          "Plugins/VoxelPCGBridge/VoxelPCGBridge.uplugin",
          "Plugins/OtherPlugin/OtherPlugin.uplugin",
          "Config/DefaultGame.ini",
        ],
      },
    });

    expect(undeployNativeModule(projectDir, "ue-mcp-plugin-test")).toBe(1);
    expect(fs.existsSync(safeFile)).toBe(false);
    expect(fs.existsSync(otherPluginFile)).toBe(true);
    expect(fs.existsSync(configFile)).toBe(true);
  });
});
