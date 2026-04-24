#!/usr/bin/env node
/**
 * Claude Code hook handler — shipped with ue-mcp so it stays in sync.
 *
 * Usage (from .claude/settings.json):
 *   "command": "npx ue-mcp hook post-tool-use"
 *
 * Reads the hook payload from stdin, emits JSON to stdout when the
 * agent needs a nudge (e.g. after execute_python workarounds).
 */

interface HookInput {
  tool_name?: string;
  tool_input?: Record<string, unknown>;
  tool_output?: string;
  [key: string]: unknown;
}

function readStdin(): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    process.stdin.on("data", (chunk) => chunks.push(chunk));
    process.stdin.on("end", () => resolve(Buffer.concat(chunks).toString("utf-8")));
    process.stdin.on("error", reject);
    // If stdin is already closed (piped empty), resolve quickly
    if (process.stdin.readableEnded) resolve("");
  });
}

async function postToolUse(): Promise<void> {
  let input: HookInput;
  try {
    const raw = await readStdin();
    if (!raw.trim()) return;
    input = JSON.parse(raw);
  } catch (e) {
    // We cannot log to stderr without breaking the Claude Code hook contract
    // (stderr is surfaced to the agent). Write to a rotating diagnostic file
    // so the failure is not completely invisible during debugging.
    try {
      const os = await import("node:os");
      const fs = await import("node:fs");
      const path = await import("node:path");
      fs.appendFileSync(
        path.join(os.tmpdir(), "ue-mcp-hook.log"),
        `${new Date().toISOString()} post-tool-use: unparseable stdin :: ${e instanceof Error ? e.message : String(e)}\n`,
      );
    } catch {
      // logging is best-effort; the hook itself must not throw
    }
    return;
  }

  if (input.tool_input?.action !== "execute_python") return;

  const msg = JSON.stringify({
    systemMessage: [
      "[MANDATORY] execute_python was just used as a workaround — a native ue-mcp tool could not handle this task.",
      "In your NEXT message to the user, you MUST say:",
      '"I had to use execute_python to <describe what and why>. Would you like to submit feedback so this can become a native tool?"',
      "If the user agrees, call feedback(action=\"submit\") with title, summary, pythonWorkaround, and idealTool.",
      "Do NOT skip or defer this step.",
    ].join("\n"),
  });

  process.stdout.write(msg);
}

/* ── Subcommand dispatch ─────────────────────────────────────────── */

const event = process.argv[3]; // argv: [node, script, "hook", event]

switch (event) {
  case "post-tool-use":
    postToolUse().catch(() => process.exit(0));
    break;
  default:
    // Unknown event — silent no-op so we never break the agent
    break;
}
