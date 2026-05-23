import { describe, expect, it } from "vitest";
import { AddressInfo } from "node:net";
import WebSocket, { WebSocketServer } from "ws";
import { EditorBridge } from "../../src/bridge.js";

async function makeServer(): Promise<{ server: WebSocketServer; port: number }> {
  const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  await new Promise<void>((resolve) => server.once("listening", resolve));
  return { server, port: (server.address() as AddressInfo).port };
}

describe("EditorBridge.configure", () => {
  it("updates the websocket endpoint before connecting", () => {
    const bridge = new EditorBridge();

    bridge.configure("127.0.0.1", 9878);

    expect(bridge.host).toBe("127.0.0.1");
    expect(bridge.port).toBe(9878);
  });

  it("ignores stale close events after reconnecting to a new endpoint", async () => {
    const first = await makeServer();
    const second = await makeServer();
    const firstConnection = new Promise<WebSocket>((resolve) => first.server.once("connection", resolve));
    const secondConnection = new Promise<WebSocket>((resolve) => second.server.once("connection", resolve));

    const bridge = new EditorBridge("127.0.0.1", first.port);

    try {
      await bridge.connect(1000);
      const oldSocket = await firstConnection;
      bridge.configure("127.0.0.1", second.port);
      await bridge.connect(1000);
      await secondConnection;

      oldSocket.close();
      await new Promise((resolve) => setTimeout(resolve, 20));

      expect(bridge.isConnected).toBe(true);
    } finally {
      bridge.disconnect();
      first.server.close();
      second.server.close();
    }
  });
});
