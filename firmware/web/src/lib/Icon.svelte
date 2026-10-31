<script>
  let { name, size = '' } = $props();
</script>

<svg class="ic{size ? ' ' + size : ''}" aria-hidden="true"><use href="#i-{name}"/></svg>
