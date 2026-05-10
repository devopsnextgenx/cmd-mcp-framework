function App() {
  const containerStyle: React.CSSProperties = {
    padding: '2rem',
    borderRadius: '12px',
    backgroundColor: 'var(--card-bg)',
    boxShadow: '0 10px 25px -5px rgba(0, 0, 0, 0.3), 0 0 15px var(--accent-glow)',
    border: '1px solid rgba(255, 255, 255, 0.1)',
    textAlign: 'center',
    maxWidth: '400px',
    width: '90%',
  };

  const titleStyle: React.CSSProperties = {
    margin: '0 0 1rem 0',
    fontSize: '2rem',
    color: 'var(--accent-blue)',
    letterSpacing: '-0.025em',
  };

  const textStyle: React.CSSProperties = {
    color: 'var(--text-secondary)',
    fontSize: '1.1rem',
  };

  return (
    <div style={containerStyle}>
      <h1 style={titleStyle}>MCP Dashboard</h1>
      <p style={textStyle}>UI is successfully running.</p>
      <div style={{ 
        marginTop: '1.5rem', 
        height: '4px', 
        background: 'var(--accent-blue)', 
        borderRadius: '2px',
        opacity: 0.6 
      }} />
    </div>
  );
}

export default App;