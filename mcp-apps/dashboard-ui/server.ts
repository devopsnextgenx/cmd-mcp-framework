import express from 'express';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const app = express();
const PORT = process.env.PORT || 6543;

// Serve static files from dist directory
app.use(express.static(path.join(__dirname, 'dist')));

// Resource manifest endpoint for MCP apps discovery
app.get('/resource-manifest.json', (req, res) => {
  const manifest = {
    resources: [
      {
        uri: 'app://math-form',
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
        uri: 'http://localhost:6543/ui/math-form.html',
        name: 'Math Calculator Form',
        description: 'Math calculator form UI for arithmetic operations via MCP tool',
        mimeType: 'text/html',
        _meta: {
          ui: {
            resourceUri: 'http://localhost:6543/ui/math-form.html',
          },
        },
      },
      {
        uri: 'app://geo-form',
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
        uri: 'http://localhost:6543/ui/geo-form.html',
        name: 'Geometry Calculator Form',
        description: 'Geometry calculator form UI for perimeter, area, and volume operations via MCP tool',
        mimeType: 'text/html',
        _meta: {
          ui: {
            resourceUri: 'http://localhost:6543/ui/geo-form.html',
          },
        },
      },
    ],
  };

  res.json(manifest);
});

// Fallback: serve index.html for root and dashboard routes
app.get(['/', '/dashboard', '/dashboard/*'], (req, res) => {
  res.sendFile(path.join(__dirname, 'dist', 'index.html'));
});

// CORS headers for all responses
app.use((req, res, next) => {
  res.header('Access-Control-Allow-Origin', '*');
  res.header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.header('Access-Control-Allow-Headers', 'Content-Type');
  next();
});

app.listen(PORT, '0.0.0.0', () => {
  console.log(`\n✓ Static file server running on http://0.0.0.0:${PORT}`);
  console.log(`✓ Serving static content from: ${path.join(__dirname, 'dist')}`);
  console.log(`✓ Math form available at: http://localhost:${PORT}/ui/math-form.html`);
  console.log(`✓ Geometry form available at: http://localhost:${PORT}/ui/geo-form.html`);
  console.log(`✓ Resource manifest available at: http://localhost:${PORT}/resource-manifest.json\n`);
});

process.on('SIGINT', () => {
  console.log('\nShutting down gracefully...');
  process.exit(0);
});
