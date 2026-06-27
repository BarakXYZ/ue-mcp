import WebSocket from "ws";
import * as fs from "node:fs";
import * as path from "node:path";
import { McpError, ErrorCode } from "./errors.js";
import { debug, warn } from "./log.js";

// #492: per-project port lockfile published by the bridge plugin. When the
// default port (9877) is taken by another editor, the plugin walks up and
// publishes the actual bound port here. The client reads this before
// falling back to the default port so a second editor finds the right one.
export function readBridgeLockfile(
  uprojectPath: string | null,
): { port: number; pid: number; startedAt?: string; apiVersion?: number } | null {
  if (!uprojectPath) return null;
  const lockfile = path.join(
    path.dirname(uprojectPath),
    "Saved",
    "UE_MCP_Bridge",
    "port.json",
  );
  try {
    const raw = fs.readFileSync(lockfile, "utf8");
    const parsed = JSON.parse(raw);
    if (typeof parsed?.port === "number" && parsed.port > 0 && parsed.port < 65536) {
      return parsed;
    }
  } catch {
    // Missing or unreadable - fall back to default.
  }
  return null;
}

export interface BridgeResponse {
  id: string;
  result?: unknown;
  error?: { code: number; message: string };
}

interface PendingRequest {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
  timer: ReturnType<typeof setTimeout>;
}

/** Minimal interface for tool handlers — enables mocking in tests. */
export interface IBridge {
  readonly isConnected: boolean;
  configure?(host: string, port: number): void;
  call(method: string, params?: Record<string, unknown>, timeoutMs?: number): Promise<unknown>;
  connect(timeoutMs?: number): Promise<void>;
}

export class EditorBridge implements IBridge {
  private ws: WebSocket | null = null;
  private pending = new Map<string, PendingRequest>();
  private reconnectTimer: ReturnType<typeof setInterval> | null = null;
  private connectInFlight: Promise<void> | null = null;
  private idCounter = 0;
  private connectionGeneration = 0;

  constructor(
    // #497: default to 127.0.0.1 so the client picks the loopback IPv4 the
    // plugin actually binds to. "localhost" can resolve to ::1 on systems
    // where the IPv6 stack wins DNS, leaving the client stuck connecting to
    // an empty IPv6 socket while the plugin owns 127.0.0.1:9877.
    // UE_MCP_HOST overrides the default for non-standard topologies.
    public host = process.env.UE_MCP_HOST ?? "127.0.0.1",
    public port = Number.parseInt(process.env.UE_MCP_PORT ?? "", 10) || 9877,
  ) {}

