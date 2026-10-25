<script lang="ts">
  import type { DeviceMode } from "../api/types.ts";
  import type { LiveState } from "../state/device.svelte.ts";
  import Layout from "./Layout.svelte";

  let {
    mode = "sta",
    live = "live",
  }: {
    mode?: DeviceMode;
    live?: LiveState;
  } = $props();
</script>

<Layout {mode} {live}>
  <div>content</div>
</Layout>
