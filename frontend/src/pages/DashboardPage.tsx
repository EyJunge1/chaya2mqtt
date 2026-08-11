import { Heart } from "lucide-react";
import { useState } from "react";
import type { ShowToast } from "../components/Toast";
import { api } from "../api/client";
import { otaHasPendingUpdate } from "../api/ota";
import type { ChayaStatus, DeviceInfo, OtaStatus, WifiStatus } from "../api/types";
import { Alert } from "../components/Alert";
import { Panel } from "../components/Card";
import { LinkButton, PrimaryButton } from "../components/Form";
import { StatusBadge } from "../components/StatusBadge";
import { WifiSetup } from "../components/WifiSetup";
import { useI18n } from "../i18n/useI18n";

export function DashboardPage({
  device,
  chaya,
  wifi,
  ota,
  onToast,
}: {
  device: DeviceInfo;
  chaya: ChayaStatus;
  wifi: WifiStatus;
  ota?: OtaStatus | null;
  onToast: ShowToast;
}) {
  const { t } = useI18n();
  const [busy, setBusy] = useState(false);

  async function sendHeart() {
    setBusy(true);
    try {
      const res = await api.sendChaya();
      if (!res.ok) {
        onToast(t("toast.heart-offline"), "error");
      } else {
        onToast(t("toast.heart-sent"), "success");
      }
    } catch {
      onToast(t("toast.heart-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  if (device.mode === "ap") {
    return (
      <div className="space-y-4">
        {device.apSsid && device.apIp ? (
          <Alert variant="info" title={t("wifi.setup-ap-title")}>
            <p>
              {t("wifi.setup-ap-text", {
                ssid: device.apSsid,
                ip: device.apIp,
              })}
            </p>
          </Alert>
        ) : null}
        <WifiSetup device={device} wifi={wifi} onToast={onToast} showStatus={false} />
      </div>
    );
  }

  return (
    <div className="space-y-4">
      {otaHasPendingUpdate(ota) && ota ? (
        <Alert variant="warning" title={t("dashboard.update-available-title")}>
          <div className="space-y-2">
            <p>{t("dashboard.update-available-text", { version: ota.availableVersion })}</p>
            <LinkButton to="/update" variant="warning">
              {t("dashboard.update-available-action")}
            </LinkButton>
          </div>
        </Alert>
      ) : null}

      <div className="flex flex-wrap gap-2">
        <StatusBadge
          to="/wifi"
          ok={wifi.connected}
          label={t("status.wifi")}
          detailOk={t("status.wifi-ok")}
          detailBad={t("status.wifi-bad")}
        />
        <StatusBadge
          to="/mqtt"
          ok={chaya.connected}
          label={t("status.mqtt")}
          detailOk={t("status.mqtt-ok")}
          detailBad={t("status.mqtt-bad")}
        />
      </div>

      <Panel title={t("dashboard.hearts")} hint={t("dashboard.hearts-hint")}>
        <div className="mb-4 grid grid-cols-2 gap-3">
          <div className="rounded-xl bg-bg px-3 py-4 text-center">
            <div className="inline-flex items-center justify-center gap-1 text-xs uppercase tracking-wide text-muted">
              <Heart size={12} fill="currentColor" className="text-accent" aria-hidden />
              {t("dashboard.rx")}
            </div>
            <div className="mt-1 text-3xl font-bold text-text-bright">{chaya.rx}</div>
          </div>
          <div className="rounded-xl bg-bg px-3 py-4 text-center">
            <div className="inline-flex items-center justify-center gap-1 text-xs uppercase tracking-wide text-muted">
              <Heart size={12} fill="currentColor" className="text-accent" aria-hidden />
              {t("dashboard.tx")}
            </div>
            <div className="mt-1 text-3xl font-bold text-text-bright">{chaya.tx}</div>
          </div>
        </div>
        <PrimaryButton onClick={sendHeart} loading={busy} disabled={!chaya.connected}>
          <Heart size={18} fill="currentColor" />
          {t("dashboard.send-heart")}
        </PrimaryButton>
      </Panel>
    </div>
  );
}
