<script lang="ts">
  import { api } from "../api/client.ts";
  import type { WifiConnectStatus } from "../api/types.ts";
  import Alert from "../components/Alert.svelte";
  import DangerButton from "../components/DangerButton.svelte";
  import Panel from "../components/Panel.svelte";
  import PrimaryButton from "../components/PrimaryButton.svelte";
  import Spinner from "../components/Spinner.svelte";
  import type { ShowToast } from "../components/toastStack.ts";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { router } from "../nav/router.svelte.ts";

  let { onToast }: { onToast: ShowToast } = $props();

  let status = $state<WifiConnectStatus>({ state: "testing", ssid: "" });
  let busy = $state(false);

  $effect(() => {
    let alive = true;
    const tick = async () => {
      try {
        const s = await api.getWifiConnectStatus();
        if (alive) status = s;
      } catch {
        /* keep polling */
      }
    };
    void tick();
    const id = window.setInterval(() => void tick(), 700);
    return () => {
      alive = false;
      window.clearInterval(id);
    };
  });

  async function commit() {
    busy = true;
    try {
      const res = await api.commitWifiConnect();
      if (!res.ok) {
        onToast(i18n.t("toast.wifi-commit-failed"), "error");
        return;
      }
      onToast(i18n.t("toast.wifi-committed"), "success");
      if (res.next) {
        const next = res.next;
        window.setTimeout(() => window.location.replace(next), 2000);
      }
    } catch {
      onToast(i18n.t("toast.wifi-commit-failed"), "error");
    } finally {
      busy = false;
    }
  }

  async function abort() {
    busy = true;
    try {
      await api.abortWifiConnect();
      router.replace("/");
    } catch {
      onToast(i18n.t("toast.wifi-abort-failed"), "error");
    } finally {
      busy = false;
    }
  }

  const statusText = $derived(
    status.state === "testing"
      ? i18n.t("wifi-test.testing")
      : status.state === "ok"
        ? i18n.t("wifi-test.ok")
        : status.state === "fail"
          ? i18n.t("wifi-test.fail")
          : i18n.t("wifi-test.idle"),
  );
</script>

<div class="space-y-4">
  <Panel
    title={i18n.t("wifi-test.title")}
    hint={status.state !== "ok" ? i18n.t("wifi-test.commit-hint") : undefined}
  >
    <p class="mb-2 text-sm text-muted">
      {i18n.t("wifi-test.ssid")} <span class="text-text-bright">{status.ssid || "…"}</span>
    </p>
    <p
      role="status"
      aria-busy={status.state === "testing" || undefined}
      class="inline-flex items-center gap-2 text-sm text-text-bright"
    >
      {#if status.state === "testing"}
        <Spinner size={16} />
      {/if}
      {statusText}
    </p>
  </Panel>
  {#if status.state === "fail"}
    <Alert variant="error">{i18n.t("wifi-test.fail")}</Alert>
  {/if}
  <div class="space-y-3">
    <PrimaryButton loading={busy} disabled={status.state !== "ok"} onclick={() => void commit()}>
      {i18n.t("wifi-test.commit")}
    </PrimaryButton>
    <DangerButton loading={busy} onclick={() => void abort()}>
      {i18n.t("wifi-test.abort")}
    </DangerButton>
  </div>
</div>
