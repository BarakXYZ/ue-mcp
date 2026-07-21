import * as path from "node:path";
import { fileURLToPath } from "node:url";

import { describe, expect, it } from "vitest";

import { ProjectTaskRegistry } from "../../src/flow/project-task-registry.js";

const fixtureProjectDir = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "../fixtures/project-flow",
);

describe("ProjectTaskRegistry", () => {
  it("resolves ./ class paths relative to the project config directory", async () => {
    const registry = new ProjectTaskRegistry(fixtureProjectDir);

    const taskClass = await registry.resolve("./tasks/ExampleTask");
    const task = new taskClass({}, {});
    const result = await task.run();

    expect(result).toEqual({
      success: true,
      data: { resolved: true },
      duration: expect.any(Number),
    });
  });

  it.each(["./..", "./../outside/ExampleTask"])(
    "refuses project task path %s when it escapes the project root",
    async (classPath) => {
      const registry = new ProjectTaskRegistry(fixtureProjectDir);

      await expect(registry.resolve(classPath)).rejects.toThrow(
        "escapes the project root",
      );
    },
  );
});
