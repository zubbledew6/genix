function DownloadWidget() {
  const [releases, setReleases] = React.useState([]);
  const [loading, setLoading] = React.useState(true);

  React.useEffect(() => {
    async function fetchReleases() {
      try {
        const res = await fetch('https://api.github.com/repos/zubbledew6/genix/releases');
        const data = await res.json();
        if (Array.isArray(data)) setReleases(data);
      } catch (err) {
        console.error('Download fetch error:', err);
      } finally {
        setLoading(false);
      }
    }

    fetchReleases();
  }, []);

  if (loading) {
    return React.createElement('div', { style: { color: '#a0b2c6', padding: '12px 0' } }, 'Checking for releases...');
  }

  // Categorize releases into stable and experimental based on the prerelease flag and naming conventions, very advanced tech right guys? OMG AUTOMATION
  const isExp = (rel) => {
    const title = (rel.name || '').toLowerCase();
    const tag = (rel.tag_name || '').toLowerCase();
    return rel.prerelease || title.includes('experimental') || tag.includes('experimental') || tag.includes('v1.0.0');
  };

  const stableReleases = releases.filter((rel) => !isExp(rel));
  const experimentalReleases = releases.filter((rel) => isExp(rel));

  const latestStable = stableReleases[0] || null;
  const latestExperimental = experimentalReleases[0] || null;

  const olderStable = stableReleases.slice(1);
  const olderExperimental = experimentalReleases.slice(1);


  function renderPanelCard(release, isExperimental) {
    if (!release) return null;

    const isoAsset = release.assets?.find((asset) => asset.name.endsWith('.iso'));
    const downloadUrl = isoAsset ? isoAsset.browser_download_url : release.html_url;
    const formattedDate = new Date(release.published_at).toLocaleDateString('en-GB');
    const sizeGb = isoAsset ? (isoAsset.size / (1024 * 1024 * 1024)).toFixed(1) : null;
    const kickerColor = isExperimental ? '#e3a008' : '#7cc2ff';

    return React.createElement(
      'div',
      { className: 'panel', style: { margin: '20px 0' } },
      React.createElement(
        'div',
        { className: 'panel-header' },
        React.createElement('span', { className: 'panel-kicker', style: { color: kickerColor } }, isExperimental ? 'Experimental Build' : 'Latest Stable Build'),
        React.createElement('h2', { style: { margin: '0 0 8px 0', fontSize: '1.6rem' } }, release.name || release.tag_name)
      ),
      React.createElement(
        'p',
        { className: 'iso-meta', style: { color: '#a0b2c6', fontSize: '0.9rem', margin: '0 0 16px 0' } },
        `${sizeGb ? `${sizeGb} GB · ` : ''}Released ${formattedDate}`
      ),
      release.body && React.createElement(
        'p',
        { style: { color: '#d7dfe8', fontSize: '0.95rem', margin: '0 0 20px 0' } },
        release.body
      ),
      React.createElement(
        'div',
        { className: 'iso-actions', style: { display: 'flex', gap: '12px', alignItems: 'center' } },
        React.createElement('a', { href: downloadUrl, className: 'download-button' }, 'Download ISO'),
        React.createElement('a', { href: release.html_url, target: '_blank', rel: 'noopener noreferrer', className: 'secondary-button' }, 'Release Notes')
      )
    );
  }


  function renderListItems(list) {
    if (list.length === 0) {
      return React.createElement('li', { style: { color: '#888' } }, 'No older releases found.');
    }
    return list.map((rel) =>
      React.createElement(
        'li',
        { key: rel.id },
        React.createElement(
          'a',
          { href: rel.html_url, target: '_blank', rel: 'noopener noreferrer' },
          `${rel.name || rel.tag_name} (${new Date(rel.published_at).toLocaleDateString('en-GB')})`
        )
      )
    );
  }

  return React.createElement(
    'div',
    { style: { width: '100%' } },

    renderPanelCard(latestStable, false),
    renderPanelCard(latestExperimental, true),


    React.createElement(
      'section',
      { className: 'older-isos', style: { marginTop: '32px' } },
      React.createElement('h2', { style: { fontSize: '1.5rem', color: '#fff', marginBottom: '16px' } }, 'Older ISOs'),
      
      React.createElement(
        'details',
        { className: 'iso-dropdown' },
        React.createElement('summary', null, 'Stable Releases'),
        React.createElement('ul', { className: 'page-list' }, renderListItems(olderStable))
      ),

      React.createElement(
        'details',
        { className: 'iso-dropdown' },
        React.createElement('summary', null, 'Experimental / Unstable Releases'),
        React.createElement('ul', { className: 'page-list' }, renderListItems(olderExperimental))
      )
    )
  );
}

const container = document.getElementById('react-download-widget');
if (container) {
  const root = ReactDOM.createRoot(container);
  root.render(React.createElement(DownloadWidget));
}