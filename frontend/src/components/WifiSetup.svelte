<script lang="ts">
  import { Lock, RefreshCw, Wifi } from "@lucide/svelte";
  import { untrack } from "svelte";
  import { api } from "../api/client.ts";
  import type { DeviceInfo, WifiIpMode, WifiScanAp, WifiStatus } from "../api/types.ts";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { router } from "../nav/router.svelte.ts";
  import { cn } from "../ui/cn.ts";
  import { EMPTY_STATE, HOVER_ROW, dash } from "../ui/styles.ts";
  import Field from "./Field.svelte";
  import GhostButton from "./GhostButton.svelte";
  import KeyValueGrid from "./KeyValueGrid.svelte";
  import Panel from "./Panel.svelte";
  import PrimaryButton from "./PrimaryButton.svelte";
  import SegmentedControl from "./SegmentedControl.svelte";
  import ServerChipList from "./ServerChipList.svelte";
  import StatusBadge from "./StatusBadge.svelte";
  import TextInput from "./TextInput.svelte";
  import type { ShowToast } from "./toastStack.ts";
  import { wifiSignalIcon } from "../ui/wifiSignal.ts";

  const IPV4_RE =
    /^(?:(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)$/;
  const DEFAULT_DNS1 = "1.1.1.1";
  const DEFAULT_DNS2 = "1.0.0.1";
  const DEFAULT_NTP = "time.cloudflare.com";
  const NTP_HOST_RE = /^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,61}[A-Za-z0-9])?$/;

  function isIpv4(value: string): boolean {
    return IPV4_RE.test(value);
  }

  function isNtpHost(value: string): boolean {
    return value.length > 0 && value.length < 64 && NTP_HOST_RE.test(value);
  }

  function pairSlots(values: string[]): [string, string] {
    return [values[0] ?? "", values[1] ?? ""];
  }

  function isAutomaticNtp(ntp1: string, ntp2: string): boolean {
    const a = ntp1.trim();
    const b = ntp2.trim();
    if (!a && !b) return true;
    if (a === DEFAULT_NTP && !b) return true;
    if (a === DEFAULT_NTP && b === "pool.ntp.org") return true;
    if (a === "pool.ntp.org" && b === DEFAULT_NTP) return true;
    return false;
  }

  let {
    device,
    wifi,
    onToast,
    showStatus = true,
  }: {
    device: DeviceInfo;
    wifi: WifiStatus;
    onToast: ShowToast;
    showStatus?: boolean;
  } = $props();

  let ssid = $state("");
  let password = $state("");
  let mode = $state<WifiIpMode>("dhcp");
  let ip = $state("");
  let gateway = $state("");
  let netmask = $state("255.255.255.0");
  let dnsServers = $state<string[]>([]);
  let ntpServers = $state<string[]>([]);
  let aps = $state<WifiScanAp[]>([]);
  let scanning = $state(false);
  let busy = $state(false);
  let configLoaded = $state(false);
  let scanSeq = 0;

  const SCAN_POLL_MS = 500;
  const SCAN_POLL_MAX_MS = 90_000;

  async function pollScan(seq: number): Promise<boolean> {
    const deadline = Date.now() + SCAN_POLL_MAX_MS;
    for (;;) {
      if (seq !== scanSeq) return false;
      const result = await api.scanWifi();
      if (seq !== scanSeq) return false;
      if (result.status === "ready") {
        aps = result.aps;
        return true;
      }
      if (result.status === "failed") {
        return false;
      }
      if (Date.now() >= deadline) {
        return false;
      }
      await new Promise((r) => setTimeout(r, SCAN_POLL_MS));
    }
  }

  async function startAndPoll(seq: number): Promise<void> {
    const started = await api.startWifiScan();
    if (seq !== scanSeq) return;
    if (!started.ok) {
      onToast(i18n.t("toast.wifi-scan-failed"), "error");
      return;
    }
    if (!(await pollScan(seq))) {
      if (seq !== scanSeq) return;
      onToast(i18n.t("toast.wifi-scan-failed"), "error");
    }
  }

  async function scan() {
    const seq = ++scanSeq;
    scanning = true;
    try {
      await startAndPoll(seq);
    } catch {
      if (seq !== scanSeq) return;
      onToast(i18n.t("toast.wifi-scan-failed"), "error");
    } finally {
      if (seq === scanSeq) scanning = false;
    }
  }

  async function hydrateScan() {
    const seq = ++scanSeq;
    scanning = true;
    try {
      const snap = await api.scanWifi();
      if (seq !== scanSeq) return;
      if (snap.status === "ready") {
        aps = snap.aps;
        return;
      }
      if (snap.status === "pending") {
        if (!(await pollScan(seq))) {
          if (seq !== scanSeq) return;
          onToast(i18n.t("toast.wifi-scan-failed"), "error");
        }
        return;
      }
      await startAndPoll(seq);
    } catch {
      if (seq !== scanSeq) return;
      onToast(i18n.t("toast.wifi-scan-failed"), "error");
    } finally {
      if (seq === scanSeq) scanning = false;
    }
  }

  $effect(() => {
    if (!ssid && wifi.connected) ssid = wifi.ssid;
  });

  $effect(() => {
    untrack(() => {
      void hydrateScan();
    });
    return () => {
      scanSeq += 1;
    };
  });

  $effect(() => {
    let cancelled = false;
    void (async () => {
      try {
        const cfg = await api.getWifiConfig();
        if (cancelled) return;
        if (cfg.ssid) ssid = cfg.ssid;
        mode = cfg.mode === "static" ? "static" : "dhcp";
        ip = cfg.ip || "";
        gateway = cfg.gateway || "";
        netmask = cfg.netmask || "255.255.255.0";
        dnsServers = [cfg.dns1, cfg.dns2].map((v) => v.trim()).filter(Boolean);
        const loadedNtp1 = (cfg.ntp1 || "").trim();
        const loadedNtp2 = (cfg.ntp2 || "").trim();
        ntpServers = isAutomaticNtp(loadedNtp1, loadedNtp2)
          ? []
          : [loadedNtp1, loadedNtp2].filter(Boolean);
      } catch {
        // Keep defaults when config is unavailable (e.g. first boot AP).
      } finally {
        if (!cancelled) configLoaded = true;
      }
    })();
    return () => {
      cancelled = true;
    };
  });

  function dnsFieldsValid(): boolean {
    return dnsServers.every(isIpv4);
  }

  function ntpFieldsValid(): boolean {
    return ntpServers.every(isNtpHost);
  }

  function staticFieldsValid(): boolean {
    if (mode !== "static") return true;
    return isIpv4(ip) && isIpv4(gateway) && isIpv4(netmask);
  }

  async function connect(e: SubmitEvent) {
    e.preventDefault();
    if (!staticFieldsValid() || !dnsFieldsValid() || !ntpFieldsValid()) {
      onToast(i18n.t("toast.wifi-connect-failed"), "error");
      return;
    }
    const [dns1, dns2] = pairSlots(dnsServers);
    const [ntp1, ntp2] = pairSlots(ntpServers);
    busy = true;
    try {
      const res = await api.connectWifi({
        ssid,
        password,
        mode,
        ip: mode === "static" ? ip : undefined,
        gateway: mode === "static" ? gateway : undefined,
        netmask: mode === "static" ? netmask : undefined,
        dns1,
        dns2,
        ntp1,
        ntp2,
      });
      if (!res.ok) {
        onToast(i18n.t("toast.wifi-connect-failed"), "error");
        return;
      }
      if (res.next === "/wifi-testing" || device.mode === "ap") {
        router.navigate("/wifi-testing");
        return;
      }
      onToast(
        res.message === "saved_rebooting"
          ? i18n.t("toast.wifi-saved-reboot")
          : i18n.t("toast.saved"),
        "success",
      );
    } catch {
      onToast(i18n.t("toast.wifi-connect-failed"), "error");
    } finally {
      busy = false;
    }
  }

  const staticRequired = $derived(mode === "static");
  const WifiStatusIcon = $derived(wifiSignalIcon(wifi));
  const activeNtp = $derived(ntpServers[0] || DEFAULT_NTP);
  const dnsPreview = $derived(
    wifi.connected
      ? [wifi.dns1, wifi.dns2].map((v) => (v || "").trim()).filter(Boolean)
      : [DEFAULT_DNS1, DEFAULT_DNS2],
  );
  const ntpPreview = [DEFAULT_NTP];
