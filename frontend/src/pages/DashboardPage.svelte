<script lang="ts">
  import {
    Battery,
    BatteryFull,
    BatteryLow,
    BatteryMedium,
    BatteryWarning,
    Heart,
    Radio,
    Wifi,
  } from "@lucide/svelte";
  import { api } from "../api/client.ts";
  import { otaHasPendingUpdate } from "../api/ota.ts";
  import type { ChayaStatus, DeviceInfo, OtaStatus, WifiStatus } from "../api/types.ts";
  import Alert from "../components/Alert.svelte";
  import Badge from "../components/Badge.svelte";
  import LinkButton from "../components/LinkButton.svelte";
  import Panel from "../components/Panel.svelte";
  import PrimaryButton from "../components/PrimaryButton.svelte";
  import StatusBadge from "../components/StatusBadge.svelte";
  import type { ShowToast } from "../components/toastStack.ts";
  import WifiSetup from "../components/WifiSetup.svelte";
  import { i18n } from "../i18n/i18n.svelte.ts";

  let {
    device,
    chaya,
    wifi,
    ota = null,
    onToast,
  }: {
    device: DeviceInfo;
    chaya: ChayaStatus;
    wifi: WifiStatus;
    ota?: OtaStatus | null;
    onToast: ShowToast;
  } = $props();

  let busy = $state(false);

  const batteryPct = $derived(Math.max(0, Math.min(100, device.batteryPct)));
  const BatteryIcon = $derived(
    batteryPct >= 75
      ? BatteryFull
      : batteryPct >= 45
        ? BatteryMedium
        : batteryPct >= 25
          ? BatteryWarning
          : batteryPct >= 10
            ? BatteryLow
            : Battery,
  );
  // Green → orange → red (skip pure yellow).
  const batteryColor = $derived.by(() => {
    if (batteryPct >= 50) {
      const t = (100 - batteryPct) * 2; // 0% @ full → 100% @ half
      return `color-mix(in oklch, #f59e0b ${t}%, #22c55e)`;
    }
    const t = (50 - batteryPct) * 2; // 0% @ half → 100% @ empty
    return `color-mix(in oklch, #e53935 ${t}%, #f59e0b)`;
  });

  async function sendHeart() {
    busy = true;
    try {
      const res = await api.sendChaya();
      if (!res.ok) {
        onToast(i18n.t("toast.heart-offline"), "error");
      } else {
        onToast(i18n.t("toast.heart-sent"), "success");
      }
    } catch {
      onToast(i18n.t("toast.heart-failed"), "error");
    } finally {
      busy = false;
    }
  }
</script>

{#if device.mode === "ap"}
  <WifiSetup {device} {wifi} {onToast} showStatus={false} />
{:else}
  <div class="space-y-4">
    {#if otaHasPendingUpdate(ota) && ota}
      <Alert variant="warning" title={i18n.t("dashboard.update-available-title")}>
        <div class="space-y-2">
          <p>{i18n.t("dashboard.update-available-text", { version: ota.availableVersion })}</p>
          <LinkButton href="/update" variant="warning">
            {i18n.t("dashboard.update-available-action")}
          </LinkButton>
        </div>
      </Alert>
    {/if}

    <div class="flex flex-wrap gap-2">
      <StatusBadge
        href="/wifi"
        ok={wifi.connected}
        icon={Wifi}
        label={i18n.t("status.wifi")}
        detailOk={i18n.t("status.wifi-ok")}
        detailBad={i18n.t("status.wifi-bad")}
      />
      <StatusBadge
        href="/mqtt"
        ok={chaya.connected}
        icon={Radio}
        label={i18n.t("status.mqtt")}
        detailOk={i18n.t("status.mqtt-ok")}
        detailBad={i18n.t("status.mqtt-bad")}
      />
      <Badge
        tone="neutral"
        aria-label={`${i18n.t("dashboard.battery")}: ${batteryPct}%`}
        title={`${i18n.t("dashboard.battery")}: ${batteryPct}%`}
      >
        <span style:color={batteryColor} class="inline-flex">
          <BatteryIcon size={18} aria-hidden="true" />
        </span>
        {batteryPct}%
      </Badge>
    </div>

    <Panel title={i18n.t("dashboard.hearts")} hint={i18n.t("dashboard.hearts-hint")}>
      <div class="mb-4 grid grid-cols-2 gap-3">
        <div class="rounded-xl bg-bg px-3 py-4 text-center">
          <div
            class="inline-flex items-center justify-center gap-1 text-xs uppercase tracking-wide text-muted"
          >
            <Heart size={12} fill="currentColor" class="text-accent" aria-hidden="true" />
            {i18n.t("dashboard.rx")}
          </div>
          <div class="mt-1 text-3xl font-bold text-text-bright">{chaya.rx}</div>
        </div>
        <div class="rounded-xl bg-bg px-3 py-4 text-center">
          <div
            class="inline-flex items-center justify-center gap-1 text-xs uppercase tracking-wide text-muted"
          >
            <Heart size={12} fill="currentColor" class="text-accent" aria-hidden="true" />
            {i18n.t("dashboard.tx")}
          </div>
          <div class="mt-1 text-3xl font-bold text-text-bright">{chaya.tx}</div>
        </div>
      </div>
      <PrimaryButton onclick={sendHeart} loading={busy} disabled={!chaya.connected}>
        <Heart size={18} fill="currentColor" />
        {i18n.t("dashboard.send-heart")}
      </PrimaryButton>
    </Panel>
  </div>
{/if}
