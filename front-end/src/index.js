import React from 'react';
import ReactDOM from 'react-dom/client';
import './index.css';
import App from './App';

const root = ReactDOM.createRoot(document.getElementById('root'));
root.render(
  <React.StrictMode>
    <div id='title'><h1>3D reconstruction</h1><a href='#'>GitHub</a><a href='#'>YouTube</a></div>
    <App />
  </React.StrictMode>
);
