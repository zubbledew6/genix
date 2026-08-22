import React from 'https://esm.sh/react@18';
import { createRoot } from 'https://esm.sh/react-dom@18/client';
import ReleaseExplorer from './ReleaseExplorer.js';

const container = document.getElementById('react-release-explorer');
if (container) {
  const root = createRoot(container);
  root.render(React.createElement(ReleaseExplorer));
}