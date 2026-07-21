import { BaseTask } from "@db-lyon/flowkit";

export default class ExampleTask extends BaseTask {
  get taskName() {
    return "fixture.example";
  }

  async execute() {
    return { success: true, data: { resolved: true } };
  }
}
