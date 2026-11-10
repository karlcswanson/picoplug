// Clipboard with the execCommand fallback (the board serves plain http).

import { app, go } from './app.svelte.js';

export const toast = $state({ msg: '', on: false });
let t;
export function say(msg) { toast.msg = msg; toast.on = true; clearTimeout(t); t = setTimeout(() => toast.on = false, 1400); }

export function copyText(s) {
  if (navigator.clipboard && window.isSecureContext)
    return navigator.clipboard.writeText(s).then(() => say('Copied')).catch(() => legacy(s));
  return legacy(s);
}

function legacy(s) {
  const ta = document.createElement('textarea');
  ta.value = s; ta.setAttribute('readonly', ''); ta.style.cssText = 'position:fixed;left:-9999px;top:0';
  // an open modal dialog (the Share sheet) makes the rest of the page inert: the text box has to be inside it
  const host = document.querySelector('dialog[open]') || document.body;
  host.appendChild(ta);
  ta.focus(); ta.select(); ta.setSelectionRange(0, s.length);
  let ok = false;
  try { ok = document.execCommand('copy'); } catch (e) {}
  ta.remove();
  if (ok) say('Copied');
  else { app.fallback = s; app.fbFocus = true; go('#log'); say('Copy manually'); }
  return Promise.resolve(ok);
}

// Svelte action: tap the element to copy `value`
export function copyable(node, value) {
  let v = value;
  const h = e => { if (e.target.closest('button,a,input,label')) return; e.stopPropagation(); copyText(String(v)); };
  node.addEventListener('click', h);
  return { update(n) { v = n; }, destroy() { node.removeEventListener('click', h); } };
}