</script>

<div class="space-y-4">
  {#if showStatus}
    <Panel>
      {#snippet title()}
        <StatusBadge
          ok={wifi.connected}
          icon={WifiStatusIcon}
          label={i18n.t("wifi.status")}
          detailOk={i18n.t("status.wifi-ok")}
          detailBad={i18n.t("status.wifi-bad")}
        />
      {/snippet}
      <KeyValueGrid
        items={[
          { label: i18n.t("wifi.ssid"), value: wifi.connected ? dash(wifi.ssid) : "-" },
          {
            label: i18n.t("wifi.signal"),
            value: wifi.connected ? `${wifi.rssi} dBm` : "-",
          },
          { label: i18n.t("wifi.ip"), value: wifi.connected ? dash(wifi.ip) : "-" },
          { label: i18n.t("wifi.netmask"), value: wifi.connected ? dash(wifi.netmask) : "-" },
          { label: i18n.t("wifi.gateway"), value: wifi.connected ? dash(wifi.gateway) : "-" },
          { label: i18n.t("wifi.ntp"), value: wifi.connected ? activeNtp : "-" },
          { label: i18n.t("wifi.dns1"), value: wifi.connected ? dash(wifi.dns1) : "-" },
          { label: i18n.t("wifi.dns2"), value: wifi.connected ? dash(wifi.dns2) : "-" },
        ]}
      />
    </Panel>
  {/if}

  <Panel title={i18n.t("wifi.networks")}>
    {#snippet action()}
      <GhostButton type="button" onclick={() => void scan()} disabled={scanning}>
        <RefreshCw size={14} class={scanning ? "animate-spin" : ""} aria-hidden="true" />
        {i18n.t("wifi.scan")}
      </GhostButton>
    {/snippet}
    <div class="space-y-2">
      {#if aps.length === 0}
        <p class={EMPTY_STATE}>{scanning ? i18n.t("wifi.searching") : i18n.t("wifi.none")}</p>
      {:else}
        {#each aps as ap, index (`${ap.ssid}-${ap.rssi}-${index}`)}
          <button
            type="button"
            onclick={() => (ssid = ap.ssid)}
            class={cn(
              "flex w-full items-center justify-between rounded-lg border border-border bg-bg px-3 py-2.5 text-left transition focus-ring",
              HOVER_ROW,
            )}
          >
            <span class="inline-flex items-center gap-2 text-sm text-text-bright">
              <Wifi size={16} class="text-accent" aria-hidden="true" />
              {ap.ssid || i18n.t("wifi.hidden")}
              {#if !ap.open}
                <Lock size={12} class="text-muted" aria-hidden="true" />
              {/if}
            </span>
            <span class="text-xs text-muted">{ap.rssi} dBm</span>
          </button>
        {/each}
      {/if}
    </div>
  </Panel>

  <Panel>
    <form class="space-y-3" onsubmit={(e) => void connect(e)}>
      <Field label={i18n.t("wifi.ssid")} hint={i18n.t("wifi.ssid-hint")}>
        <TextInput bind:value={ssid} required maxlength={32} />
      </Field>
      <Field label={i18n.t("wifi.password")} hint={i18n.t("wifi.password-hint")}>
        <TextInput
          type="password"
          bind:value={password}
          maxlength={63}
          autocomplete="current-password"
        />
      </Field>

      <fieldset class="space-y-3">
        <legend class="mb-1.5 text-sm font-semibold text-text-bright">
          {i18n.t("wifi.ip-settings")}
        </legend>
        <SegmentedControl
          label={i18n.t("wifi.ip-settings")}
          value={mode}
          onChange={(next) => (mode = next)}
          options={[
            { value: "dhcp", label: i18n.t("wifi.mode-dhcp"), testId: "wifi-mode-dhcp" },
            { value: "static", label: i18n.t("wifi.mode-manual"), testId: "wifi-mode-static" },
          ]}
        />
        {#if mode === "static"}
          <div
            class="grid grid-cols-1 gap-3 rounded-xl border border-border bg-surface p-3 sm:grid-cols-2"
          >
            <Field label={i18n.t("wifi.ip")} hint={i18n.t("wifi.ip-hint")}>
              <TextInput
                bind:value={ip}
                required={staticRequired}
                inputmode="decimal"
                placeholder="192.168.1.50"
                data-testid="wifi-ip"
              />
            </Field>
            <Field label={i18n.t("wifi.netmask")} hint={i18n.t("wifi.netmask-hint")}>
              <TextInput
                bind:value={netmask}
                required={staticRequired}
                inputmode="decimal"
                placeholder="255.255.255.0"
                data-testid="wifi-netmask"
              />
            </Field>
            <Field label={i18n.t("wifi.gateway")} hint={i18n.t("wifi.gateway-hint")}>
              <TextInput
                bind:value={gateway}
                required={staticRequired}
                inputmode="decimal"
                placeholder="192.168.1.1"
                data-testid="wifi-gateway"
              />
            </Field>
          </div>
        {/if}
      </fieldset>

      <ServerChipList
        label={i18n.t("wifi.dns")}
        values={dnsServers}
        onChange={(next) => (dnsServers = next)}
        placeholder={DEFAULT_DNS1}
        validate={isIpv4}
        hint={i18n.t("wifi.servers-auto-dns")}
        previewValues={dnsPreview}
        addLabel={i18n.t("wifi.add-server")}
        removeLabel={i18n.t("wifi.remove-server")}
        testIdPrefix="wifi-dns"
        inputMode="decimal"
      />

      <ServerChipList
        label={i18n.t("wifi.ntp")}
        values={ntpServers}
        onChange={(next) => (ntpServers = next)}
        placeholder={DEFAULT_NTP}
        validate={isNtpHost}
        hint={i18n.t("wifi.servers-auto-ntp")}
        previewValues={ntpPreview}
        addLabel={i18n.t("wifi.add-server")}
        removeLabel={i18n.t("wifi.remove-server")}
        testIdPrefix="wifi-ntp"
        maxLength={63}
      />

      <PrimaryButton
        type="submit"
        loading={busy}
        disabled={!ssid ||
          !configLoaded ||
          !staticFieldsValid() ||
          !dnsFieldsValid() ||
          !ntpFieldsValid()}
      >
        {device.mode === "ap" ? i18n.t("wifi.test-connect") : i18n.t("wifi.save-reboot")}
      </PrimaryButton>
    </form>
  </Panel>
</div>
