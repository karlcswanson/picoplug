<script>
  // other picoplug probes heard on this port (EtherType 0x88B5) and the two-probe path test
  import ProtoCard from './ProtoCard.svelte';
  import Kv from '../lib/Kv.svelte';
  import Ln from '../lib/Ln.svelte';
  import Icon from '../lib/Icon.svelte';
  import ProbeBadge from '../lib/ProbeBadge.svelte';
  import Parts from '../lib/Parts.svelte';
  import { sg } from '../lib/parts.js';
  import { fmtAgo, waiting, stripKind, isP, RES } from '../lib/format.js';
  import { tool } from '../lib/app.svelte.js';
  let { ps, pt, cur, up } = $props();

  let list = $derived((ps && ps.list) || []);
  let phase = $derived((pt && pt.phase) || '');
  let busy = $derived(['asking', 'watching', 'recovering'].includes(phase));
  let w = $derived(waiting(up, 7));
  let myCh = $derived(stripKind((cur && cur.chassis_id) || ''));
  const sec = ms => (ms / 1000).toFixed(ms < 10000 ? 1 : 0) + ' s';

  function where(p) {
    if (p.sees_us) return 'neighbor: this probe';
    if (p.chassis && myCh && stripKind(p.chassis) === myCh) return ['same switch' + (cur.sys_name ? ' (' + cur.sys_name + ')' : '') + ', port ', sg(p.port)];
    if (p.chassis) return ['on ', sg(stripKind(p.chassis)), p.port ? [' port ', sg(p.port)] : ''];
    return '';
  }
  let verdict = $derived.by(() => {
    if (!phase || phase === 'idle') return '';
    if (phase === 'asking') return 'requesting…';
    if (phase === 'watching') return 'other probe off 3 s · watching…';
    const v = pt.verdict;
    if (v === 'direct') return 'link dropped' + (pt.own_down_ms ? ' · down ' + sec(pt.own_down_ms) : '');
    if (v === 'switched') return 'link stayed up' + (pt.peer_back_ms ? ' · back after ' + sec(pt.peer_back_ms) : '');
    if (v === 'switched-peer-lost') return 'link stayed up · no answer since';
    if (v === 'no-answer') return 'no answer';
    if (v === 'inconclusive') return 'other probe stayed on';
    return '';
  });
  let vs = $derived(!phase || phase === 'idle' ? '' : busy ? ' · testing path' : pt.verdict === 'direct' ? ' · link dropped'
    : pt.verdict && pt.verdict.startsWith('switched') ? ' · link stayed up' : '');

  function testPath(e, mac) {
    e.stopPropagation(); e.currentTarget.blur();
    tool('path=1&peer=' + encodeURIComponent(mac));
  }
</script>

{#if !list.length && (!phase || phase === 'idle')}
  <ProtoCard key="peer" icon="probes" title="Other probes" state={w ? 'listen' : 'none'} summary={w || 'none heard'}/>
{:else}
  <ProtoCard key="peer" icon="probes" title="Other probes" state={list.length ? 'ok' : 'heard'} summary={list.length + ' probe' + (list.length === 1 ? '' : 's') + vs}>
    {#each list as p}
      <Kv k={isP(p.probe) ? 'Probe ' + p.probe + ' · ' + RES[p.probe] : 'Probe'} wide copy={p.mac}>
        <ProbeBadge n={p.probe}/><Parts p={sg(p.mac)}/>
        <Ln p={[where(p), p.pvid !== undefined ? 'PVID ' + p.pvid : '', p.speed ? p.speed + 'M ' + (p.full_duplex ? 'FD' : 'HD') : '',
          p.rtt_ms !== undefined ? (p.rtt_min_ms === 0 ? '<1' : p.rtt_min_ms) + ' ms' : '', 'heard ' + fmtAgo(p.age_s)]}/>
        {#if !busy}<div style="margin-top:8px"><button class="q" onclick={e => testPath(e, p.mac)}><Icon name="probes"/>Test path</button></div>{/if}
      </Kv>
    {/each}
    {#if ps && ps.other_macs !== undefined}
      <Kv k="Other devices" wide><span class="say">{ps.other_macs === 0 ? 'none heard' : ps.other_macs + (ps.other_macs_capped ? '+' : '') + ' other source MAC' + (ps.other_macs === 1 ? '' : 's') + ' heard'}</span></Kv>
    {/if}
    {#if verdict || (phase && phase !== 'idle')}
      <Kv k="Path test" wide><span class="say">{verdict}</span>{#if phase === 'recovering'}<Ln p={'waiting ' + sec(pt.elapsed_ms) + '…'}/>{/if}</Kv>
    {/if}
  </ProtoCard>
{/if}