  get isConnected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN;
  }

  configure(host: string, port: number): void {
    if (this.host === host && this.port === port) return;

    this.connectionGeneration++;
    this.host = host;
    this.port = port;
    this.rejectPending("Bridge endpoint changed");

    if (this.ws) {
      const oldSocket = this.ws;
      this.ws = null;
      oldSocket.terminate();
    }
  }

  async ensureConnected(timeoutMs = 5000): Promise<void> {
    if (this.isConnected) return;

    if (!this.connectInFlight) {
      this.connectInFlight = this.connect(timeoutMs).finally(() => {
        this.connectInFlight = null;
      });
    }

    await this.connectInFlight;
  }

  /**
   * #492: project context for resolving the per-project port lockfile. Set
   * by index.ts after the user's .uproject is loaded. Leaving this null
   * keeps the default-port-only behaviour for callers that don't have a
   * project context (CLI tools, tests).
   */
  public projectPathForLockfile: string | null = null;

  async connect(timeoutMs = 3000): Promise<void> {
    if (this.isConnected) return;

    if (this.ws) {
      const oldSocket = this.ws;
      this.ws = null;
      oldSocket.terminate();
    }

    // #492: if a per-project lockfile exists for this .uproject, prefer the
    // port it advertises over the default. Lets multiple editors run side-
    // by-side without their npm clients colliding on 9877.
    const lockfile = readBridgeLockfile(this.projectPathForLockfile);
    if (lockfile && lockfile.port !== this.port) {
      debug("bridge", `lockfile points at port ${lockfile.port}, using it instead of default ${this.port}`);
      this.port = lockfile.port;
    }

    const generation = this.connectionGeneration;
    const url = `ws://${this.host}:${this.port}`;

    return new Promise((resolve, reject) => {
      let settled = false;
      let ws: WebSocket | null = null;
      const fail = (error: Error) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        reject(error);
      };
      const succeed = () => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        resolve();
      };
      const timer = setTimeout(() => {
        ws?.terminate();
        fail(new McpError(ErrorCode.BRIDGE_TIMEOUT, `Connection to editor bridge timed out (${url})`));
      }, timeoutMs);

      ws = new WebSocket(url);

      ws.on("open", () => {
        if (generation !== this.connectionGeneration) {
          ws.terminate();
          fail(new McpError(ErrorCode.CONNECTION_LOST, "Bridge endpoint changed"));
          return;
        }
        this.ws = ws;
        this.setupListeners(ws, generation);
        succeed();
      });

      ws.on("error", (err) => {
        fail(
          new McpError(
            ErrorCode.NOT_CONNECTED,
            `Failed to connect to editor bridge at ${url}: ${err.message}`,
          ),
        );
      });
    });
  }

  startReconnecting(intervalMs = 15000): void {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setInterval(() => {
      if (this.isConnected) return;
      this.connect().then(
        () => { warn("bridge", "editor bridge reconnected"); },
        (e) => { debug("bridge", "reconnect attempt failed (will retry)", e); },
      );
    }, intervalMs);
  }

  stopReconnecting(): void {
    if (this.reconnectTimer) {
      clearInterval(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  async call(method: string, params?: Record<string, unknown>, timeoutMs?: number): Promise<unknown> {
    if (!this.isConnected) {
      await this.ensureConnected();
    }

    const id = String(++this.idCounter);
    const request = { id, method, params: params ?? {} };
    const timeout = timeoutMs ?? 30_000;
    const ws = this.ws;
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      throw new McpError(
        ErrorCode.NOT_CONNECTED,
        "Not connected to editor bridge. Is Unreal Editor running with the MCP bridge plugin?",
      );
    }

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        if (this.ws === ws) {
          ws.terminate();
          this.ws = null;
        }
        reject(new McpError(ErrorCode.BRIDGE_TIMEOUT, `Bridge call '${method}' timed out after ${Math.round(timeout / 1000)}s`));
      }, timeout);

      this.pending.set(id, { resolve, reject, timer });
      ws.send(JSON.stringify(request), (err) => {
        if (!err) return;

        clearTimeout(timer);
        this.pending.delete(id);
        if (this.ws === ws) {
          ws.terminate();
          this.ws = null;
        }
        reject(new McpError(ErrorCode.CONNECTION_LOST, `Failed to send bridge call '${method}': ${err.message}`));
      });
    });
  }

  disconnect(): void {
    this.connectionGeneration++;
    this.stopReconnecting();
    this.rejectPending("Bridge disconnected");
    if (this.ws) {
      const oldSocket = this.ws;
      this.ws = null;
      oldSocket.close();
    }
  }

  private rejectPending(message: string): void {
    for (const [, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(new McpError(ErrorCode.CONNECTION_LOST, message));
    }
    this.pending.clear();
  }

  private setupListeners(ws: WebSocket, generation: number): void {
    ws.on("message", (data) => {
      if (this.ws !== ws || generation !== this.connectionGeneration) return;
      try {
        const msg = JSON.parse(data.toString()) as BridgeResponse;
        const pending = this.pending.get(msg.id);
        if (!pending) return;

        this.pending.delete(msg.id);
        clearTimeout(pending.timer);

        if (msg.error) {
          pending.reject(new McpError(ErrorCode.BRIDGE_ERROR, `Bridge error: ${msg.error.message}`));
        } else {
          pending.resolve(msg.result);
        }
      } catch (e) {
        warn("bridge", "dropped malformed message from editor", e);
      }
    });

    ws.on("close", () => {
      if (this.ws !== ws || generation !== this.connectionGeneration) return;
      this.rejectPending("Bridge connection lost");
      this.ws = null;
    });

    ws.on("error", (err) => {
      // `close` fires next and is where we reject pending calls; log here so
      // the underlying socket error (ECONNRESET, etc.) is not invisible.
      debug("bridge", "websocket error (close will follow)", err);
    });
  }
}
