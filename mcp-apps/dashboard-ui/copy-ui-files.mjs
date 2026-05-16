#!/usr/bin/env node
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Create dist directories if they don't exist
const distDir = path.join(__dirname, 'dist');
const distUiDir = path.join(distDir, 'ui');

if (!fs.existsSync(distDir)) {
  fs.mkdirSync(distDir, { recursive: true });
  console.log(`✓ Created dist/ directory`);
}

if (!fs.existsSync(distUiDir)) {
  fs.mkdirSync(distUiDir, { recursive: true });
  console.log(`✓ Created dist/ui/ directory`);
}

// Copy HTML files from ui/ to dist/ui/
const uiDir = path.join(__dirname, 'ui');
if (fs.existsSync(uiDir)) {
  const files = fs.readdirSync(uiDir).filter(f => f.endsWith('.html'));
  for (const file of files) {
    const src = path.join(uiDir, file);
    const dst = path.join(distUiDir, file);
    fs.copyFileSync(src, dst);
    console.log(`✓ Copied ${file} to dist/ui/`);
  }
} else {
  console.warn(`⚠ Warning: ui/ directory not found`);
}

// Create a simple index.html if it doesn't exist
const indexPath = path.join(distDir, 'index.html');
if (!fs.existsSync(indexPath)) {
  const indexHtml = `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>MCP Apps - Dashboard</title>
  <style>
    body { font-family: system-ui, -apple-system, sans-serif; margin: 0; padding: 2rem; background: #f5f5f5; }
    .container { max-width: 800px; margin: 0 auto; }
    h1 { color: #333; }
    .apps { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 1.5rem; margin-top: 2rem; }
    .app-card { background: white; padding: 1.5rem; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
    .app-card h3 { margin-top: 0; color: #0066cc; }
    .app-card p { color: #666; line-height: 1.6; }
    .app-card a { display: inline-block; margin-top: 1rem; padding: 0.5rem 1rem; background: #0066cc; color: white; text-decoration: none; border-radius: 4px; }
    .app-card a:hover { background: #0052a3; }
  </style>
</head>
<body>
  <div class="container">
    <h1>🔧 MCP Apps - Dashboard</h1>
    <p>Available applications and utilities for the MCP Framework</p>
    
    <div class="apps">
      <div class="app-card">
        <h3>Math Calculator</h3>
        <p>Perform arithmetic operations with a visual form interface.</p>
        <a href="/ui/math-form.html" target="_blank">Open Math Form</a>
      </div>
      
      <div class="app-card">
        <h3>Geometry Calculator</h3>
        <p>Calculate perimeter, area, and volume for geometric shapes.</p>
        <a href="/ui/geo-form.html" target="_blank">Open Geometry Form</a>
      </div>
    </div>
  </div>
</body>
</html>`;
  
  fs.writeFileSync(indexPath, indexHtml);
  console.log(`✓ Created index.html`);
}

console.log(`\n✓ Build complete! dist/ directory is ready to serve.`);

