import * as fs from "node:fs";
import * as path from "node:path";
import { pathToFileURL } from "node:url";

import {
  BaseTask,
  TaskRegistry,
  type TaskConstructor,
} from "@db-lyon/flowkit";

function isProjectRelativeClassPath(classPath: string): boolean {
  return classPath.startsWith("./") || classPath.startsWith(".\\");
}

function projectTaskCandidates(projectDir: string, classPath: string): string[] {
  const resolvedProjectDir = path.resolve(projectDir);
  const resolvedClassPath = path.resolve(resolvedProjectDir, classPath);
  const relativePath = path.relative(resolvedProjectDir, resolvedClassPath);
  if (
    relativePath === ".." ||
    relativePath.startsWith(`..${path.sep}`) ||
    path.isAbsolute(relativePath)
  ) {
    throw new Error(
      `Project task class_path "${classPath}" escapes the project root "${resolvedProjectDir}".`,
    );
  }

  const extension = path.extname(resolvedClassPath).toLowerCase();
  if (extension === ".ts" || extension === ".js") {
    return [resolvedClassPath];
  }
  return [
    `${resolvedClassPath}.ts`,
    `${resolvedClassPath}.js`,
    path.join(resolvedClassPath, "index.ts"),
    path.join(resolvedClassPath, "index.js"),
  ];
}

/**
 * Resolves explicit ./ project tasks from the directory containing ue-mcp.yml.
 * Other names and class paths retain Flowkit's built-in resolution behavior.
 */
export class ProjectTaskRegistry extends TaskRegistry {
  private readonly projectTaskCache = new Map<string, TaskConstructor>();

  constructor(private readonly projectDir?: string) {
    super();
  }

  override async resolve(classPathOrName: string): Promise<TaskConstructor> {
    if (!this.projectDir || !isProjectRelativeClassPath(classPathOrName)) {
      return super.resolve(classPathOrName);
    }

    const cached = this.projectTaskCache.get(classPathOrName);
    if (cached) return cached;

    const candidates = projectTaskCandidates(this.projectDir, classPathOrName);
    const resolvedPath = candidates.find((candidate) => {
      try {
        return fs.statSync(candidate).isFile();
      } catch {
        return false;
      }
    });
    if (!resolvedPath) {
      throw new Error(
        `Cannot resolve project task "${classPathOrName}" relative to "${this.projectDir}". Searched:\n` +
          candidates.map((candidate) => `  - ${candidate}`).join("\n"),
      );
    }

    const module = await import(pathToFileURL(resolvedPath).href) as Record<string, unknown>;
    const baseName = path.basename(resolvedPath, path.extname(resolvedPath));
    const taskClass = module.default ?? module[baseName];
    if (typeof taskClass !== "function") {
      throw new Error(
        `Module "${resolvedPath}" does not export a default class or a named export matching "${baseName}".`,
      );
    }
    if (!(taskClass.prototype instanceof BaseTask)) {
      throw new Error(`Task class from "${resolvedPath}" does not extend BaseTask.`);
    }

    const constructor = taskClass as TaskConstructor;
    this.projectTaskCache.set(classPathOrName, constructor);
    return constructor;
  }
}
