import { Lock, RefreshCw, Wifi } from "lucide-react";
import { useCallback, useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";
import { api } from "../api/client";
import type { DeviceInfo, WifiScanAp, WifiStatus } from "../api/types";
import { useI18n } from "../i18n/useI18n";
import { Panel } from "./Card";
import { Field, GhostButton, PrimaryButton, TextInput } from "./Form";
import { StatusBadge } from "./StatusBadge";
import type { ShowToast } from "./Toast";

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
  const [aps, setAps] = useState<WifiScanAp[]>([]);
  const [scanning, setScanning] = useState(false);
  const [busy, setBusy] = useState(false);

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

  async function connect(e: React.FormEvent) {
    e.preventDefault();
    setBusy(true);
    try {
      const res = await api.connectWifi(ssid, password);
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

  return (
    <div className="space-y-4">
      {showStatus ? (
        <Panel title={t("wifi.status")}>
          <div className="flex items-center justify-between gap-3">
            <StatusBadge
              ok={wifi.connected}
              label={t("status.wifi")}
              detailOk={t("status.wifi-ok")}
              detailBad={t("status.wifi-bad")}
            />
            {wifi.connected ? (
              <div className="text-right text-sm text-muted">
                <div className="font-medium text-text-bright">{wifi.ssid}</div>
                <div>
                  {wifi.ip} · {wifi.rssi} dBm
                </div>
              </div>
            ) : (
              <span className="text-sm text-muted">{t("wifi.no-link")}</span>
            )}
          </div>
        </Panel>
      ) : null}

      <Panel
        title={t("wifi.networks")}
        action={
          <GhostButton type="button" onClick={() => void scan()} disabled={scanning}>
            <RefreshCw size={14} className={scanning ? "animate-spin" : ""} />
            {t("wifi.scan")}
          </GhostButton>
        }
      >
        <div className="space-y-2">
          {aps.length === 0 ? (
            <p className="text-sm text-muted">{scanning ? t("wifi.searching") : t("wifi.none")}</p>
          ) : (
            aps.map((ap) => (
              <button
                key={`${ap.ssid}-${ap.rssi}`}
                type="button"
                onClick={() => setSsid(ap.ssid)}
                className="flex w-full items-center justify-between rounded-lg border border-border bg-bg px-3 py-2.5 text-left transition hover:border-accent/40 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-bg"
              >
                <span className="inline-flex items-center gap-2 text-sm text-text-bright">
                  <Wifi size={16} className="text-accent" />
                  {ap.ssid || t("wifi.hidden")}
                  {!ap.open ? <Lock size={12} className="text-muted" /> : null}
                </span>
                <span className="text-xs text-muted">{ap.rssi} dBm</span>
              </button>
            ))
          )}
        </div>
      </Panel>

      <Panel>
        <form className="space-y-3" onSubmit={(e) => void connect(e)}>
          <Field label={t("wifi.ssid")}>
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
          <PrimaryButton type="submit" loading={busy} disabled={!ssid}>
            {device.mode === "ap" ? t("wifi.test-connect") : t("wifi.save-reboot")}
          </PrimaryButton>
        </form>
      </Panel>
    </div>
  );
}
