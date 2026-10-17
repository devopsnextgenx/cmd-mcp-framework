const defaultPayload = {
  jsonrpc: "2.0",
  id: 1,
  method: "tools/list",
  params: {}
};

const initializeProtocolVersion = "2025-03-26";

const mcpRequestHeaders = {
  "Content-Type": "application/json",
  Accept: "application/json, text/event-stream",
  "MCP-Protocol-Version": initializeProtocolVersion
};

function getMcpEndpoint() {
  const url = new URL(window.location.href);
  url.port = "5432";
  url.pathname = "/mcp";
  url.search = "";
  url.hash = "";
  return url.toString();
}

const clientConfigs = {
  claude: {
    mcpServers: {
      "cmdsdk-python": {
        transport: "streamable_http",
        url: "__MCP_ENDPOINT__"
      }
    }
  },
  chatgpt: {
    mcp_servers: {
      cmdsdk_python: {
        type: "http",
        url: "__MCP_ENDPOINT__"
      }
    }
  },
  vscode: {
    servers: {
      "cmdsdk-python": {
        type: "http",
        endpoint: "__MCP_ENDPOINT__"
      }
    }
  }
};

function pretty(value) {
  return JSON.stringify(value, null, 2);
}

function detectProtocolLabel(url) {
  if (url.includes("/mcp")) {
    return "MCP";
  }
  if (url.includes("/api/")) {
    return "REST";
  }
  return null;
}

function installFetchLoggingInterceptor() {
  const originalFetch = window.fetch.bind(window);

  window.fetch = async (input, init = {}) => {
    const url = typeof input === "string" ? input : input instanceof URL ? input.toString() : input.url;
    const protocol = detectProtocolLabel(url);

    if (!protocol) {
      return originalFetch(input, init);
    }

    const method = (init.method || "GET").toUpperCase();
    const requestHeaders = init.headers || {};
    const requestBody = init.body ?? null;

    console.log(`[${protocol}] Request`, {
      method,
      url,
      headers: requestHeaders,
      body: requestBody
    });

    try {
      const response = await originalFetch(input, init);
      const responseClone = response.clone();
      const contentType = responseClone.headers.get("content-type") || "";
      let responseBody;

      if (contentType.includes("application/json")) {
        responseBody = await responseClone.json();
      } else {
        responseBody = await responseClone.text();
      }

      console.log(`[${protocol}] Response`, {
        method,
        url,
        status: response.status,
        headers: Object.fromEntries(response.headers.entries()),
        body: responseBody
      });

      return response;
    } catch (error) {
      console.error(`[${protocol}] Request failed`, {
        method,
        url,
        error
      });
      throw error;
    }
  };
}

function loadClients() {
  const raw = localStorage.getItem("mcpPythonClients");
  if (!raw) {
    return [];
  }

  try {
    return JSON.parse(raw);
  } catch {
    return [];
  }
}

function saveClients(items) {
  localStorage.setItem("mcpPythonClients", JSON.stringify(items));
}

function buildInitializePayload(name) {
  return {
    jsonrpc: "2.0",
    id: `initialize-${Date.now()}`,
    method: "initialize",
    params: {
      protocolVersion: initializeProtocolVersion,
      capabilities: {},
      clientInfo: {
        name,
        version: "0.1.0"
      }
    }
  };
}

async function createMcpSession(name) {
  const response = await fetch(getMcpEndpoint(), {
    method: "POST",
    headers: mcpRequestHeaders,
    body: JSON.stringify(buildInitializePayload(name))
  });

  const contentType = response.headers.get("content-type") || "";
  const body = contentType.includes("application/json") ? await response.json() : await response.text();

  if (!response.ok) {
    throw new Error(pretty({ status: response.status, body }));
  }

  const sessionId =
    response.headers.get("mcp-session-id") ||
    body?.sessionId ||
    body?.result?.sessionId ||
    null;

  if (!sessionId) {
    throw new Error("MCP initialize succeeded but did not return a session ID");
  }

  await fetch(getMcpEndpoint(), {
    method: "POST",
    headers: {
      ...mcpRequestHeaders,
      "MCP-Session-Id": sessionId
    },
    body: JSON.stringify({
      jsonrpc: "2.0",
      method: "notifications/initialized"
    })
  });

  return {
    sessionId,
    initializeResponse: body
  };
}

function renderClientConfig() {
  const select = document.getElementById("clientType");
  const code = document.getElementById("clientConfig");
  const endpoint = getMcpEndpoint();
  const config = JSON.parse(JSON.stringify(clientConfigs[select.value]).replaceAll("__MCP_ENDPOINT__", endpoint));
  code.textContent = pretty(config);
}

function renderRegisteredClients() {
  const code = document.getElementById("registeredClients");
  code.textContent = pretty(loadClients());
}

async function registerClient(event) {
  event.preventDefault();
  const registerMessage = document.getElementById("registerMessage");
  const name = document.getElementById("clientName").value.trim();
  const token = document.getElementById("sessionToken").value.trim();
  const type = document.getElementById("clientType").value;

  if (!name || !token) {
    return;
  }

  registerMessage.textContent = "Creating MCP session...";

  try {
    const { sessionId, initializeResponse } = await createMcpSession(name);
    const clients = loadClients();
    clients.push({
      name,
      token,
      type,
      endpoint: getMcpEndpoint(),
      sessionId,
      initializeResponse,
      registeredAt: new Date().toISOString()
    });

    saveClients(clients);
    document.getElementById("testSessionId").value = sessionId;
    registerMessage.textContent = `Registered ${name} (${type}) with sessionId ${sessionId}`;
    renderRegisteredClients();
  } catch (error) {
    registerMessage.textContent = `Registration failed: ${error}`;
  }
}

async function sendMcpRequest(event) {
  event.preventDefault();
  const responseCode = document.getElementById("mcpResponse");
  const sessionId = document.getElementById("testSessionId").value.trim();
  const payloadRaw = document.getElementById("mcpPayload").value;

  let payload;
  try {
    payload = JSON.parse(payloadRaw);
  } catch (error) {
    responseCode.textContent = `Invalid JSON: ${error}`;
    return;
  }

  try {
    const headers = { ...mcpRequestHeaders };
    if (sessionId) {
      headers["MCP-Session-Id"] = sessionId;
    }

    const response = await fetch(getMcpEndpoint(), {
      method: "POST",
      headers,
      body: JSON.stringify(payload)
    });

    const contentType = response.headers.get("content-type") || "";
    const body = contentType.includes("application/json") ? await response.json() : await response.text();
    responseCode.textContent = pretty({ status: response.status, body });
  } catch (error) {
    responseCode.textContent = `Request failed: ${error}`;
  }
}

function setup() {
  installFetchLoggingInterceptor();
  document.getElementById("mcpPayload").value = pretty(defaultPayload);
  document.getElementById("clientType").addEventListener("change", renderClientConfig);
  document.getElementById("registerForm").addEventListener("submit", registerClient);
  document.getElementById("mcpForm").addEventListener("submit", sendMcpRequest);

  renderClientConfig();
  renderRegisteredClients();
}

document.addEventListener("DOMContentLoaded", setup);
