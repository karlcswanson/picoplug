import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';

// `npm run dev` proxies the board's endpoints. Point it at the board
// (default) or at firmware/test/web_mock.py for canned status data:
//   BOARD=http://127.0.0.1:8080 npm run dev
const board = process.env.BOARD || 'http://192.168.7.1';
const proxy = Object.fromEntries(['/status.json', '/tool', '/settings', '/reboot', '/icon.png', '/manifest.json']
  .map(p => [p, { target: board, changeOrigin: true }]));

// One self-contained index.html, the way the firmware embeds it.
export default defineConfig({
  plugins: [svelte(), viteSingleFile()],
  build: { target: 'es2020', cssCodeSplit: false, assetsInlineLimit: 100000000 },
  server: { proxy },
});
