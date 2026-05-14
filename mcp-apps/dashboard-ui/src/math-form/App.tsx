// src/math-form/App.tsx — Math Calculator Form Widget
// Connects to the MCP host via @modelcontextprotocol/ext-apps.
// The host calls `open-math-form` → structuredContent provides available subTypes.
// On submit, calls `math.calculate` MCP tool via app.callServerTool().
import { App } from '@modelcontextprotocol/ext-apps';
import { createRoot } from 'react-dom/client';
import { useState, useEffect, useCallback } from 'react';
import '../index.css';

// ─── Default operations (used when MCP host does not provide subTypes) ───────
const DEFAULT_OPERATIONS: { value: string; label: string }[] = [
  { value: 'MATH.ADD', label: 'Add (+)' },
  { value: 'MATH.SUB', label: 'Subtract (−)' },
  { value: 'MATH.MUL', label: 'Multiply (×)' },
  { value: 'MATH.DIV', label: 'Divide (÷)' },
  { value: 'MATH.MOD', label: 'Modulo (%)' },
  { value: 'MATH.POW', label: 'Power (^)' },
];

// ─── Types ───────────────────────────────────────────────────────────────────
interface MathFormConfig {
  subTypes?: string[];
  labels?: Record<string, string>;
  toolName?: string;
}

interface MathResult {
  value?: number;
  result?: number;
  subTypeExecuted?: string;
  operation?: string;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
function buildOperations(config: MathFormConfig | null) {
  if (!config?.subTypes?.length) return DEFAULT_OPERATIONS;
  return config.subTypes.map((v) => ({
    value: v,
    label: config.labels?.[v] ?? v,
  }));
}

function extractResultValue(structuredContent: unknown): number | null {
  const data = structuredContent as MathResult;
  if (typeof data?.value === 'number') return data.value;
  if (typeof data?.result === 'number') return data.result;
  return null;
}

function isMathFormConfig(value: unknown): value is MathFormConfig {
  if (!value || typeof value !== 'object') return false;
  const candidate = value as MathFormConfig;
  return Array.isArray(candidate.subTypes) || typeof candidate.toolName === 'string';
}

// ─── Component ───────────────────────────────────────────────────────────────
function MathFormWidget() {
  const [appInstance] = useState(
    () => new App({ name: 'Math Calculator', version: '1.0.0' }),
  );
  const [formConfig, setFormConfig] = useState<MathFormConfig | null>(null);
  const [operation, setOperation] = useState('MATH.ADD');
  const [leftValue, setLeftValue] = useState('');
  const [rightValue, setRightValue] = useState('');
  const [result, setResult] = useState<number | null>(null);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [isConnected, setIsConnected] = useState(false);
  const [toolName, setToolName] = useState('math_calculate');

  useEffect(() => {
    appInstance.ontoolresult = (toolResult) => {
      if (isMathFormConfig(toolResult.structuredContent)) {
        const data = toolResult.structuredContent;
        setFormConfig(data);
        if (data.toolName) {
          setToolName(data.toolName);
        }
        if (data.subTypes?.length) {
          setOperation(data.subTypes[0]);
        }
      }
      setIsConnected(true);
    };

    appInstance
      .connect()
      .then(() => setIsConnected(true))
      .catch(() => {
        // Gracefully degrade — the form still works; users can enter values manually.
        setIsConnected(false);
      });
  }, [appInstance]);

  const operations = buildOperations(formConfig);

  const handleCalculate = useCallback(async () => {
    setError(null);
    setResult(null);

    const left = parseFloat(leftValue);
    const right = parseFloat(rightValue);

    if (Number.isNaN(left) || Number.isNaN(right)) {
      setError('Please enter two valid numbers.');
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
          arguments: { subType: operation, left, right },
        }),
        timeout,
      ]);

      // Primary: structured content
      if (toolResult.structuredContent) {
        const value = extractResultValue(toolResult.structuredContent);
        if (typeof value === 'number') {
          setResult(value);
          return;
        }
      }

