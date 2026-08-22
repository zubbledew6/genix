import React, { useState, useEffect } from 'https://esm.sh/react@18';

export default function ReleaseExplorer() {
  const [releases, setReleases] = useState([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    async function fetchReleases() {
      try {
        const res = await fetch('https://api.github.com/repos/zubbledew6/genix/releases', {
          headers: { 'Accept': 'application/vnd.github.v3+json' }
        });
        const data = await res.json();
        setReleases(Array.isArray(data) && data.length > 0 ? data : []);
      } catch (err) {
        console.error('Fetch error:', err);
      } finally {
        setLoading(false);
      }
    }

    fetchReleases();
  }, []);

  if (loading) {
    return React.createElement('div', { style: { color: '#888', padding: '1rem', textAlign: 'center' } }, 'Loading releases from GitHub...');
  }

  if (releases.length === 0) {
    return React.createElement('div', { style: { color: '#888', padding: '1rem', textAlign: 'center' } }, 'No releases found.');
  }

  return React.createElement(
    'div',
    { style: { maxWidth: '900px', margin: '0 auto', width: '100%' } },
    releases.map((rel) => {
      // Check if the release is experimental based on its name or tag, cause for some reason it doesn't always set the prerelease flag correctly
      const title = (rel.name || '').toLowerCase();
      const tag = (rel.tag_name || '').toLowerCase();
      const isExperimental = rel.prerelease || title.includes('experimental') || tag.includes('experimental');

      return React.createElement(
        'div',
        {
          key: rel.id,
          style: {
            background: 'rgba(255, 255, 255, 0.04)',
            border: '1px solid rgba(255, 255, 255, 0.1)',
            borderRadius: '10px',
            padding: '1.25rem',
            marginBottom: '1rem',
            textAlign: 'left'
          }
        },
        React.createElement(
          'div',
          { style: { display: 'flex', justifyContent: 'space-between', alignItems: 'center' } },
          React.createElement('h3', { style: { margin: 0, color: '#fff', fontSize: '1.2rem' } }, rel.name || rel.tag_name),
          React.createElement(
            'span',
            {
              style: {
                fontSize: '0.75rem',
                background: isExperimental ? '#e3a008' : '#0070f3',
                color: isExperimental ? '#000' : '#fff',
                fontWeight: 'bold',
                padding: '0.25rem 0.5rem',
                borderRadius: '4px'
              }
            },
            isExperimental ? 'Experimental' : 'Stable'
          )
        ),
        React.createElement(
          'p',
          { style: { color: '#ccc', fontSize: '0.9rem', marginTop: '0.75rem', whiteSpace: 'pre-wrap' } },
          rel.body || 'No release details provided.'
        ),
        React.createElement(
          'a',
          {
            href: rel.html_url,
            target: '_blank',
            rel: 'noopener noreferrer',
            style: { color: '#0070f3', fontSize: '0.85rem', textDecoration: 'none' }
          },
          'View changes on GitHub'
        )
      );
    })
  );
}