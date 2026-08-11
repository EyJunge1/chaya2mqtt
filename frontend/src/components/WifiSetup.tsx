import { Lock, RefreshCw, Wifi } from "lucide-react";
import { useCallback, useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";
import { api } from "../api/client";
import type { DeviceInfo, WifiIpMode, WifiScanAp, WifiStatus } from "../api/types";
import { useI18n } from "../i18n/useI18n";
import { cn } from "../ui/cn";
import { EMPTY_STATE, HOVER_ROW, dash } from "../ui/styles";
import { Panel } from "./Card";
import { Field, GhostButton, PrimaryButton, TextInput } from "./Form";
import { KeyValueGrid } from "./KeyValueGrid";
import { SegmentedControl } from "./SegmentedControl";
import { ServerChipList } from "./ServerChipList";
import { StatusBadge } from "./StatusBadge";
import type { ShowToast } from "./Toast";

const IPV4_RE = /^(?:(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)$/;
/** Built-in DNS when no DHCP DNS is shown yet / placeholders. */
const DEFAULT_DNS1 = "1.1.1.1";
const DEFAULT_DNS2 = "1.0.0.1";
/** Built-in NTP when DHCP does not offer NTP (option 42). */
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

/** Empty or known built-in values → automatic (DHCP NTP, else Cloudflare). */
function isAutomaticNtp(ntp1: string, ntp2: string): boolean {
  const a = ntp1.trim();
  const b = ntp2.trim();
  if (!a && !b) return true;
  if (a === DEFAULT_NTP && !b) return true;
  // Legacy defaults from older firmware builds.
  if (a === DEFAULT_NTP && b === "pool.ntp.org") return true;
  if (a === "pool.ntp.org" && b === DEFAULT_NTP) return true;
  return false;
}

export function WifiSetup({
  device,
  wifi,
  onToast,
  showStatus = true,
}: {
  device: DeviceInfo;
  wifi: WifiStatus;
  onToast: ShowToast;
  showStatus?: boolean;
}) {
  const { t } = useI18n();
  const navigate = useNavigate();
  const [ssid, setSsid] = useState(wifi.connected ? wifi.ssid : "");
  const [password, setPassword] = useState("");
  const [mode, setMode] = useState<WifiIpMode>("dhcp");
  const [ip, setIp] = useState("");
  const [gateway, setGateway] = useState("");
  const [netmask, setNetmask] = useState("255.255.255.0");
  const [dnsServers, setDnsServers] = useState<string[]>([]);
  const [ntpServers, setNtpServers] = useState<string[]>([]);
  const [aps, setAps] = useState<WifiScanAp[]>([]);
  const [scanning, setScanning] = useState(false);
  const [busy, setBusy] = useState(false);
  const [configLoaded, setConfigLoaded] = useState(false);

  const scan = useCallback(async () => {
    setScanning(true);
    try {
      for (let i = 0; i < 8; i++) {
        const result = await api.scanWifi();
        if (result !== "pending") {
          setAps(result);
          break;
        }
        await new Promise((r) => setTimeout(r, 500));
      }
    } catch {
      onToast(t("toast.wifi-scan-failed"), "error");
    } finally {
      setScanning(false);
    }
  }, [onToast, t]);

  useEffect(() => {
    void scan();
  }, [scan]);

  useEffect(() => {
    let cancelled = false;
    void (async () => {
      try {
        const cfg = await api.getWifiConfig();
        if (cancelled) return;
        if (cfg.ssid) setSsid(cfg.ssid);
        setMode(cfg.mode === "static" ? "static" : "dhcp");
        setIp(cfg.ip || "");
        setGateway(cfg.gateway || "");
        setNetmask(cfg.netmask || "255.255.255.0");
        setDnsServers([cfg.dns1, cfg.dns2].map((v) => v.trim()).filter(Boolean));
        const loadedNtp1 = (cfg.ntp1 || "").trim();
        const loadedNtp2 = (cfg.ntp2 || "").trim();
        setNtpServers(
          isAutomaticNtp(loadedNtp1, loadedNtp2) ? [] : [loadedNtp1, loadedNtp2].filter(Boolean),
        );
      } catch {
        // Keep defaults when config is unavailable (e.g. first boot AP).
      } finally {
        if (!cancelled) setConfigLoaded(true);
      }
    })();
    return () => {
      cancelled = true;
    };
  }, []);

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

  async function connect(e: React.FormEvent) {
    e.preventDefault();
    if (!staticFieldsValid() || !dnsFieldsValid() || !ntpFieldsValid()) {
      onToast(t("toast.wifi-connect-failed"), "error");
      return;
    }
    const [dns1, dns2] = pairSlots(dnsServers);
    // Empty NTP = device uses DHCP option 42, else built-in Cloudflare/pool.
    const [ntp1, ntp2] = pairSlots(ntpServers);
    setBusy(true);
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
        onToast(t("toast.wifi-connect-failed"), "error");
        return;
      }
      if (res.next === "/wifi-testing" || device.mode === "ap") {
        navigate("/wifi-testing");
        return;
      }
      onToast(
        res.message === "saved_rebooting" ? t("toast.wifi-saved-reboot") : t("toast.saved"),
        "success",
      );
    } catch {
      onToast(t("toast.wifi-connect-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  const staticRequired = mode === "static";
  const activeNtp = ntpServers[0] || DEFAULT_NTP;
  const dnsPreview = wifi.connected
    ? [wifi.dns1, wifi.dns2].map((v) => (v || "").trim()).filter(Boolean)
    : [DEFAULT_DNS1, DEFAULT_DNS2];
  const ntpPreview = [DEFAULT_NTP];

  return (
    <div className="space-y-4">
      {showStatus ? (
        <Panel
          title={
            <StatusBadge
              ok={wifi.connected}
              label={t("wifi.status")}
              detailOk={t("status.wifi-ok")}
              detailBad={t("status.wifi-bad")}
            />
          }
        >
          <KeyValueGrid
            items={[
              { label: t("wifi.ssid"), value: wifi.connected ? dash(wifi.ssid) : "-" },
              {
                label: t("wifi.signal"),
                value: wifi.connected ? `${wifi.rssi} dBm` : "-",
              },
              { label: t("wifi.ip"), value: wifi.connected ? dash(wifi.ip) : "-" },
              { label: t("wifi.netmask"), value: wifi.connected ? dash(wifi.netmask) : "-" },
              { label: t("wifi.gateway"), value: wifi.connected ? dash(wifi.gateway) : "-" },
              { label: t("wifi.ntp"), value: wifi.connected ? activeNtp : "-" },
              { label: t("wifi.dns1"), value: wifi.connected ? dash(wifi.dns1) : "-" },
              { label: t("wifi.dns2"), value: wifi.connected ? dash(wifi.dns2) : "-" },
            ]}
          />
        </Panel>
      ) : null}

      <Panel
        title={t("wifi.networks")}
        action={
          <GhostButton type="button" onClick={() => void scan()} disabled={scanning}>
            <RefreshCw size={14} className={scanning ? "animate-spin" : ""} aria-hidden />
            {t("wifi.scan")}
          </GhostButton>
        }
      >
        <div className="space-y-2">
          {aps.length === 0 ? (
            <p className={EMPTY_STATE}>{scanning ? t("wifi.searching") : t("wifi.none")}</p>
          ) : (
            aps.map((ap) => (
              <button
                key={`${ap.ssid}-${ap.rssi}`}
                type="button"
                onClick={() => setSsid(ap.ssid)}
                className={cn(
                  "flex w-full items-center justify-between rounded-lg border border-border bg-bg px-3 py-2.5 text-left transition focus-ring",
                  HOVER_ROW,
                )}
              >
                <span className="inline-flex items-center gap-2 text-sm text-text-bright">
                  <Wifi size={16} className="text-accent" aria-hidden />
                  {ap.ssid || t("wifi.hidden")}
                  {!ap.open ? <Lock size={12} className="text-muted" aria-hidden /> : null}
                </span>
                <span className="text-xs text-muted">{ap.rssi} dBm</span>
              </button>
            ))
          )}
        </div>
      </Panel>

      <Panel>
        <form className="space-y-3" onSubmit={(e) => void connect(e)}>
          <Field label={t("wifi.ssid")} hint={t("wifi.ssid-hint")}>
            <TextInput
              value={ssid}
              onChange={(e) => setSsid(e.target.value)}
              required
              maxLength={32}
            />
          </Field>
          <Field label={t("wifi.password")} hint={t("wifi.password-hint")}>
            <TextInput
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              maxLength={64}
              autoComplete="current-password"
            />
          </Field>

          <fieldset className="space-y-3">
            <legend className="mb-1.5 text-sm font-semibold text-text-bright">
              {t("wifi.ip-settings")}
            </legend>
            <SegmentedControl
              label={t("wifi.ip-settings")}
              value={mode}
              onChange={setMode}
              options={[
                { value: "dhcp", label: t("wifi.mode-dhcp"), testId: "wifi-mode-dhcp" },
                { value: "static", label: t("wifi.mode-manual"), testId: "wifi-mode-static" },
              ]}
            />
            {mode === "static" ? (
              <div
                className={cn(
                  "grid grid-cols-1 gap-3 rounded-xl border border-border bg-surface p-3 sm:grid-cols-2",
                )}
              >
                <Field label={t("wifi.ip")} hint={t("wifi.ip-hint")}>
                  <TextInput
                    value={ip}
                    onChange={(e) => setIp(e.target.value)}
                    required={staticRequired}
                    inputMode="decimal"
                    placeholder="192.168.1.50"
                    data-testid="wifi-ip"
                  />
                </Field>
                <Field label={t("wifi.netmask")} hint={t("wifi.netmask-hint")}>
                  <TextInput
                    value={netmask}
                    onChange={(e) => setNetmask(e.target.value)}
                    required={staticRequired}
                    inputMode="decimal"
                    placeholder="255.255.255.0"
                    data-testid="wifi-netmask"
                  />
                </Field>
                <Field label={t("wifi.gateway")} hint={t("wifi.gateway-hint")}>
                  <TextInput
                    value={gateway}
                    onChange={(e) => setGateway(e.target.value)}
                    required={staticRequired}
                    inputMode="decimal"
                    placeholder="192.168.1.1"
                    data-testid="wifi-gateway"
                  />
                </Field>
              </div>
            ) : null}
          </fieldset>

          <ServerChipList
            label={t("wifi.dns")}
            values={dnsServers}
            onChange={setDnsServers}
            placeholder={DEFAULT_DNS1}
            validate={isIpv4}
            hint={t("wifi.servers-auto-dns")}
            previewValues={dnsPreview}
            addLabel={t("wifi.add-server")}
            removeLabel={t("wifi.remove-server")}
            testIdPrefix="wifi-dns"
            inputMode="decimal"
          />

          <ServerChipList
            label={t("wifi.ntp")}
            values={ntpServers}
            onChange={setNtpServers}
            placeholder={DEFAULT_NTP}
            validate={isNtpHost}
            hint={t("wifi.servers-auto-ntp")}
            previewValues={ntpPreview}
            addLabel={t("wifi.add-server")}
            removeLabel={t("wifi.remove-server")}
            testIdPrefix="wifi-ntp"
            maxLength={63}
          />

          <PrimaryButton
            type="submit"
            loading={busy}
            disabled={
              !ssid ||
              !configLoaded ||
              !staticFieldsValid() ||
              !dnsFieldsValid() ||
              !ntpFieldsValid()
            }
          >
            {device.mode === "ap" ? t("wifi.test-connect") : t("wifi.save-reboot")}
          </PrimaryButton>
        </form>
      </Panel>
    </div>
  );
}
