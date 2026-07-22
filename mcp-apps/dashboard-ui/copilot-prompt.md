Create a new MCP app UI resource project from scratch.

Important constraints:
- Do not assume this repository or any existing UI examples are available.
- Do not rely on files such as ui/, src/, math-form, geo-form, or any current project structure.
- Create the project structure yourself and make it self-contained.
- Treat this as a fresh starter project for building MCP app tool resource UIs.

Goal:
Build a minimal example MCP app UI resource that shows a greeting experience:
- The page contains an input for a name.
- If the user enters a name, display: Hello <name>!!!
- If the input is empty or only whitespace, display: Hello World!!!
- Keep the implementation simple, readable, and suitable as a starter example.

Project requirements:
1. Use React, TypeScript, and Vite.
2. Create the project structure from scratch, including:
   - package.json
   - tsconfig.json
   - tsconfig.node.json
   - vite.config.ts
   - a build script for compiling one or more HTML entry points
   - a simple server file that serves the built UI and exposes a resource manifest
3. Create a UI resource entry in a new ui/ folder.
4. Create a matching React entry in a new src/ folder.
5. Use a shared stylesheet in src/index.css or equivalent.
6. Make the resource discoverable by an MCP host through a manifest endpoint such as /resource-manifest.json.
7. Create a skill document at .github/skills/mcp-app-ui/SKILL.md that explains how to add a new MCP app tool resource UI.

Implementation details:
- Create a small standalone component with local state.
- Use a single HTML page per resource entry.
- Mount the React app into a container defined in the HTML file.
- Use a simple, polished layout with a text input, a submit or update behavior, and a visible greeting result.
- The greeting logic should be implemented in the React component, not in plain HTML.
- The app should work even if no MCP host is connected; it should still render locally.
- The implementation should not depend on math-form, geo-form, or any other sample UI.

Suggested project structure:
- package.json
- tsconfig.json
- tsconfig.node.json
- vite.config.ts
- build-ui.mjs
- server.ts
- index.html
- src/
  - index.css
  - greet/greet.tsx
- ui/
  - greet.html
- .github/skills/mcp-app-ui/SKILL.md

Behavior requirements:
- The initial view should show Hello World!!!.
- As the user types a name, the displayed greeting should update in real time or on submit.
- If the name is missing, the fallback text remains Hello World!!!.
- The greeting should be displayed clearly and readably.

Manifest requirements:
- Add a resource entry for the new UI resource.
- The manifest should include:
  - a resource URI such as ui://ui/greet.html
  - a name such as Hello World UI
  - a description
  - a mime type of text/html
  - a UI resource URI pointing to the built HTML asset

Build and run expectations:
- Provide scripts so the project can be installed and run locally.
- The build should generate a distributable HTML UI bundle.
- The server should serve the built output and expose the manifest endpoint.

Quality bar:
- Keep the code minimal and easy to understand.
- Prefer clarity over abstraction.
- Use idiomatic React and TypeScript.
- Ensure the app can be used as a clean starting point for future MCP app UI resources.

When you finish, provide:
- the created project files
- a short explanation of the architecture
- the commands to install, build, and run the project
