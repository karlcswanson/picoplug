<script>
  // The logo mark; chevrons follow the text color. Optional dot pulse, its phase taken
  // from the clock so it carries on smoothly when one card replaces another.
  let { pulse = false } = $props();
  const t0 = -(performance.now() % 1500) / 1000;
  const dots = [['8', '#2fd46f'], ['12', '#ffcf00'], ['16', '#ff4d4d']];
</script>

<svg class="mark" class:pulse viewBox="0 0 24 24" aria-hidden="true">
  <g fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="1.75">
    <path d="M6.25 8.5 2.75 12l3.5 3.5"/><path d="M17.75 8.5 21.25 12l-3.5 3.5"/>
  </g>
  {#each dots as [cx, fill], i}
    <circle {cx} cy="12" r="1.3" {fill} style:animation-delay="{(t0 + i * 0.2).toFixed(3)}s"/>
  {/each}
</svg>
