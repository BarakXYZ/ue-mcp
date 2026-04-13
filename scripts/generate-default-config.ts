#!/usr/bin/env tsx
/**
 * Generate the default ue-mcp.yml from code annotations.
 *
 * Walks ALL_TOOLS, reads each action's description + bridge/handler metadata,
 * and emits a YAML config with every built-in task declared.
 *
 * Run: tsx scripts/generate-default-config.ts
 * Output: dist/ue-mcp.default.yml
 */

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

import { projectTool } from "../src/tools/project.js";
import { assetTool } from "../src/tools/asset.js";
import { blueprintTool } from "../src/tools/blueprint.js";
import { levelTool } from "../src/tools/level.js";
import { materialTool } from "../src/tools/material.js";
import { animationTool } from "../src/tools/animation.js";
import { landscapeTool } from "../src/tools/landscape.js";
import { pcgTool } from "../src/tools/pcg.js";
import { foliageTool } from "../src/tools/foliage.js";
import { niagaraTool } from "../src/tools/niagara.js";
import { audioTool } from "../src/tools/audio.js";
import { widgetTool } from "../src/tools/widget.js";
import { editorTool } from "../src/tools/editor.js";
import { reflectionTool } from "../src/tools/reflection.js";
import { gameplayTool } from "../src/tools/gameplay.js";
import { gasTool } from "../src/tools/gas.js";
import { networkingTool } from "../src/tools/networking.js";
import { demoTool } from "../src/tools/demo.js";
import { feedbackTool } from "../src/tools/feedback.js";
import type { ToolDef } from "../src/types.js";

const ALL_TOOLS: ToolDef[] = [
  projectTool, assetTool, blueprintTool, levelTool, materialTool,
  animationTool, landscapeTool, pcgTool, foliageTool, niagaraTool,
  audioTool, widgetTool, editorTool, reflectionTool, gameplayTool,
  gasTool, networkingTool, demoTool, feedbackTool,
];

