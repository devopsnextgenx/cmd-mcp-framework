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
    const response = await fetch("/api/mcp-proxy", {
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
  document.getElementById("mcpPayload").value = pretty(defaultPayload);
  document.getElementById("clientType").addEventListener("change", renderClientConfig);
  document.getElementById("registerForm").addEventListener("submit", registerClient);
  document.getElementById("mcpForm").addEventListener("submit", sendMcpRequest);

  renderClientConfig();
  renderRegisteredClients();
}

document.addEventListener("DOMContentLoaded", setup);
