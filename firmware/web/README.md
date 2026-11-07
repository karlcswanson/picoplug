# picoplug web app (Svelte)

The page the firmware serves, written in Svelte 5. The firmware build runs
`npm run build` and embeds `dist/index.html`, so building the firmware needs Node.

```
npm install
npm run dev      # dev server; proxies /status.json etc. to the board at 192.168.7.1
BOARD=http://127.0.0.1:8080 npm run dev   # or to firmware/test/web_mock.py
npm run build    # dist/index.html, one self-contained file like the shipping page
```

Layout:

* `src/app.css` - the shipping page's styles; the card footer now wraps its buttons under the text when both don't fit
* `src/App.svelte` - tabs and the order of the Port tab's cards
* `src/Header.svelte` - header band, link lights, tabs
* `src/cards/` - one file per card: switch, Network, spanning tree, LAG,
  802.1X, IGMP/PIM, PTP, other probes, Listening, No link, Last port
* `src/views/` - the Tools, Log and Setup tabs
* `src/lib/` - polling (`status.svelte.js`), tab and settings state (`app.svelte.js`),
  ports visited and recent runs (`history.svelte.js`), clipboard, DHCP option decoding,
  formatting helpers, and small shared pieces (`Kv`, `Parts`/`Ln` for "a · b · c" value
  lines, `ProtoCard`, `More`)
