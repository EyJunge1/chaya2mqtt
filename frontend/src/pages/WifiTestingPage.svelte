<script lang="ts">
  import { api } from "../api/client.ts";
  import type { WifiConnectStatus } from "../api/types.ts";
  import Panel from "../components/Panel.svelte";
  import PrimaryButton from "../components/PrimaryButton.svelte";
  import Spinner from "../components/Spinner.svelte";
  import type { ShowToast } from "../components/toastStack.ts";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { router } from "../nav/router.svelte.ts";

  let { onToast }: { onToast: ShowToast } = $props();

  let status = $state<WifiConnectStatus>({ state: "testing", ssid: "" });
  let busy = $state(false);
  let prevState = $state<WifiConnectStatus["state"] | null>(null);
  let redirectTimerId: ReturnType<typeof setTimeout> | undefined;

  const IPV4_HOST = /^(?:(?:25[0-5]|2[0-4]\d|[01]?\d{1,2})\.){3}(?:25[0-5]|2[0-4]\d|[01]?\d{1,2})$/;

  /** Private + link-local IPv4 only (RFC1918 / 169.254.0.0/16). */
  function isPrivateOrLinkLocalIpv4(host: string): boolean {
    const m = IPV4_HOST.exec(host);
    if (!m) return false;
    const parts = host.split(".").map((p) => Number(p));
    const [a, b] = parts;
    if (a === 10) return true;
    if (a === 172 && b >= 16 && b <= 31) return true;
    if (a === 192 && b === 168) return true;
    if (a === 169 && b === 254) return true;
    return false;
  }

  function isAllowedRedirectNext(next: string): boolean {
    if (next.startsWith("/") && !next.startsWith("//")) return true;
    try {
      const url = new URL(next);
      if (url.protocol !== "http:") return false;
      return isPrivateOrLinkLocalIpv4(url.hostname);
    } catch {
      return false;
    }
  }

  $effect(() => {
    let alive = true;
    const tick = async () => {
      try {
        const s = await api.getWifiConnectStatus();
        if (!alive) return;
        if (s.state === "fail" && prevState !== "fail") {
          onToast(i18n.t("toast.wifi-connect-failed"), "error");
        } else if (s.state === "ok" && prevState !== "ok") {
          onToast(i18n.t("toast.wifi-connect-ok"), "success");
        }
        prevState = s.state;
        status = s;
      } catch {
        /* keep polling */
      }
    };
    void tick();
    const id = window.setInterval(() => void tick(), 700);
    return () => {
      alive = false;
      window.clearInterval(id);
      if (redirectTimerId !== undefined) {
        window.clearTimeout(redirectTimerId);
        redirectTimerId = undefined;
      }
      // Abort only when leaving the testing route. Transient remounts (e.g. simulator
      // boot/refresh) keep pathname `/wifi-testing` and must not wipe connect state.
      if (router.pathname !== "/wifi-testing") {
        void api.abortWifiConnect().catch(() => {});
      }
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
      const target =
        typeof res.next === "string" && isAllowedRedirectNext(res.next) ? res.next : "/";
      if (redirectTimerId !== undefined) window.clearTimeout(redirectTimerId);
      redirectTimerId = window.setTimeout(() => window.location.replace(target), 2000);
    } catch {
      onToast(i18n.t("toast.wifi-commit-failed"), "error");
    } finally {
      busy = false;
    }
  }

  async function retry() {
    busy = true;
    try {
      const res = await api.retryWifiConnect();
      if (!res.ok) {
        onToast(i18n.t("toast.wifi-retry-failed"), "error");
        return;
      }
      prevState = "testing";
      status = { state: "testing", ssid: status.ssid };
    } catch {
      onToast(i18n.t("toast.wifi-retry-failed"), "error");
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
  <Panel title={i18n.t("wifi-test.title")}>
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
    <PrimaryButton loading={busy} onclick={() => void retry()}>
      {i18n.t("common.retry")}
    </PrimaryButton>
  {:else if status.state === "ok"}
    <PrimaryButton loading={busy} onclick={() => void commit()}>
      {i18n.t("wifi-test.commit")}
    </PrimaryButton>
  {/if}
</div>
