import React from 'react';
import ReactDOM from 'react-dom/client';
import './index.css';

type FallbackProps = {
  title: string;
  message: string;
};

function FallbackScreen({ title, message }: FallbackProps) {
  return (
    <div
      style={{
        maxWidth: '700px',
        margin: '2rem auto',
        padding: '1.25rem',
        borderRadius: '12px',
        border: '1px solid rgba(255, 255, 255, 0.15)',
        background: 'rgba(0, 0, 0, 0.2)',
        color: 'var(--text-primary)',
        fontFamily: 'system-ui, -apple-system, sans-serif',
      }}
    >
      <h1 style={{ marginTop: 0, fontSize: '1.25rem' }}>{title}</h1>
      <pre
        style={{
          margin: 0,
          whiteSpace: 'pre-wrap',
          wordBreak: 'break-word',
          color: 'var(--text-secondary)',
          fontFamily: 'ui-monospace, SFMono-Regular, Menlo, monospace',
        }}
      >
        {message}
      </pre>
    </div>
  );
}

class RootErrorBoundary extends React.Component<
  { children: React.ReactNode },
  { error: Error | null }
> {
  state = { error: null as Error | null };

  static getDerivedStateFromError(error: Error) {
    return { error };
  }

  render() {
    if (this.state.error) {
      return (
        <FallbackScreen
          title="Dashboard UI failed to render"
          message={this.state.error.message}
        />
      );
    }
    return this.props.children;
  }
}

const rootElement = document.getElementById('root');

if (!rootElement) {
  throw new Error('Failed to find the root element.');
}

const root = ReactDOM.createRoot(rootElement);

async function bootstrap() {
  try {
    const { default: App } = await import('./App.tsx');
    root.render(
      <React.StrictMode>
        <RootErrorBoundary>
          <App />
        </RootErrorBoundary>
      </React.StrictMode>,
    );
  } catch (error) {
    const message = error instanceof Error ? error.stack ?? error.message : String(error);
    root.render(
      <FallbackScreen title="Dashboard UI failed to start" message={message} />,
    );
  }
}

bootstrap();