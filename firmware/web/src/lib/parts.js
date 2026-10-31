// Building blocks for the "a · b · c" value lines, rendered by Parts.svelte.
// A part is a string, one of the objects below, or an array of parts
// (concatenated with no separator). Empty parts are dropped.

const empty = v => v === undefined || v === null || v === '';
/** one tappable value inside a multi-value row: tapping copies just v (label is display only) */
export const sg = (v, label) => empty(v) ? '' : { sg: String(v), label };
export const nw = v => empty(v) ? '' : { cls: 'nw', t: String(v) };
export const mut = t => ({ cls: 'c-mut', t });
export const ok = t => ({ cls: 'c-ok', t });
export const say = t => ({ cls: 'say', t });
export const yn = (good, t) => good ? ok(t) : t;
export const isEmpty = p => !p || (Array.isArray(p) && p.every(isEmpty));
