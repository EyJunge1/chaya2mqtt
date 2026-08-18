<script lang="ts">
  import InfoTip from "./InfoTip.svelte";

  let { text = "Port details" }: { text?: string } = $props();
</script>

<div>
  <button type="button">Outside</button>
  <InfoTip {text} />
</div>
