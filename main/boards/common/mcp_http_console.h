#ifndef MCP_HTTP_CONSOLE_H
#define MCP_HTTP_CONSOLE_H

/**
 * Browser console for exercising the device MCP tools without the LLM.
 *
 * The device is an MCP server inside the XiaoZhi WebSocket/MQTT transport, so
 * a browser cannot reach it directly. This server exposes the same JSON-RPC
 * entry point over plain HTTP on the LAN:
 *
 *   http://<device-ip>:81/              generated controls for every tool
 *   POST http://<device-ip>:81/mcp      JSON-RPC request -> JSON-RPC response
 *   GET  http://<device-ip>:81/mcp/result?id=N
 *                                       result of a request that is still running
 *   POST http://<device-ip>:81/snapshot receiver for self.screen.snapshot
 *   GET  http://<device-ip>:81/snapshot.jpg
 *                                       last uploaded screen snapshot
 *   GET  http://<device-ip>:81/address  numeric device address for URLs
 *
 * The page talks to the same McpServer instance the XiaoZhi protocol uses, so
 * tool names, schemas, validation, and callbacks are exactly the ones the
 * model sees. Long-running tools return "pending" and the page polls for the
 * result, which keeps the HTTP task free (self.screen.snapshot posts back to
 * this same server while the tool runs on the main task).
 *
 * Start() is idempotent and must run after the network stack is initialized.
 */
class McpHttpConsole {
public:
    static bool Start(int port = 81);
};

#endif  // MCP_HTTP_CONSOLE_H
