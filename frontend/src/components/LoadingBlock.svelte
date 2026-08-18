<script lang="ts">
  import Spinner from "./Spinner.svelte";

  let { label }: { label: string } = $props();
</script>

<div
  role="status"
  aria-busy="true"
  class="flex items-center justify-center gap-2 rounded-xl border border-border bg-surface px-4 py-8 text-sm text-muted"
>
  <Spinner size={18} />
  {label}
</div>
