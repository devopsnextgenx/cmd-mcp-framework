# MCP App UI Resource Skill

Use this skill when you need to create a new UI resource for an MCP app tool from scratch.

## Goal
Create a small, self-contained web UI that can be served as an MCP app resource. The UI should be simple, readable, and easy to extend.

## Assumption
Do not assume that an existing repository, UI folder, or sample resource is available. Create the structure yourself if it does not already exist.

## Recommended project structure
Create a fresh project with this shape:

- package.json
- tsconfig.json
- tsconfig.node.json
- vite.config.ts
- build-ui.mjs
- server.ts
- ui/<resource-name>.html
- src/index.css
- src/<resource-name>/<resource-name>.tsx
- .github/skills/mcp-app-ui/SKILL.md

## Core workflow
1. Create an HTML entry file in ui/.
2. Create a React component in src/<resource-name>/.
3. Mount the React component into the HTML page.
4. Add a resource manifest entry so an MCP host can discover the UI.
5. Build and serve the UI from a simple server.

## Minimal UI pattern
A starter UI resource should:
- have one focused purpose
- use local state for simple interactions
- display a clear result to the user
- degrade gracefully when no MCP host is present

Example behavior for a Hello World resource:
- input field for a name
- if a name is entered, display Hello <name>!!!
- if the input is empty, display Hello World!!!

## Example HTML entry
Create a file such as ui/hello-world.html:

```html
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>Hello World UI</title>
  </head>
  <body>
    <div id="root"></div>
    <script type="module" src="/src/hello-world/hello-world.tsx"></script>
  </body>
</html>
```

## Example React entry
Create a file such as src/hello-world/hello-world.tsx:

```tsx
import { createRoot } from 'react-dom/client';
import { useMemo, useState } from 'react';
import '../index.css';

function HelloWorldWidget() {
  const [name, setName] = useState('');
  const greeting = useMemo(() => {
    const trimmed = name.trim();
    return trimmed ? `Hello ${trimmed}!!!` : 'Hello World!!!';
  }, [name]);

  return (
    <div>
      <h1>Hello World MCP App UI</h1>
      <input
        value={name}
        onChange={(event) => setName(event.target.value)}
        placeholder="Type a name"
      />
      <p>{greeting}</p>
    </div>
  );
}

createRoot(document.getElementById('root')!).render(<HelloWorldWidget />);
```

## Manifest and server guidance
Expose the resource through a manifest endpoint such as /resource-manifest.json.

Include fields such as:
- uri
- name
- description
- mimeType
- _meta.ui.resourceUri

Example:

```ts
{
  uri: 'ui://ui/hello-world',
  name: 'Hello World UI',
  description: 'Simple greeting UI for an MCP app tool',
  mimeType: 'text/html',
  _meta: {
    ui: {
      resourceUri: 'http://localhost:6543/ui/hello-world.html',
    },
  },
}
```

## Design rules
- Keep the UI small and focused on one workflow.
- Prefer clear local state over unnecessary abstraction.
- Handle empty input gracefully.
- Make the page work both standalone and when opened by an MCP host.
- Use a shared stylesheet for consistent styling.

## Build and verify
- Install dependencies.
- Build the project.
- Confirm the HTML entry is emitted under a build output such as dist/ui/.
- Confirm the server exposes the manifest and serves the built UI.
