// src/geo-form/App.tsx — Geometry Calculator Form Widget
// Connects to the MCP host via @modelcontextprotocol/ext-apps.
// The host calls `open-geo-form` → structuredContent provides available subTypes.
// On submit, calls `geo.calculate` MCP tool via app.callServerTool().
import { App } from '@modelcontextprotocol/ext-apps';
import { createRoot } from 'react-dom/client';
import { useState, useEffect, useCallback } from 'react';
import '../index.css';

// ─── Default operations (used when MCP host does not provide subTypes) ───────
const DEFAULT_OPERATIONS: { value: string; label: string; paramFields: string[] }[] = [
  { value: 'GEO.PERIMETER.TRIANGLE', label: 'Perimeter of Triangle', paramFields: ['a', 'b', 'c'] },
  { value: 'GEO.PERIMETER.RECTANGLE', label: 'Perimeter of Rectangle', paramFields: ['a', 'b'] },
  { value: 'GEO.PERIMETER.CIRCLE', label: 'Circumference of Circle', paramFields: ['radius'] },
  { value: 'GEO.AREA.TRIANGLE', label: 'Area of Triangle (Heron)', paramFields: ['a', 'b', 'c'] },
  { value: 'GEO.AREA.RECTANGLE', label: 'Area of Rectangle', paramFields: ['a', 'b'] },
  { value: 'GEO.AREA.CIRCLE', label: 'Area of Circle', paramFields: ['radius'] },
  { value: 'GEO.AREA.PYRAMID', label: 'Surface Area of Pyramid', paramFields: ['a', 'slant'] },
  { value: 'GEO.AREA.TETRAHEDRON', label: 'Surface Area of Tetrahedron', paramFields: ['side'] },
  { value: 'GEO.VOLUME.PYRAMID', label: 'Volume of Pyramid', paramFields: ['a', 'height'] },
  { value: 'GEO.VOLUME.TETRAHEDRON', label: 'Volume of Tetrahedron', paramFields: ['side'] },
  { value: 'GEO.VOLUME.CUBE', label: 'Volume of Cube', paramFields: ['side'] },
  { value: 'GEO.VOLUME.CUBOID', label: 'Volume of Cuboid', paramFields: ['a', 'b', 'height'] },
  { value: 'GEO.VOLUME.PRISM', label: 'Volume of Prism', paramFields: ['a', 'b', 'height'] },
  { value: 'GEO.VOLUME.CYLINDER', label: 'Volume of Cylinder', paramFields: ['radius', 'height'] },
  { value: 'GEO.VOLUME.CONE', label: 'Volume of Cone', paramFields: ['radius', 'height'] },
  { value: 'GEO.VOLUME.SPHERE', label: 'Volume of Sphere', paramFields: ['radius'] },
];

const PARAM_LABELS: Record<string, string> = {
  side: 'Side Length',
  a: 'First Side (a)',
  b: 'Second Side (b)',
  c: 'Third Side (c)',
  radius: 'Radius',
  height: 'Height',
  slant: 'Slant Height',
};

// ─── Types ───────────────────────────────────────────────────────────────────
interface GeoFormConfig {
  subTypes?: string[];
  labels?: Record<string, string>;
  toolName?: string;
}

