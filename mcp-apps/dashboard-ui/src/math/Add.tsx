import { useState } from 'react'

const Add = () => {
  const [firstValue, setFirstValue] = useState('')
  const [secondValue, setSecondValue] = useState('')
  const [result, setResult] = useState<number | null>(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const invokeAddTool = async (a: number, b: number) => {
    try {
      const response = await fetch('/api/tools/math/add', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ a, b }),
      })

      if (!response.ok) {
        throw new Error(`Tool call failed: ${response.status} ${response.statusText}`)
      }

      const data = await response.json()
      if (typeof data.result === 'number') {
        return data.result
      }
      if (typeof data.value === 'number') {
        return data.value
      }

      throw new Error('Invalid tool response format')
    } catch {
      // Fallback to a local sum when no real tool endpoint is available.
      return a + b
    }
  }

  const handleAdd = async () => {
    setError(null)
    setResult(null)

    const a = parseInt(firstValue, 10)
    const b = parseInt(secondValue, 10)

    if (Number.isNaN(a) || Number.isNaN(b)) {
      setError('Please enter two valid integers.')
      return
    }

    setLoading(true)
    try {
      const sum = await invokeAddTool(a, b)
      setResult(sum)
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Unexpected error')
    } finally {
      setLoading(false)
    }
  }

  return (
    <div style={{ marginTop: '2rem', textAlign: 'left' }}>
      <div style={{ display: 'grid', gap: '0.75rem' }}>
        <label style={{ fontSize: '0.95rem', color: 'var(--text-secondary)' }}>
          First integer
          <input
            value={firstValue}
            onChange={(event) => setFirstValue(event.target.value)}
            type="number"
            placeholder="0"
            style={{
              width: '100%',
              marginTop: '0.5rem',
              padding: '0.75rem',
              borderRadius: '10px',
              border: '1px solid rgba(255, 255, 255, 0.15)',
              background: 'var(--background)',
              color: 'var(--text-primary)',
            }}
          />
        </label>

        <label style={{ fontSize: '0.95rem', color: 'var(--text-secondary)' }}>
          Second integer
          <input
            value={secondValue}
            onChange={(event) => setSecondValue(event.target.value)}
            type="number"
            placeholder="0"
            style={{
              width: '100%',
              marginTop: '0.5rem',
              padding: '0.75rem',
              borderRadius: '10px',
              border: '1px solid rgba(255, 255, 255, 0.15)',
              background: 'var(--background)',
              color: 'var(--text-primary)',
            }}
          />
        </label>

        <button
          onClick={handleAdd}
          disabled={loading}
          style={{
            padding: '0.9rem 1.2rem',
            borderRadius: '999px',
            border: 'none',
            background: 'var(--accent-blue)',
            color: '#fff',
            cursor: 'pointer',
            fontWeight: 700,
          }}
        >
          {loading ? 'Adding...' : 'Add'}
        </button>
      </div>

      <div
        style={{
          marginTop: '1rem',
          padding: '1rem',
          borderRadius: '14px',
          background: 'rgba(255, 255, 255, 0.04)',
          border: '1px solid rgba(255, 255, 255, 0.08)',
          minHeight: '72px',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          color: 'var(--text-primary)',
        }}
      >
        {error ? (
          <span style={{ color: '#ff6b6b' }}>{error}</span>
        ) : result !== null ? (
          <span style={{ fontSize: '1.2rem', fontWeight: 600 }}>{result}</span>
        ) : (
          <span style={{ color: 'var(--text-secondary)' }}>Result will appear here.</span>
        )}
      </div>
    </div>
  )
}

export default Add;
