import { describe, expect, it } from "vitest";
import { editorTool } from "../../src/tools/editor.js";

describe("editor play_in_editor tool", () => {
  it("accepts and forwards Unreal's explicit new-process play mode", () => {
    expect(editorTool.schema.playMode.safeParse("PlayMode_InNewProcess").success).toBe(true);
    expect(editorTool.schema.playMode.safeParse("standalone").success).toBe(false);

    const mapped = editorTool.actions.play_in_editor.mapParams?.({
      action: "play_in_editor",
      pieAction: "start",
      playMode: "PlayMode_InNewProcess",
    });

    expect(mapped).toMatchObject({
      action: "start",
      playMode: "PlayMode_InNewProcess",
    });
  });
});
