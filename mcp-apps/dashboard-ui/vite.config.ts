import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react()],
  
  // 1. Set base to empty or relative so it works on any sub-path 
  // (e.g., http://localhost:5432/apps/dashboard-ui/)
  base: './',

  build: {
    // 2. Ensure output goes to the 'dist' directory for the C++ server to find
    outDir: 'dist',
    
    // 3. Generate a single bundle or ensure assets are handled cleanly
    assetsDir: 'assets',
    
    // Optional: Minification and sourcemaps
    minify: 'esbuild',
    sourcemap: false,
  },

  server: {
    // 4. Useful for local development: allows the MCP Host (like Claude) 
    // to access the dev server via localhost
    cors: true,
    port: 6543,
    strictPort: true,
  }
});