      // Fallback: parse text content
      const contentArray = (toolResult as any).content;
      if (Array.isArray(contentArray) && contentArray.length > 0) {
        const text = contentArray[0]?.text as string | undefined;
        if (text) {
          try {
            const parsed = JSON.parse(text) as MathResult;
            const value = extractResultValue(parsed);
            if (typeof value === 'number') {
              setResult(value);
              return;
            }
          } catch {
            // text was not JSON
          }
          // Try parsing numeric text directly
          const num = parseFloat(text);
          if (!Number.isNaN(num)) {
            setResult(num);
            return;
          }
        }
      }

      setError('Unexpected response from server. Could not extract a numeric result.');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to calculate. Please try again.');
    } finally {
      setSubmitting(false);
    }
  }, [appInstance, operation, leftValue, rightValue, toolName]);

  const handleReset = () => {
    setLeftValue('');
    setRightValue('');
    setResult(null);
    setError(null);
  };

  // ─── Styles ────────────────────────────────────────────────────────────────
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

  const selectStyle: React.CSSProperties = {
    ...inputStyle,
    cursor: 'pointer',
    appearance: 'none' as const,
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
      <h1 style={titleStyle}>Math Calculator</h1>
      <p style={subtitleStyle}>
        <span style={statusDotStyle} />
        {isConnected ? 'Connected to MCP server' : 'Standalone mode'}
        {' · '}Powered by C++ MCP server
      </p>

      <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
        {/* Operation selector */}
        <div style={fieldStyle}>
          <label style={labelStyle} htmlFor="operation">Operation</label>
          <select
            id="operation"
            value={operation}
            onChange={(e) => { setOperation(e.target.value); setResult(null); setError(null); }}
            style={selectStyle}
          >
            {operations.map((op) => (
              <option key={op.value} value={op.value}>{op.label}</option>
            ))}
          </select>
        </div>

        {/* Two number inputs */}
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0.75rem' }}>
          <div style={fieldStyle}>
            <label style={labelStyle} htmlFor="left">First number</label>
            <input
              id="left"
              type="number"
              placeholder="0"
              value={leftValue}
              onChange={(e) => { setLeftValue(e.target.value); setResult(null); setError(null); }}
              style={inputStyle}
              onKeyDown={(e) => e.key === 'Enter' && handleCalculate()}
            />
          </div>
          <div style={fieldStyle}>
            <label style={labelStyle} htmlFor="right">Second number</label>
            <input
              id="right"
              type="number"
              placeholder="0"
              value={rightValue}
              onChange={(e) => { setRightValue(e.target.value); setResult(null); setError(null); }}
              style={inputStyle}
              onKeyDown={(e) => e.key === 'Enter' && handleCalculate()}
            />
          </div>
        </div>

        {/* Action buttons */}
        <div style={{ display: 'flex', gap: '0.5rem' }}>
          <button
            onClick={handleCalculate}
            disabled={submitting || !leftValue || !rightValue}
            style={{
              ...buttonStyle,
              opacity: submitting || !leftValue || !rightValue ? 0.6 : 1,
            }}
          >
            {submitting ? 'Calculating…' : 'Calculate'}
          </button>
          <button onClick={handleReset} style={resetButtonStyle} title="Reset form">
            ↺
          </button>
        </div>

        {/* Result area */}
        <div style={resultBoxStyle}>
          {error ? (
            <span style={{ color: '#ff6b6b', fontSize: '0.9rem', textAlign: 'center' }}>{error}</span>
          ) : result !== null ? (
            <div style={{ textAlign: 'center' }}>
              <div style={{ fontSize: '0.8rem', color: 'var(--text-secondary)', marginBottom: '0.25rem' }}>
                {leftValue} {operations.find((o) => o.value === operation)?.label?.split(' ')[0]} {rightValue} =
              </div>
              <div style={{ fontSize: '1.8rem', fontWeight: 700, color: 'var(--accent-blue)' }}>
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

      {/* Divider */}
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

createRoot(document.getElementById('root')!).render(<MathFormWidget />);
