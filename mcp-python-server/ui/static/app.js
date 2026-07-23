const defaultPayload = {
  jsonrpc: "2.0",
  id: 1,
  method: "tools/list",
  params: {}
};

const clientConfigs = {
  claude: {
    mcpServers: {
      "cmdsdk-python": {
        transport: "streamable_http",
        url: "http://localhost:5432/mcp"
      }
    }
  },
  chatgpt: {
    mcp_servers: {
      cmdsdk_python: {
        type: "http",
        url: "http://localhost:5432/mcp"
      }
    }
  },
  vscode: {
    servers: {
      "cmdsdk-python": {
        type: "http",
        endpoint: "http://localhost:5432/mcp"
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

function renderClientConfig() {
  const select = document.getElementById("clientType");
  const code = document.getElementById("clientConfig");
  code.textContent = pretty(clientConfigs[select.value]);
}

function renderRegisteredClients() {
  const code = document.getElementById("registeredClients");
  code.textContent = pretty(loadClients());
}

function registerClient(event) {
  event.preventDefault();
  const name = document.getElementById("clientName").value.trim();
  const token = document.getElementById("sessionToken").value.trim();
  const type = document.getElementById("clientType").value;

  if (!name || !token) {
    return;
  }

  const clients = loadClients();
  clients.push({
    name,
    token,
    type,
    endpoint: "http://localhost:5432/mcp",
    registeredAt: new Date().toISOString()
  });

  saveClients(clients);
  document.getElementById("registerMessage").textContent = `Registered ${name} (${type})`;
  renderRegisteredClients();
}

async function sendMcpRequest(event) {
  event.preventDefault();
  const responseCode = document.getElementById("mcpResponse");
  const sessionId = document.getElementById("testSessionId").value || "default";
  const payloadRaw = document.getElementById("mcpPayload").value;

  let payload;
  try {
    payload = JSON.parse(payloadRaw);
  } catch (error) {
    responseCode.textContent = `Invalid JSON: ${error}`;
    return;
  }

  try {
    const response = await fetch("/mcp", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "MCP-Session-Id": sessionId
      },
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
