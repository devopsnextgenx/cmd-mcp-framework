import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';

// When INPUT is set, build-ui.mjs drives a single-file bundle for that HTML entry.
const INPUT = process.env.INPUT;

const resourceManifest = {
  resources: [
    {
      uri: 'ui://ui/math-form',
      name: 'Math Form UI',
      description: 'Math operation form with subtype enum and two numeric inputs',
      mimeType: 'text/html',
      _meta: {
        ui: {
          resourceUri: 'http://localhost:6543/ui/math-form.html',
        },
      },
    },
    {
      uri: 'ui://ui/geo-form',
      name: 'Geometry Form UI',
      description: 'Geometry operation form with perimeter, area, and volume calculations',
      mimeType: 'text/html',
      _meta: {
        ui: {
          resourceUri: 'http://localhost:6543/ui/geo-form.html',
        },
      },
    },
    {
      uri: 'ui://ui/greet',
      name: 'Greetings UI',
      description: 'Simple greeting UI that displays Hello <name>!!! or Hello World!!!',
      mimeType: 'text/html',
      _meta: {
        ui: {
          resourceUri: 'http://localhost:6543/ui/greet.html',
        },
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

      next();
    });
  },
};

export default defineConfig(
  INPUT
    ? // ── Single-file build mode (invoked by build-ui.mjs with INPUT env var) ──
      {
        plugins: [react(), viteSingleFile()],
        build: {
          rollupOptions: { input: INPUT },
          outDir: 'dist',
          emptyOutDir: false,
          minify: true,
          sourcemap: false,
        },
      }
    : // ── Normal dashboard dev/build mode ───────────────────────────────────
      {
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
      },
);
