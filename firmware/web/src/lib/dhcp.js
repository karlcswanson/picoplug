// Decodes the raw DHCP options the device sends (hex of code, length, value
// triples) into rows of { code, name, value } for the Network card.
import { fmtDur } from './format.js';

const NAMES = {
  1: 'Subnet mask', 2: 'Time offset', 3: 'Router', 4: 'Time server', 6: 'DNS server', 7: 'Log server',
  12: 'Host name', 13: 'Boot file size', 15: 'Domain name', 17: 'Root path', 26: 'Interface MTU',
  28: 'Broadcast', 31: 'Router discovery', 33: 'Static route', 35: 'ARP timeout', 42: 'NTP server',
  43: 'Vendor specific', 44: 'NetBIOS (WINS)', 46: 'NetBIOS node type', 47: 'NetBIOS scope',
  51: 'Lease time', 53: 'Message type', 54: 'Server ID', 55: 'Parameter list', 56: 'Message',
  57: 'Max message size', 58: 'Renewal (T1)', 59: 'Rebind (T2)', 60: 'Vendor class', 61: 'Client ID',
  66: 'TFTP server', 67: 'Boot file', 77: 'User class', 80: 'Rapid commit', 81: 'Client FQDN',
  82: 'Relay agent info', 100: 'Time zone (POSIX)', 101: 'Time zone (name)', 108: 'IPv6-only preferred',
  114: 'Captive portal', 116: 'Auto-config', 118: 'Subnet selection', 119: 'Domain search',
  120: 'SIP servers', 121: 'Classless routes', 125: 'Vendor-identifying info', 150: 'TFTP servers (Cisco)',
  159: 'Port params', 160: 'Captive portal (old)', 209: 'PXE config', 210: 'PXE path prefix',
  211: 'PXE reboot time', 249: 'Classless routes (MS)', 252: 'WPAD',
};

// options whose value is a list of IPv4 addresses, a count of seconds, or text
const IP_LISTS = new Set([1, 3, 4, 6, 7, 28, 33, 42, 44, 54, 118, 120, 150]);
const SECONDS = new Set([2, 35, 51, 58, 59, 108, 211]);
const TEXT = new Set([12, 14, 15, 17, 40, 47, 56, 60, 66, 67, 77, 100, 101, 114, 160, 209, 210, 252]);
const NUMBERS = new Set([13, 26, 57]);

const MESSAGE_TYPES = { 1: 'DISCOVER', 2: 'OFFER', 3: 'REQUEST', 4: 'DECLINE', 5: 'ACK', 6: 'NAK', 7: 'RELEASE', 8: 'INFORM' };
const NODE_TYPES = { 1: 'B-node', 2: 'P-node', 4: 'M-node', 8: 'H-node' };

const hex = bytes => bytes.map(b => b.toString(16).padStart(2, '0')).join(' ');
// printable ASCII as is, anything else as '?'
const text = bytes => String.fromCharCode(...bytes.map(b => (b >= 32 && b < 127 ? b : 63)));
const number = bytes => bytes.reduce((v, b) => v * 256 + b, 0);

// "a.b.c.d, e.f.g.h", plus a note when the length isn't a multiple of 4
function ipList(bytes) {
  const ips = [];
  for (let i = 0; i + 4 <= bytes.length; i += 4) ips.push(bytes.slice(i, i + 4).join('.'));
  const extra = bytes.length % 4;
  return ips.join(', ') + (extra ? ' +' + extra + ' B' : '');
}

// Domain search list (option 119): DNS names as length-prefixed labels, where a
// label length with the top two bits set is a pointer to labels earlier in the option.
function domainList(bytes) {
  const names = [];
  let labels = [];
  const endName = () => { if (labels.length) names.push(labels.join('.')); labels = []; };
  const isPointer = len => (len & 0xc0) === 0xc0;
  // follow a pointer and collect the labels it leads to (depth-limited against loops)
  const follow = (at, depth) => {
    while (at < bytes.length && bytes[at] && depth < 8) {
      const len = bytes[at];
      if (isPointer(len)) { follow(((len & 0x3f) << 8) | bytes[at + 1], depth + 1); return; }
      labels.push(text(bytes.slice(at + 1, at + 1 + len)));
      at += 1 + len;
    }
  };
  let at = 0;
  for (let guard = 0; at < bytes.length && guard < 32; guard++) {
    const len = bytes[at];
    if (len === 0) { at++; endName(); }
    else if (isPointer(len)) { follow(at, 0); at += 2; endName(); }
    else { labels.push(text(bytes.slice(at + 1, at + 1 + len))); at += 1 + len; }
  }
  endName();
  return names.join(', ');
}

// Classless static routes (options 121 and 249): prefix length, the significant
// bytes of the destination, then a 4-byte gateway.
function routes(bytes) {
  const out = [];
  let at = 0;
  while (at < bytes.length) {
    const width = bytes[at++], n = Math.ceil(width / 8);
    const dest = bytes.slice(at, at + n);
    while (dest.length < 4) dest.push(0);
    at += n;
    const gw = bytes.slice(at, at + 4);
    at += 4;
    out.push(dest.join('.') + '/' + width + ' via ' + gw.join('.'));
  }
  return out.join(', ');
}

function decode(code, v) {
  switch (code) {
    case 53: return MESSAGE_TYPES[v[0]] || v[0];
    case 46: return NODE_TYPES[v[0]] || v[0];
    case 55: return v.map(c => (NAMES[c] ? c + ' ' + NAMES[c] : c)).join(', ');
    case 119: return domainList(v);
    case 121: case 249: return routes(v);
    case 81: return 'flags ' + v[0] + ' ' + text(v.slice(3));
    case 61: if (v[0] === 1 && v.length === 7) return 'MAC ' + hex(v.slice(1)).replace(/ /g, ':');
      break;
  }
  if (NUMBERS.has(code)) return number(v);
  if (IP_LISTS.has(code)) return ipList(v);
  if (SECONDS.has(code)) return fmtDur(number(v)) + ' (' + number(v) + ' s)';
  if (TEXT.has(code)) return text(v);
  return hex(v);
}

export function dhcpOpts(raw) {
  const bytes = [];
  for (let i = 0; i + 2 <= raw.length; i += 2) bytes.push(parseInt(raw.substr(i, 2), 16));
  const out = [];
  for (let at = 0; at + 2 <= bytes.length;) {
    const code = bytes[at], len = bytes[at + 1], value = bytes.slice(at + 2, at + 2 + len);
    at += 2 + len;
    out.push({ code, name: NAMES[code] || '', value: String(decode(code, value)) });
  }
  return out;
}
