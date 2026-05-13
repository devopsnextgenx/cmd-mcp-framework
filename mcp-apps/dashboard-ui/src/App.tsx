import type { CSSProperties } from 'react';
import { useEffect, useMemo, useState } from 'react';
import { z } from 'zod';
import Add from './math/Add.tsx';
import { useApp } from '@modelcontextprotocol/ext-apps/react';
import { Client } from '@modelcontextprotocol/sdk/client';
import { StreamableHTTPClientTransport } from '@modelcontextprotocol/sdk/client/streamableHttp';

const C_PLUS_PLUS_SERVER_URL = 'http://localhost:5432/mcp';
const RESOURCE_MANIFEST_URL = 'http://localhost:6543/resource-manifest.json';
const RESOURCE_STATE_URL = '/dashboard-state';

const updateStateSchema = z.object({
  update: z.record(z.any()),
});

type DashboardState = {
  lastUpdated: string;
  serverConnected: boolean;
  hostConnected: boolean;
  resourceUri: string;
  message: string;
};

const initialState: DashboardState = {
  lastUpdated: new Date().toISOString(),
  serverConnected: false,
  hostConnected: false,
  resourceUri: 'http://localhost:6543/dashboard-state',
  message: 'Dashboard is initializing...',
};

function App() {
  const [dashboardState, setDashboardState] = useState<DashboardState>(initialState);
  const [serverStatus, setServerStatus] = useState('Connecting to local C++ server...');
  const [hostStatus, setHostStatus] = useState('Waiting for MCP host connection...');
  const [resourceStatus] = useState(`Published resource manifest at ${RESOURCE_MANIFEST_URL}`);

  const { isConnected, error } = useApp({
    appInfo: { name: 'dashboard-ui', version: '0.1.0' },
    capabilities: { tools: { listChanged: true } },
    onAppCreated: (app) => {
      app.registerTool(
        'dashboard.updateState',
        {
          title: 'Update Dashboard State',
          description: 'Update the dashboard UI state from the host',
          inputSchema: updateStateSchema,
        },
        async (args) => {
          const update = args.update;
          if (!update || typeof update !== 'object') {
            return {
              content: [
                {
                  type: 'text',
                  text: 'Invalid update payload',
                },
              ],
            };
          }

          setDashboardState((prev) => {
            const nextState = {
              ...prev,
              ...update,
              hostConnected: true,
              lastUpdated: new Date().toISOString(),
              message: 'Received state update from host',
            };
            pushDashboardState(nextState).catch(() => undefined);
            return nextState;
          });

          return {
            content: [
              {
                type: 'text',
                text: 'State update applied',
              },
            ],
          };
        },
      );
    },
  });

  useEffect(() => {
    const nextState = {
      ...dashboardState,
      hostConnected: isConnected,
      lastUpdated: new Date().toISOString(),
      message: isConnected
        ? 'Connected to MCP host'
        : error
        ? `MCP host error: ${error.message}`
        : 'Waiting for MCP host connection...',
    };

    setHostStatus(nextState.message);
    setDashboardState(nextState);
    pushDashboardState(nextState).catch(() => undefined);
  }, [isConnected, error]);

  const client = useMemo(() => new Client({ name: 'dashboard-ui', version: '0.1.0' }), []);

  useEffect(() => {
    const transport = new StreamableHTTPClientTransport(new URL(C_PLUS_PLUS_SERVER_URL), {
      requestInit: { mode: 'cors' },
      reconnectionOptions: {
        initialReconnectionDelay: 1000,
        maxReconnectionDelay: 30000,
        reconnectionDelayGrowFactor: 1.5,
        maxRetries: 5,
      },
    });

    client
      .connect(transport)
      .then(() => {
        setServerStatus('Connected to C++ MCP server at localhost:5432');
        setDashboardState((prev) => {
          const next = {
            ...prev,
            serverConnected: true,
            lastUpdated: new Date().toISOString(),
            message: 'Connected to local C++ server',
          };
          pushDashboardState(next).catch(() => undefined);
          return next;
        });
      })
      .catch((err) => {
        setServerStatus(`Failed to connect to C++ server: ${String(err)}`);
        setDashboardState((prev) => {
          const next = {
            ...prev,
            serverConnected: false,
            lastUpdated: new Date().toISOString(),
            message: 'Unable to reach local C++ MCP server',
          };
          pushDashboardState(next).catch(() => undefined);
          return next;
        });
      });

    return () => {
      transport.close().catch(() => undefined);
    };
  }, [client]);
  const invokeServerMath = async (a: number, b: number): Promise<number> => {
    try {
      const result = await client.callTool({
        name: 'math.calculate',
        arguments: { subType: 'MATH.ADD', left: a, right: b },
      });

      if ('isError' in result && result.isError) {
        throw new Error(JSON.stringify(result.content));
      }

      // Handle MCP-style structured output or direct result
      const output = (result as any).structuredContent || (result as any).content;
      
      if (output && typeof output === 'object') {
        if (typeof output.value === 'number') return output.value;
        if (typeof output.result === 'number') return output.result;
      }
    } catch {
      // Fall back to direct HTTP JSON-RPC if the managed transport fails.
    }

    const response = await fetch(C_PLUS_PLUS_SERVER_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      mode: 'cors',
      body: JSON.stringify({
        jsonrpc: '2.0',
        method: 'tools/call',
        params: { name: 'math.calculate', arguments: { subType: 'MATH.ADD', left: a, right: b } },
        id: 'dashboard-add',
      }),
    });

    const body = await response.json();
    if (!response.ok || body.error) {
      throw new Error(JSON.stringify(body.error ?? body));
    }

    const resultData = body.result;

    // 1. Primary: Check structuredContent (matches your provided log)
    if (resultData?.structuredContent && typeof resultData.structuredContent.value === 'number') {
      return resultData.structuredContent.value;
    }

    // 2. Secondary: Parse the stringified JSON inside the content array
    const contentArray = resultData?.content;
    if (Array.isArray(contentArray) && contentArray.length > 0) {
      const firstItem = contentArray[0];
      if (firstItem && typeof firstItem.text === 'string') {
        try {
          const parsed = JSON.parse(firstItem.text);
          if (typeof parsed.value === 'number') return parsed.value;
        } catch (e) {
          // Parsing failed, move to final error
        }
      }
    }

    throw new Error('Unexpected server response format: Missing numeric value');
  };

  const pushDashboardState = async (nextState: DashboardState) => {
    await fetch(RESOURCE_STATE_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(nextState),
    });
  };

  const containerStyle: CSSProperties = {
    padding: '2rem',
    borderRadius: '12px',
    backgroundColor: 'var(--card-bg)',
    boxShadow: '0 10px 25px -5px rgba(0, 0, 0, 0.3), 0 0 15px var(--accent-glow)',
    border: '1px solid rgba(255, 255, 255, 0.1)',
    textAlign: 'center',
    maxWidth: '500px',
    width: '90%',
  };

  const titleStyle: CSSProperties = {
    margin: '0 0 1rem 0',
    fontSize: '2rem',
    color: 'var(--accent-blue)',
    letterSpacing: '-0.025em',
  };

  const statusStyle: CSSProperties = {
    color: 'var(--text-secondary)',
    fontSize: '0.95rem',
    marginTop: '1rem',
    lineHeight: 1.5,
  };

  return (
    <div style={containerStyle}>
      <h1 style={titleStyle}>MCP Dashboard UI</h1>
      <div style={statusStyle}>
        <div>{serverStatus}</div>
        <div>{hostStatus}</div>
        <div>{resourceStatus}</div>
        <div>Resource URI: {dashboardState.resourceUri}</div>
      </div>
      <Add invokeMath={invokeServerMath} />
      <div
        style={{
          marginTop: '1.5rem',
          height: '4px',
          background: 'var(--accent-blue)',
          borderRadius: '2px',
          opacity: 0.6,
        }}
      />
    </div>
  );
}

export default App;
