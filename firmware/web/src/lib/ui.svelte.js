// Expanders and the chosen neighbor, kept in localStorage (same key as the shipping page).

const KEY = 'picolldp.ui';
function load() { try { return JSON.parse(localStorage.getItem(KEY) || '{}'); } catch (e) { return {}; } }

export const ui = $state(load());
export const sel = $state({ key: null });

export function toggle(k) {
  ui[k] = !ui[k];
  try { localStorage.setItem(KEY, JSON.stringify(ui)); } catch (e) {}
}
