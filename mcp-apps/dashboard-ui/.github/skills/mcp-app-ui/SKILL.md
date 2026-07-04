# MCP App UI Resource Skill

Use this skill when you need to create a new UI resource for an MCP app tool from scratch.

## Recommended project structure
- vite.config.ts
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
- **always connect to the MCP App host to receive initial parameters**
- degrade gracefully when no MCP host is present

Example behavior for a Hello World resource:
- input field for a name
- if a name is entered, display Hello <name>!!!
- if the input is empty, display Hello World!!!
- **if the form is opened from an MCP host with a name parameter, prepopulate the input field**

## Example HTML entry
Create a file such as ui/greet.html:

```html
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>Greetings UI</title>
  </head>
  <body>
    <div id="root"></div>
    <script type="module" src="/src/greet/greet.tsx"></script>
  </body>
</html>
```

## Example React entry
Create a file such as src/greet/greet.tsx:

```tsx
import { createRoot } from 'react-dom/client';
import { useEffect, useMemo, useState } from 'react';
import { App } from '@modelcontextprotocol/ext-apps';
import '../index.css';

interface GreetFormConfig {
  name?: string;
}

function GreetWidget() {
  const [appInstance] = useState(
    () => new App({ name: 'Greetings', version: '1.0.0' }),
  );
  const [name, setName] = useState('');

  // Connect to MCP host and receive initial parameters
  useEffect(() => {
    appInstance.ontoolresult = (toolResult) => {
      // Extract name from response (check both top-level and args field)
      const data = toolResult.structuredContent as GreetFormConfig;
      if (typeof data?.name === 'string') {
        setName(data.name);
      } else if (data?.args && typeof (data.args as GreetFormConfig).name === 'string') {
        setName((data.args as GreetFormConfig).name);
      }
    };

    appInstance
      .connect()
      .catch(() => {
        // Gracefully degrade — form still works standalone
      });
  }, [appInstance]);

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

createRoot(document.getElementById('root')!).render(<GreetWidget />);
```

## MCP App Integration and Parameter Handling

When your UI resource is opened by an MCP host, the host may pass initial parameters (form inputs, options, etc.). Follow these best practices:

### 1. Always Create and Connect to the App Instance
```tsx
const [appInstance] = useState(
  () => new App({ name: 'Your App Name', version: '1.0.0' }),
);

useEffect(() => {
  appInstance.ontoolresult = (toolResult) => {
    // Handle parameters from the host
  };

  appInstance
    .connect()
    .catch(() => {
      // Gracefully degrade if no host present
    });
}, [appInstance]);
```

### 2. Extract Parameters from Response
The MCP handler passes input parameters in the response. **Always check both locations:**
- Top-level fields: `toolResult.structuredContent.fieldName`
- Nested in `args`: `toolResult.structuredContent.args.fieldName`

This provides forward compatibility with different response formats:

```tsx
const data = toolResult.structuredContent;
let paramValue = data?.paramName;

// Check args field if top-level value not found
if (typeof paramValue === 'undefined' && data?.args?.paramName !== undefined) {
  paramValue = data.args.paramName;
}
```

### 3. Prepopulate Form Fields on Mount
Use the `ontoolresult` callback to populate form fields when the UI opens:

```tsx
const [leftValue, setLeftValue] = useState('');
const [rightValue, setRightValue] = useState('');

appInstance.ontoolresult = (toolResult) => {
  const data = toolResult.structuredContent;
  
  // Try to get from args field first (contains original input)
  const args = data?.args || data;
  
  if (typeof args.left === 'number') {
    setLeftValue(args.left.toString());
  }
  if (typeof args.right === 'number') {
    setRightValue(args.right.toString());
  }
};
```

### 4. Handle Forms with Multiple Parameter Types
For complex forms (like geometry calculators with multiple fields), create a mapping function:

```tsx
const [paramValues, setParamValues] = useState({
  a: '', b: '', c: '', radius: '', height: '',
});

appInstance.ontoolresult = (toolResult) => {
  const data = toolResult.structuredContent;
  const args = data?.args || data;
  
  const newParamValues = { ...paramValues };
  
  // Map all possible parameters
  if (typeof args.a === 'number') newParamValues.a = args.a.toString();
  if (typeof args.b === 'number') newParamValues.b = args.b.toString();
  if (typeof args.c === 'number') newParamValues.c = args.c.toString();
  if (typeof args.radius === 'number') newParamValues.radius = args.radius.toString();
  if (typeof args.height === 'number') newParamValues.height = args.height.toString();
  
  setParamValues(newParamValues);
};
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
  uri: 'ui://ui/greet',
  name: 'Greetings UI',
  description: 'Simple greeting UI for an MCP app tool',
  mimeType: 'text/html',
  _meta: {
    ui: {
      resourceUri: 'http://localhost:6543/ui/greet.html',
    },
  },
}
```

## Design rules
- Keep the UI small and focused on one workflow.
- Prefer clear local state over unnecessary abstraction.
- Handle empty input gracefully.
- **Always connect to the MCP App host to receive and prepopulate initial parameters.**
- **Check both top-level fields and the `args` field when extracting parameters from the response.**
- **Ensure form fields are prepopulated before the user interacts with the form.**
- Make the page work both standalone and when opened by an MCP host.
- Use a shared stylesheet for consistent styling.

## Build and verify
- Install dependencies.
- Build the project.
- Confirm the HTML entry is emitted under a build output such as dist/ui/.
- Confirm the server exposes the manifest and serves the built UI.
