<script lang="ts">
  import { device } from "./device.svelte.ts";
  import DeviceRoot from "./DeviceRoot.svelte";

  let { showChrome = false }: { showChrome?: boolean } = $props();
</script>

<DeviceRoot>
  {#snippet chrome()}
    {#if showChrome}
      <div data-testid="chrome">simulator</div>
    {/if}
  {/snippet}
  <div>
    <span data-testid="device-id">{device.device?.deviceId}</span>
    <span data-testid="live">{device.live}</span>
    <span data-testid="rx">{device.chaya.rx}</span>
  </div>
</DeviceRoot>
