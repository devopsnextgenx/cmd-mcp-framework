import { useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import '../index.css';

interface HelloWorldWidgetProps {
  name?: string;
}

function getGreeting(name: string | undefined) {
  const trimmedName = name?.trim();
  return trimmedName ? `Hello ${trimmedName}!!!` : 'Hello World!!!';
}

export function HelloWorldWidget({ name: initialName }: HelloWorldWidgetProps) {
  const [name, setName] = useState(initialName ?? '');

  useEffect(() => {
    setName(initialName ?? '');
  }, [initialName]);

  const greeting = useMemo(() => getGreeting(name), [name]);

  const containerStyle: React.CSSProperties = {
    padding: '2rem',
    borderRadius: '12px',
    backgroundColor: 'var(--card-bg)',
    boxShadow: '0 10px 25px -5px rgba(0, 0, 0, 0.3), 0 0 15px var(--accent-glow)',
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
    fontSize: '0.9rem',
    color: 'var(--text-secondary)',
    marginBottom: '1.25rem',
  };

  const inputStyle: React.CSSProperties = {
    padding: '0.8rem 0.9rem',
    borderRadius: '8px',
    border: '1px solid rgba(255,255,255,0.15)',
    backgroundColor: 'rgba(15, 23, 42, 0.6)',
    color: 'var(--text-primary)',
    fontSize: '1rem',
    outline: 'none',
  };

  const greetingStyle: React.CSSProperties = {
    marginTop: '1rem',
    padding: '1rem',
    borderRadius: '8px',
    backgroundColor: 'rgba(59, 130, 246, 0.16)',
    color: 'var(--text-primary)',
    fontSize: '1.15rem',
    fontWeight: 600,
    minHeight: '3rem',
    display: 'flex',
    alignItems: 'center',
  };

  return (
    <div style={containerStyle}>
      <h1 style={titleStyle}>Hello World</h1>
      <p style={subtitleStyle}>Enter a name to personalize the greeting.</p>
      <label style={{ display: 'flex', flexDirection: 'column', gap: '0.4rem' }}>
        <span>Name</span>
        <input
          value={name}
          onChange={(event) => setName(event.target.value)}
          placeholder="Type a name"
          style={inputStyle}
        />
      </label>
      <div style={greetingStyle}>{greeting}</div>
    </div>
  );
}

const mountNode = document.getElementById('root');

if (mountNode) {
  createRoot(mountNode).render(<HelloWorldWidget />);
}
