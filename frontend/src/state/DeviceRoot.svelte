<script lang="ts">
  import { onMount, type Snippet } from "svelte";
  import { connectEvents } from "../api/sse.ts";
  import ErrorBlock from "../components/ErrorBlock.svelte";
  import LoadingBlock from "../components/LoadingBlock.svelte";
  import Toast from "../components/Toast.svelte";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { device } from "./device.svelte.ts";

  let {
    children,
    chrome,
  }: {
    children: Snippet;
    chrome?: Snippet;
  } = $props();

  onMount(() => {
    void device.boot();
  });

  $effect(() => {
    const key = device.sseKey;
    if (!key) return;
    device.live = "connecting";
    return connectEvents({
      chaya: (data) => {
        device.live = "live";
        device.chaya = data;
      },
      wifi: (data) => {
        device.live = "live";
        device.wifi = data;
      },
      mqtt: (data) => {
        device.live = "live";
        device.mqtt = data;
      },
      ota: (data) => {
        device.live = "live";
        device.ota = data;
      },
      error: () => {
        device.live = "reconnecting";
      },
    });
  });
</script>

{@render chrome?.()}
{#if device.bootError}
  <div class="mx-auto max-w-140 px-4 py-10">
    <h1 class="mb-4 text-center text-xl font-bold text-text-bright">{i18n.t("app.title")}</h1>
    <ErrorBlock
      title={i18n.t("app.boot-error-title")}
      message={i18n.t("app.boot-error")}
      retryLabel={i18n.t("common.retry")}
      onRetry={() => void device.boot()}
    />
  </div>
{:else if device.booting || !device.device}
  <div class="mx-auto max-w-140 px-4 py-10">
    <LoadingBlock label={i18n.t("app.connecting")} />
  </div>
{:else}
  {@render children()}
  <Toast toasts={device.toasts} onDismiss={device.dismissToast} />
{/if}
