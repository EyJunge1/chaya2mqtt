<script lang="ts">
  import type { DeviceInfo, WifiStatus } from "../api/types.ts";
  import type { ShowToast } from "../components/toastStack.ts";
  import WifiSetup from "../components/WifiSetup.svelte";

  let {
    device,
    wifi,
    onToast,
  }: {
    device: DeviceInfo;
    wifi: WifiStatus;
    onToast: ShowToast;
  } = $props();
</script>

<WifiSetup {device} {wifi} {onToast} showStatus />
