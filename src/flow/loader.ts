import * as fs from "node:fs";
import * as path from "node:path";
import { loadConfig, type LoadedConfig } from "@db-lyon/flowkit";
import { FlowConfigSchema, type FlowConfig } from "./schema.js";
import type { ToolDef } from "../types.js";

/**
 * Build the defaults object from tool definitions.
 * This is the runtime equivalent of scripts/generate-default-config.ts.
 */
export function buildDefaults(tools: ToolDef[]): Record<string, unknown> {
  const tasks: Record<string, unknown> = {};

  for (const tool of tools) {
    for (const [actionName, spec] of Object.entries(tool.actions)) {
      const taskName = `${tool.name}.${actionName}`;
      const isBridge = !!spec.bridge;

      const taskDef: Record<string, unknown> = {
        class_path: isBridge ? "ue-mcp.bridge" : taskName,
        group: tool.name,
      };
      if (spec.description) taskDef.description = spec.description;
      if (isBridge) taskDef.options = { method: spec.bridge };

      tasks[taskName] = taskDef;
    }
  }

  // Built-in shell task
  tasks["shell"] = {
    class_path: "shell",
    group: "util",
    description: "Run a shell command. Params: command, cwd?, timeout?",
  };

  return { tasks, flows: defaultFlows() };
}

/** Built-in flows that ship with ue-mcp. */
function defaultFlows(): Record<string, unknown> {
  return {
    beacon: {
      description:
        "Demo — create a dramatic light-sculpture scene with atmosphere, colored lights, and a material graph",
      steps: {
        // ── Scene foundation ──
        "1": {
          task: "level.create",
          options: { levelPath: "/Game/Flows/Beacon" },
        },
        "2": {
          task: "level.place_actor",
          options: { actorClass: "SkyAtmosphere", label: "Sky" },
        },
        "3": {
          task: "level.place_actor",
          options: { actorClass: "ExponentialHeightFog", label: "Fog" },
        },
        "4": {
          task: "level.place_actor",
          options: { actorClass: "SkyLight", label: "Ambient" },
        },

        // ── Sunset sun ──
        "5": {
          task: "level.spawn_light",
          options: { lightType: "directional", label: "Sun", intensity: 10 },
        },
        "6": {
          task: "level.set_light_properties",
          options: { actorLabel: "Sun", color: { r: 255, g: 160, b: 80 } },
        },
        "7": {
          task: "level.move_actor",
          options: { actorLabel: "Sun", rotation: { pitch: -25, yaw: -135 } },
        },

        // ── Light ring (3 colored point lights) ──
        "8": {
          task: "level.spawn_light",
          options: {
            lightType: "point",
            label: "Beacon_Cyan",
            location: { x: 200, y: 0, z: 300 },
            intensity: 80000,
          },
        },
        "9": {
          task: "level.set_light_properties",
          options: {
            actorLabel: "Beacon_Cyan",
            color: { r: 0, g: 200, b: 255 },
          },
        },
        "10": {
          task: "level.spawn_light",
          options: {
            lightType: "point",
            label: "Beacon_Magenta",
            location: { x: -100, y: 175, z: 300 },
            intensity: 80000,
          },
        },
        "11": {
          task: "level.set_light_properties",
          options: {
            actorLabel: "Beacon_Magenta",
            color: { r: 255, g: 0, b: 200 },
          },
        },
        "12": {
          task: "level.spawn_light",
          options: {
            lightType: "point",
            label: "Beacon_Gold",
            location: { x: -100, y: -175, z: 300 },
            intensity: 80000,
          },
        },
        "13": {
          task: "level.set_light_properties",
          options: {
            actorLabel: "Beacon_Gold",
            color: { r: 255, g: 200, b: 0 },
          },
        },

        // ── Center spotlight ──
        "14": {
          task: "level.spawn_light",
          options: {
            lightType: "spot",
            label: "CenterSpot",
            location: { x: 0, y: 0, z: 500 },
            intensity: 300000,
          },
        },
        "15": {
          task: "level.move_actor",
          options: {
            actorLabel: "CenterSpot",
            rotation: { pitch: -90, yaw: 0 },
          },
        },

        // ── Emissive material (demonstrates graph authoring) ──
        "16": {
          task: "material.create",
          options: { name: "M_Beacon_Glow", packagePath: "/Game/Flows/Beacon" },
        },
        "17": {
          task: "material.add_expression",
          options: {
            materialPath: "/Game/Flows/Beacon/M_Beacon_Glow",
            expressionType: "VectorParameter",
            name: "GlowColor",
            parameterName: "GlowColor",
          },
        },
        "18": {
          task: "material.add_expression",
          options: {
            materialPath: "/Game/Flows/Beacon/M_Beacon_Glow",
            expressionType: "Constant",
            name: "GlowStrength",
          },
        },
        "19": {
          task: "material.set_expression_value",
          options: {
            materialPath: "/Game/Flows/Beacon/M_Beacon_Glow",
            expressionIndex: 1,
            value: 25,
          },
        },
        "20": {
          task: "material.add_expression",
          options: {
            materialPath: "/Game/Flows/Beacon/M_Beacon_Glow",
            expressionType: "Multiply",
            name: "Multiply",
          },
        },
        "21": {
          task: "material.connect_expressions",
          options: {
            materialPath: "/Game/Flows/Beacon/M_Beacon_Glow",
            sourceExpression: "GlowColor",
            targetExpression: "Multiply",
            targetInput: "A",
          },
        },
        "22": {
          task: "material.connect_expressions",
          options: {
            materialPath: "/Game/Flows/Beacon/M_Beacon_Glow",
            sourceExpression: "GlowStrength",
            targetExpression: "Multiply",
            targetInput: "B",
          },
        },
        "23": {
          task: "material.connect_to_property",
          options: {
            materialPath: "/Game/Flows/Beacon/M_Beacon_Glow",
            expressionName: "Multiply",
            property: "EmissiveColor",
          },
        },
        "24": {
          task: "material.recompile",
          options: {
            materialPath: "/Game/Flows/Beacon/M_Beacon_Glow",
          },
        },

        // ── Camera ──
        "25": {
          task: "editor.set_viewport",
          options: {
            location: { x: -600, y: 0, z: 250 },
            rotation: { pitch: -10, yaw: 0 },
          },
        },
      },
    },
  };
}

/**
 * Load ue-mcp.yml from the given directory, layered on top of built-in defaults.
 * Returns the merged config even if no project ue-mcp.yml exists.
 */
export function loadFlowConfig(
  tools: ToolDef[],
  configDir?: string,
): LoadedConfig<FlowConfig> | null {
  const dir = configDir ?? process.cwd();
  const configPath = path.join(dir, "ue-mcp.yml");

  if (!fs.existsSync(configPath)) return null;

  return loadConfig({
    filename: "ue-mcp.yml",
    schema: FlowConfigSchema,
    defaults: buildDefaults(tools),
    envVar: "UE_MCP_ENV",
    configDir: dir,
  });
}