interface GeoResult {
  value?: number;
  result?: number;
  subTypeExecuted?: string;
  operation?: string;
  shape?: string;
  formula?: string;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
function buildOperations(config: GeoFormConfig | null) {
  if (!config?.subTypes?.length) return DEFAULT_OPERATIONS;
  return config.subTypes.map((v) => {
    const def = DEFAULT_OPERATIONS.find(op => op.value === v);
    return {
      value: v,
      label: config.labels?.[v] ?? v,
      paramFields: def?.paramFields ?? [],
    };
  });
}

function extractResultValue(structuredContent: unknown): number | null {
  const data = structuredContent as GeoResult;
  if (typeof data?.value === 'number') return data.value;
  if (typeof data?.result === 'number') return data.result;
  return null;
}

function isGeoFormConfig(value: unknown): value is GeoFormConfig {
  if (!value || typeof value !== 'object') return false;
  const candidate = value as GeoFormConfig;
  return Array.isArray(candidate.subTypes) || typeof candidate.toolName === 'string';
}

// ─── Component ───────────────────────────────────────────────────────────────
function GeoFormWidget() {
  const [appInstance] = useState(
    () => new App({ name: 'Geometry Calculator', version: '1.0.0' }),
  );
  const [formConfig, setFormConfig] = useState<GeoFormConfig | null>(null);
  const [operation, setOperation] = useState('GEO.PERIMETER.TRIANGLE');
  const [paramValues, setParamValues] = useState<Record<string, string>>({
    a: '',
    b: '',
    c: '',
    radius: '',
    height: '',
    side: '',
    slant: '',
  });
  const [result, setResult] = useState<number | null>(null);
  const [resultDetails, setResultDetails] = useState<GeoResult | null>(null);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [isConnected, setIsConnected] = useState(false);
  const [toolName, setToolName] = useState('geo_calculate');

  useEffect(() => {
    appInstance.ontoolresult = (toolResult) => {
      if (isGeoFormConfig(toolResult.structuredContent)) {
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
  const currentOp = operations.find(op => op.value === operation);
  const paramFields = currentOp?.paramFields ?? [];

  const handleCalculate = useCallback(async () => {
    setError(null);
    setResult(null);
    setResultDetails(null);

    // Validate all required parameters
    const params: Record<string, number> = {};
    for (const field of paramFields) {
      const val = parseFloat(paramValues[field] || '');
      if (Number.isNaN(val) || val <= 0) {
        setError(`Please enter a valid positive number for ${PARAM_LABELS[field] || field}.`);
        return;
      }
      params[field] = val;
    }

    setSubmitting(true);
    try {
      const timeout = new Promise<never>((_, reject) =>
        setTimeout(() => reject(new Error('Request timed out. Please try again.')), 30_000),
      );

      const toolArgs = {
        subType: operation,
        ...params,
      };

      const toolResult = await Promise.race([
        appInstance.callServerTool({
          name: toolName,
          arguments: toolArgs,
        }),
        timeout,
      ]);

      // Primary: structured content
      if (toolResult.structuredContent) {
        const details = toolResult.structuredContent as GeoResult;
        setResultDetails(details);
        const value = extractResultValue(details);
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
            const parsed = JSON.parse(text) as GeoResult;
            setResultDetails(parsed);
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
  }, [appInstance, operation, paramValues, paramFields, toolName]);

  const handleReset = () => {
    setParamValues({
      a: '',
      b: '',
      c: '',
      radius: '',
      height: '',
      side: '',
      slant: '',
    });
    setResult(null);
    setResultDetails(null);
    setError(null);
  };

  const handleParamChange = (field: string, value: string) => {
    setParamValues(prev => ({
      ...prev,
      [field]: value,
    }));
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
    maxWidth: '520px',
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
    minHeight: '80px',
    display: 'flex',
    flexDirection: 'column',
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

  const paramGridStyle: React.CSSProperties = {
    display: 'grid',
    gridTemplateColumns: paramFields.length > 2 ? '1fr 1fr' : '1fr',
    gap: '0.75rem',
  };

  const allParamsFilled = paramFields.every(field => paramValues[field]?.trim() !== '');

  return (
    <div style={containerStyle}>
      <h1 style={titleStyle}>Geometry Calculator</h1>
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

        {/* Parameter inputs (dynamic based on operation) */}
        <div style={paramGridStyle}>
          {paramFields.map((field) => (
            <div key={field} style={fieldStyle}>
              <label style={labelStyle} htmlFor={field}>
                {PARAM_LABELS[field] || field}
              </label>
              <input
                id={field}
                type="number"
                placeholder="0"
                value={paramValues[field]}
                onChange={(e) => handleParamChange(field, e.target.value)}
                style={inputStyle}
                onKeyDown={(e) => e.key === 'Enter' && allParamsFilled && handleCalculate()}
              />
            </div>
          ))}
        </div>

        {/* Action buttons */}
        <div style={{ display: 'flex', gap: '0.5rem' }}>
          <button
            onClick={handleCalculate}
            disabled={submitting || !allParamsFilled}
            style={{
              ...buttonStyle,
              opacity: submitting || !allParamsFilled ? 0.6 : 1,
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
              {resultDetails?.formula && (
                <div style={{ fontSize: '0.75rem', color: 'var(--text-secondary)', marginBottom: '0.5rem', fontStyle: 'italic' }}>
                  {resultDetails.formula}
                </div>
              )}
              {resultDetails?.shape && (
                <div style={{ fontSize: '0.8rem', color: 'var(--text-secondary)', marginBottom: '0.25rem' }}>
                  {resultDetails.operation} of {resultDetails.shape}
                </div>
              )}
              <div style={{ fontSize: '1.8rem', fontWeight: 700, color: 'var(--accent-blue)' }}>
                {result.toFixed(4).replace(/\.?0+$/, '')}
              </div>
            </div>
          ) : (
            <span style={{ color: 'var(--text-secondary)', fontSize: '0.9rem', textAlign: 'center' }}>
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

createRoot(document.getElementById('root')!).render(<GeoFormWidget />);
