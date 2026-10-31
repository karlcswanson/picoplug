// DHCP option names and value decoding (ported from the shipping page).
import { fmtDur } from './format.js';

const ONAME={1:'Subnet mask',2:'Time offset',3:'Router',4:'Time server',6:'DNS server',7:'Log server',12:'Host name',13:'Boot file size',15:'Domain name',17:'Root path',26:'Interface MTU',28:'Broadcast',31:'Router discovery',33:'Static route',35:'ARP timeout',42:'NTP server',43:'Vendor specific',44:'NetBIOS (WINS)',46:'NetBIOS node type',47:'NetBIOS scope',51:'Lease time',53:'Message type',54:'Server ID',55:'Parameter list',56:'Message',57:'Max message size',58:'Renewal (T1)',59:'Rebind (T2)',60:'Vendor class',61:'Client ID',66:'TFTP server',67:'Boot file',77:'User class',80:'Rapid commit',81:'Client FQDN',82:'Relay agent info',100:'Time zone (POSIX)',101:'Time zone (name)',108:'IPv6-only preferred',114:'Captive portal',116:'Auto-config',118:'Subnet selection',119:'Domain search',120:'SIP servers',121:'Classless routes',125:'Vendor-identifying info',150:'TFTP servers (Cisco)',159:'Port params',160:'Captive portal (old)',209:'PXE config',210:'PXE path prefix',211:'PXE reboot time',249:'Classless routes (MS)',252:'WPAD'};
const OIP=new Set([1,3,4,6,7,28,33,42,44,54,118,120,150]),OSEC=new Set([2,35,51,58,59,108,211]),OSTR=new Set([12,14,15,17,40,47,56,60,66,67,77,100,101,114,160,209,210,252]);
const MSGT={1:'DISCOVER',2:'OFFER',3:'REQUEST',4:'DECLINE',5:'ACK',6:'NAK',7:'RELEASE',8:'INFORM'};
function oip(b){const o=[];for(let i=0;i+4<=b.length;i+=4)o.push(b.slice(i,i+4).join('.'));return o.join(', ')+(b.length%4?' +'+(b.length%4)+' B':'');}
function ostr(b){return (String.fromCharCode(...b.map(c=>c>=32&&c<127?c:63)));}
function ohex(b){return b.map(c=>c.toString(16).padStart(2,'0')).join(' ');}
function ou32(b){let v=0;for(const c of b)v=v*256+c;return v;}
function olabels(b){/* RFC 1035 labels with compression, as in option 119 */
  const names=[];let p=0,cur=[],g=0;
  const walk=(q,depth)=>{while(q<b.length&&b[q]&&depth<8){const l=b[q];if((l&192)===192){walk(((l&63)<<8)|b[q+1],depth+1);return;}cur.push(ostr(b.slice(q+1,q+1+l)));q+=1+l;}};
  while(p<b.length&&g++<32){const l=b[p];if(l===0){p++;if(cur.length)names.push(cur.join('.'));cur=[];continue;}if((l&192)===192){walk(p,0);p+=2;if(cur.length)names.push(cur.join('.'));cur=[];continue;}cur.push(ostr(b.slice(p+1,p+1+l)));p+=1+l;}
  if(cur.length)names.push(cur.join('.'));return names.join(', ');}
function oroutes(b){/* RFC 3442 */const o=[];let p=0;while(p<b.length){const w=b[p++],n=Math.ceil(w/8),d=b.slice(p,p+n);while(d.length<4)d.push(0);p+=n;const g=b.slice(p,p+4);p+=4;o.push(d.join('.')+'/'+w+' via '+g.join('.'));}return o.join(', ');}
function oval(c,b){
  if(c===53)return MSGT[b[0]]||b[0];
  if(c===46)return ({1:'B-node',2:'P-node',4:'M-node',8:'H-node'})[b[0]]||b[0];
  if(c===55)return b.map(x=>ONAME[x]?x+' '+ONAME[x]:x).join(', ');
  if(c===26||c===57||c===13)return ou32(b);
  if(c===119)return olabels(b);
  if(c===121||c===249)return oroutes(b);
  if(c===61&&b[0]===1&&b.length===7)return 'MAC '+ohex(b.slice(1)).replace(/ /g,':');
  if(c===81)return 'flags '+b[0]+' '+ostr(b.slice(3));
  if(OIP.has(c))return oip(b);
  if(OSEC.has(c))return fmtDur(ou32(b))+' ('+ou32(b)+' s)';
  if(OSTR.has(c))return ostr(b);
  return ohex(b);}
export function dhcpOpts(hex){
  const b=[];for(let i=0;i+2<=hex.length;i+=2)b.push(parseInt(hex.substr(i,2),16));
  let p=0;const out=[];
  while(p+2<=b.length){const c=b[p],l=b[p+1],v=b.slice(p+2,p+2+l);p+=2+l;
    out.push({code:c,name:ONAME[c]||'',value:String(oval(c,v))});}
  return out;}
