import type { TaskContext } from "@db-lyon/flowkit";
import type { IBridge } from "../bridge.js";
import type { ProjectContext } from "../project.js";

export interface FlowContext extends TaskContext {
  bridge: IBridge;
  project: ProjectContext;
  getFlows?: () => Array<{ name: string; description?: string }>;
}
