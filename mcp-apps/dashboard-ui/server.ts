import express from 'express';
import path from 'path';
import { fileURLToPath } from 'url';
import resourceManifest from './resource-manifest.json' assert { type: 'json' };

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const app = express();
const PORT = process.env.PORT || 6543;

// Serve static files from dist directory
app.use(express.static(path.join(__dirname, 'dist')));

// Resource manifest endpoint for MCP apps discovery
app.get('/resource-manifest.json', (req, res) => {
  res.json(resourceManifest);
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
  console.log(`✓ Greetings UI available at: http://localhost:${PORT}/ui/greet.html`);
  console.log(`✓ Resource manifest available at: http://localhost:${PORT}/resource-manifest.json\n`);
});

process.on('SIGINT', () => {
  console.log('\nShutting down gracefully...');
  process.exit(0);
});
