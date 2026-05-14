import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

const dashboardResource = {
  timestamp: new Date().toISOString(),
  status: 'idle',
  metrics: {
    requestCount: 0,
    errorCount: 0,
  },
  ui: {
    activePanel: 'home',
  },
};

const resourceManifest = {
  resources: [
    {
      uri: 'app://dashboard-ui',
      name: 'Dashboard UI',
      description: 'Dashboard UI shell with command and resource inspection',
      mimeType: 'text/html',
      _meta: {
        ui: {
          resourceUri: 'http://localhost:6543/',
        },
      },
    },
    {
      uri: 'http://localhost:6543/dashboard-state',
      name: 'Dashboard UI State',
      description: 'Live dashboard UI state exposed as an MCP resource',
      mimeType: 'application/json',
      _meta: {
        source: 'dashboard-ui',
      },
    },
  ],
};

const mcpMiddlewarePlugin = {
  name: 'mcp-middleware',
  configureServer(server: import('vite').ViteDevServer) {
    server.middlewares.use((req: import('http').IncomingMessage, res: import('http').ServerResponse, next: () => void) => {
      if (!req.url) {
        next();
        return;
      }

      if (req.method === 'GET' && req.url === '/resource-manifest.json') {
        res.setHeader('Content-Type', 'application/json');
        res.end(JSON.stringify(resourceManifest, null, 2));
        return;
      }

      if (req.url === '/dashboard-state') {
        if (req.method === 'GET') {
          dashboardResource.metrics.requestCount += 1;
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify(dashboardResource, null, 2));
          return;
        }

        if (req.method === 'POST') {
          let body = '';
          req.on('data', (chunk) => {
            body += chunk;
          });
          req.on('end', () => {
            try {
              const payload = JSON.parse(body || '{}');
              Object.assign(dashboardResource, payload, {
                timestamp: new Date().toISOString(),
              });
              res.setHeader('Content-Type', 'application/json');
              res.end(JSON.stringify({ ok: true, state: dashboardResource }, null, 2));
            } catch (error) {
              dashboardResource.metrics.errorCount += 1;
              res.statusCode = 400;
              res.end(JSON.stringify({ error: 'Invalid JSON body' }, null, 2));
            }
          });
          return;
        }
      }

      next();
    });
  },
};

export default defineConfig({
  plugins: [react(), mcpMiddlewarePlugin],
  base: './',
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
    minify: 'esbuild',
    sourcemap: false,
  },
  server: {
    cors: true,
    port: 6543,
    strictPort: true,
  },
});