function generate(): string {
  const lines: string[] = [];

  lines.push("# Auto-generated — do not edit by hand.");
  lines.push("# Source: scripts/generate-default-config.ts");
  lines.push("");
  lines.push("ue-mcp:");
  lines.push("  version: 1");
  lines.push("");
  lines.push("tasks:");

  for (const tool of ALL_TOOLS) {
    lines.push("");
    lines.push(`  # ── ${tool.name} ──`);

    for (const [actionName, spec] of Object.entries(tool.actions)) {
      const taskName = `${tool.name}.${actionName}`;
      const desc = spec.description;
      const isBridge = !!spec.bridge;
      // Handler tasks are registered by name; bridge tasks use the generic class_path
      const classPath = isBridge ? "ue-mcp.bridge" : taskName;

      lines.push(`  ${taskName}:`);
      lines.push(`    class_path: ${classPath}`);
      lines.push(`    group: ${tool.name}`);
      if (desc) {
        lines.push(`    description: ${yamlString(desc)}`);
      }
      if (isBridge) {
        lines.push(`    options:`);
        lines.push(`      method: ${spec.bridge}`);
      }
    }
  }

  lines.push("");
  lines.push("flows:");
  lines.push("");
  lines.push("  beacon:");
  lines.push("    description: \"Demo — create a dramatic light-sculpture scene with atmosphere, colored lights, and a material graph\"");
  lines.push("    steps:");
  lines.push("      1:");
  lines.push("        task: level.create");
  lines.push("        options:");
  lines.push("          levelPath: /Game/Flows/Beacon");
  lines.push("      2:");
  lines.push("        task: level.place_actor");
  lines.push("        options:");
  lines.push("          actorClass: SkyAtmosphere");
  lines.push("          label: Sky");
  lines.push("      3:");
  lines.push("        task: level.place_actor");
  lines.push("        options:");
  lines.push("          actorClass: ExponentialHeightFog");
  lines.push("          label: Fog");
  lines.push("      4:");
  lines.push("        task: level.place_actor");
  lines.push("        options:");
  lines.push("          actorClass: SkyLight");
  lines.push("          label: Ambient");
  lines.push("      # ── Sunset sun ──");
  lines.push("      5:");
  lines.push("        task: level.spawn_light");
  lines.push("        options:");
  lines.push("          lightType: directional");
  lines.push("          label: Sun");
  lines.push("          intensity: 10");
  lines.push("      6:");
  lines.push("        task: level.set_light_properties");
  lines.push("        options:");
  lines.push("          actorLabel: Sun");
  lines.push("          color: { r: 255, g: 160, b: 80 }");
  lines.push("      7:");
  lines.push("        task: level.move_actor");
  lines.push("        options:");
  lines.push("          actorLabel: Sun");
  lines.push("          rotation: { pitch: -25, yaw: -135 }");
  lines.push("      # ── Light ring ──");
  lines.push("      8:");
  lines.push("        task: level.spawn_light");
  lines.push("        options:");
  lines.push("          lightType: point");
  lines.push("          label: Beacon_Cyan");
  lines.push("          location: { x: 200, y: 0, z: 300 }");
  lines.push("          intensity: 80000");
  lines.push("      9:");
  lines.push("        task: level.set_light_properties");
  lines.push("        options:");
  lines.push("          actorLabel: Beacon_Cyan");
  lines.push("          color: { r: 0, g: 200, b: 255 }");
  lines.push("      10:");
  lines.push("        task: level.spawn_light");
  lines.push("        options:");
  lines.push("          lightType: point");
  lines.push("          label: Beacon_Magenta");
  lines.push("          location: { x: -100, y: 175, z: 300 }");
  lines.push("          intensity: 80000");
  lines.push("      11:");
  lines.push("        task: level.set_light_properties");
  lines.push("        options:");
  lines.push("          actorLabel: Beacon_Magenta");
  lines.push("          color: { r: 255, g: 0, b: 200 }");
  lines.push("      12:");
  lines.push("        task: level.spawn_light");
  lines.push("        options:");
  lines.push("          lightType: point");
  lines.push("          label: Beacon_Gold");
  lines.push("          location: { x: -100, y: -175, z: 300 }");
  lines.push("          intensity: 80000");
  lines.push("      13:");
  lines.push("        task: level.set_light_properties");
  lines.push("        options:");
  lines.push("          actorLabel: Beacon_Gold");
  lines.push("          color: { r: 255, g: 200, b: 0 }");
  lines.push("      # ── Center spotlight ──");
  lines.push("      14:");
  lines.push("        task: level.spawn_light");
  lines.push("        options:");
  lines.push("          lightType: spot");
  lines.push("          label: CenterSpot");
  lines.push("          location: { x: 0, y: 0, z: 500 }");
  lines.push("          intensity: 300000");
  lines.push("      15:");
  lines.push("        task: level.move_actor");
  lines.push("        options:");
  lines.push("          actorLabel: CenterSpot");
  lines.push("          rotation: { pitch: -90, yaw: 0 }");
  lines.push("      # ── Emissive material ──");
  lines.push("      16:");
  lines.push("        task: material.create");
  lines.push("        options:");
  lines.push("          name: M_Beacon_Glow");
  lines.push("          packagePath: /Game/Flows/Beacon");
  lines.push("      17:");
  lines.push("        task: material.add_expression");
  lines.push("        options:");
  lines.push("          materialPath: /Game/Flows/Beacon/M_Beacon_Glow");
  lines.push("          expressionType: VectorParameter");
  lines.push("          name: GlowColor");
  lines.push("          parameterName: GlowColor");
  lines.push("      18:");
  lines.push("        task: material.add_expression");
  lines.push("        options:");
  lines.push("          materialPath: /Game/Flows/Beacon/M_Beacon_Glow");
  lines.push("          expressionType: Constant");
  lines.push("          name: GlowStrength");
  lines.push("      19:");
  lines.push("        task: material.set_expression_value");
  lines.push("        options:");
  lines.push("          materialPath: /Game/Flows/Beacon/M_Beacon_Glow");
  lines.push("          expressionIndex: 1");
  lines.push("          value: 25");
  lines.push("      20:");
  lines.push("        task: material.add_expression");
  lines.push("        options:");
  lines.push("          materialPath: /Game/Flows/Beacon/M_Beacon_Glow");
  lines.push("          expressionType: Multiply");
  lines.push("          name: Multiply");
  lines.push("      21:");
  lines.push("        task: material.connect_expressions");
  lines.push("        options:");
  lines.push("          materialPath: /Game/Flows/Beacon/M_Beacon_Glow");
  lines.push("          sourceExpression: GlowColor");
  lines.push("          targetExpression: Multiply");
  lines.push("          targetInput: A");
  lines.push("      22:");
  lines.push("        task: material.connect_expressions");
  lines.push("        options:");
  lines.push("          materialPath: /Game/Flows/Beacon/M_Beacon_Glow");
  lines.push("          sourceExpression: GlowStrength");
  lines.push("          targetExpression: Multiply");
  lines.push("          targetInput: B");
  lines.push("      23:");
  lines.push("        task: material.connect_to_property");
  lines.push("        options:");
  lines.push("          materialPath: /Game/Flows/Beacon/M_Beacon_Glow");
  lines.push("          expressionName: Multiply");
  lines.push("          property: EmissiveColor");
  lines.push("      24:");
  lines.push("        task: material.recompile");
  lines.push("        options:");
  lines.push("          materialPath: /Game/Flows/Beacon/M_Beacon_Glow");
  lines.push("      # ── Camera ──");
  lines.push("      25:");
  lines.push("        task: editor.set_viewport");
  lines.push("        options:");
  lines.push("          location: { x: -600, y: 0, z: 250 }");
  lines.push("          rotation: { pitch: -10, yaw: 0 }");
  lines.push("");

  return lines.join("\n");
}

/** Escape a string for inline YAML (quote if it contains special chars). */
function yamlString(s: string): string {
  if (/[:#\[\]{}&*!|>'"%@`]/.test(s) || s.includes("\n")) {
    return `"${s.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;
  }
  return s;
}

// ── Main ──────────────────────────────────────────────────────────

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, "..");
const outDir = path.join(root, "dist");

if (!fs.existsSync(outDir)) {
  fs.mkdirSync(outDir, { recursive: true });
}

const yaml = generate();
const outPath = path.join(outDir, "ue-mcp.default.yml");
fs.writeFileSync(outPath, yaml, "utf-8");

const taskCount = (yaml.match(/^\s{2}\w+\.\w+:/gm) ?? []).length;
console.log(`[generate] ${outPath} — ${taskCount} tasks`);
