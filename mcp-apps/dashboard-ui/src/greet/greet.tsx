import { useCallback, useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from '@modelcontextprotocol/ext-apps';
import '../index.css';

interface GreetWidgetProps {
  name?: string;
}

interface GreetFormConfig {
  name?: string;
  toolName?: string;
  args?: GreetFormConfig;
}

interface GreetResult {
  value?: string;
  result?: string;
  greeting?: string;
  message?: string;
  name?: string;
}

function getGreeting(name: string | undefined) {
  const trimmedName = name?.trim();
  return trimmedName ? `Hello ${trimmedName}!!!` : 'Hello World!!!';
}

function extractResultValue(structuredContent: unknown): string | null {
  const data = structuredContent as GreetResult;
  if (typeof data?.value === 'string') return data.value;
  if (typeof data?.result === 'string') return data.result;
  if (typeof data?.greeting === 'string') return data.greeting;
  if (typeof data?.message === 'string') return data.message;
  if (typeof data?.name === 'string') return `Hello ${data.name}!!!`;
  return null;
}

function isGreetFormConfig(value: unknown): value is GreetFormConfig {
  if (!value || typeof value !== 'object') return false;
  const candidate = value as GreetFormConfig;
  return typeof candidate.name === 'string' || typeof candidate.toolName === 'string' || !!candidate.args;
}

export function GreetWidget({ name: initialName }: GreetWidgetProps) {
  const [appInstance] = useState(
    () => new App({ name: 'Greetings', version: '1.0.0' }),
  );
  const [name, setName] = useState(initialName ?? '');
  const [submitting, setSubmitting] = useState(false);
  const [result, setResult] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [isConnected, setIsConnected] = useState(false);
  const [toolName, setToolName] = useState('greeting_greet');

  useEffect(() => {
    appInstance.ontoolresult = (toolResult) => {
      if (isGreetFormConfig(toolResult.structuredContent)) {
        const data = toolResult.structuredContent;
        if (typeof data.name === 'string') {
          setName(data.name);
        }
        if (data.toolName) {
          setToolName(data.toolName);
        }
        if (data.args && typeof data.args === 'object') {
          const args = data.args as GreetFormConfig;
          if (typeof args.name === 'string') {
            setName(args.name);
          }
        }
      }
      setIsConnected(true);
    };

    appInstance
      .connect()
      .then(() => setIsConnected(true))
      .catch(() => {
        setIsConnected(false);
      });
  }, [appInstance]);

  const handleGreet = useCallback(async () => {
    setError(null);
    setResult(null);

    if (!name.trim()) {
      setError('Please enter a name.');
      return;
    }

    setSubmitting(true);
    try {
      const timeout = new Promise<never>((_, reject) =>
        setTimeout(() => reject(new Error('Request timed out. Please try again.')), 30_000),
      );

      const toolResult = await Promise.race([
        appInstance.callServerTool({
          name: toolName,
          arguments: { name: name.trim() },
        }),
        timeout,
      ]);

      if (toolResult.structuredContent) {
        const value = extractResultValue(toolResult.structuredContent);
        if (typeof value === 'string') {
          setResult(value);
          return;
        }
      }

      if (typeof toolResult.rawContent === 'string' && toolResult.rawContent.trim()) {
        setResult(toolResult.rawContent);
        return;
      }

      setError('Unexpected response format from the server.');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'An unknown error occurred.');
    } finally {
      setSubmitting(false);
    }
  }, [appInstance, name, toolName]);

  const handleReset = () => {
    setName('');
    setResult(null);
    setError(null);
  };

  const greeting = useMemo(() => getGreeting(name), [name]);

  const containerStyle: React.CSSProperties = {
    padding: '2rem',
    borderRadius: '12px',
    backgroundColor: 'var(--card-bg)',
    boxShadow: '0 10px 25px -5px rgba(0,0,0,0.3), 0 0 15px var(--accent-glow)',
    border: '1px solid rgba(255,255,255,0.1)',
    maxWidth: '480px',
    width: '90%',
  };

  const titleStyle: React.CSSProperties = {
    margin: '0 0 0.25rem 0',
    fontSize: '1.6rem',
    color: 'var(--accent-blue)',
    letterSpacing: '-0.025em',
  };

  const subtitleStyle: React.CSSProperties = {
    fontSize: '0.85rem',
    color: 'var(--text-secondary)',
    marginBottom: '1.5rem',
  };

  const fieldStyle: React.CSSProperties = {
    display: 'flex',
    flexDirection: 'column',
    gap: '0.4rem',
  };

  const labelStyle: React.CSSProperties = {
    fontSize: '0.875rem',
    color: 'var(--text-secondary)',
    fontWeight: 500,
  };

  const inputStyle: React.CSSProperties = {
    padding: '0.75rem',
    borderRadius: '8px',
    border: '1px solid rgba(255,255,255,0.15)',
    background: 'var(--bg-color)',
    color: 'var(--text-primary)',
    fontSize: '1rem',
    outline: 'none',
    width: '100%',
    boxSizing: 'border-box' as const,
  };

  const buttonStyle: React.CSSProperties = {
    padding: '0.85rem 1.5rem',
    borderRadius: '999px',
    border: 'none',
    background: submitting ? 'rgba(59,130,246,0.5)' : 'var(--accent-blue)',
    color: '#fff',
    cursor: submitting ? 'not-allowed' : 'pointer',
    fontWeight: 700,
    fontSize: '1rem',
    transition: 'background 0.2s',
    flex: 1,
  };

  const resetButtonStyle: React.CSSProperties = {
    ...buttonStyle,
    background: 'rgba(255,255,255,0.08)',
    cursor: 'pointer',
    flex: 'none',
    padding: '0.85rem 1rem',
  };

  const resultBoxStyle: React.CSSProperties = {
    marginTop: '1rem',
    padding: '1rem',
    borderRadius: '10px',
    background: 'rgba(255,255,255,0.04)',
    border: '1px solid rgba(255,255,255,0.08)',
    minHeight: '64px',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    color: 'var(--text-primary)',
  };

  const statusDotStyle: React.CSSProperties = {
    width: '8px',
    height: '8px',
    borderRadius: '50%',
    background: isConnected ? '#4cff8a' : '#94a3b8',
    display: 'inline-block',
    marginRight: '6px',
  };

  return (
    <div style={containerStyle}>
      <h1 style={titleStyle}>Greetings</h1>
      <p style={subtitleStyle}>
        <span style={statusDotStyle} />
        {isConnected ? 'Connected to MCP server' : 'Standalone mode'}
        {' · '}Powered by C++ MCP server
      </p>

      <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
        <div style={fieldStyle}>
          <label style={labelStyle} htmlFor="name">Name</label>
          <input
            id="name"
            value={name}
            onChange={(event) => {
              setName(event.target.value);
              setResult(null);
              setError(null);
            }}
            placeholder="Type a name"
            style={inputStyle}
            onKeyDown={(event) => event.key === 'Enter' && handleGreet()}
          />
        </div>

        <div style={{ display: 'flex', gap: '0.5rem' }}>
          <button
            onClick={handleGreet}
            disabled={submitting || !name.trim()}
            style={{
              ...buttonStyle,
              opacity: submitting || !name.trim() ? 0.6 : 1,
            }}
          >
            {submitting ? 'Greeting…' : 'Greet'}
          </button>
          <button onClick={handleReset} style={resetButtonStyle} title="Reset form">
            ↺
          </button>
        </div>

        <div style={resultBoxStyle}>
          {error ? (
            <span style={{ color: '#ff6b6b', fontSize: '0.9rem', textAlign: 'center' }}>{error}</span>
          ) : result !== null ? (
            <div style={{ textAlign: 'center' }}>
              <div style={{ fontSize: '0.8rem', color: 'var(--text-secondary)', marginBottom: '0.25rem' }}>
                {greeting}
              </div>
              <div style={{ fontSize: '1.4rem', fontWeight: 700, color: 'var(--accent-blue)' }}>
                {result}
              </div>
            </div>
          ) : (
            <span style={{ color: 'var(--text-secondary)', fontSize: '0.9rem' }}>
              Result will appear here
            </span>
          )}
        </div>
      </div>

      <div
        style={{
          marginTop: '1.5rem',
          height: '3px',
          background: 'var(--accent-blue)',
          borderRadius: '2px',
          opacity: 0.4,
        }}
      />
    </div>
  );
}

if (typeof document !== 'undefined') {
  const mountNode = document.getElementById('root');
  if (mountNode) {
    createRoot(mountNode).render(<GreetWidget />);
  }
}
