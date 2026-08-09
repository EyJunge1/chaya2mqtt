import { Heart, Link2, Radio, Settings, Upload, Wifi } from "lucide-react";
import { useState } from "react";
import type { ShowToast } from "../components/Toast";
import { api } from "../api/client";
import type { ChayaStatus, DeviceInfo, WifiStatus } from "../api/types";
import { NavCard, Panel } from "../components/Card";
import { PrimaryButton } from "../components/Form";
import { WifiSetup } from "../components/WifiSetup";
import { useI18n } from "../i18n/useI18n";

export function DashboardPage({
  device,
  chaya,
  wifi,
  onToast,
}: {
  device: DeviceInfo;
  chaya: ChayaStatus;
  wifi: WifiStatus;
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
    return <WifiSetup device={device} wifi={wifi} onToast={onToast} showStatus={false} />;
  }

  return (
    <div className="space-y-4">
      <Panel>
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

      <div className="grid gap-3">
        <NavCard
          to="/wifi"
          title={t("dashboard.nav-wifi")}
          subtitle={t("dashboard.nav-wifi-sub")}
          icon={Wifi}
        />
        <NavCard
          to="/mqtt"
          title={t("dashboard.nav-mqtt")}
          subtitle={t("dashboard.nav-mqtt-sub")}
          icon={Radio}
        />
        <NavCard
          to="/pairing"
          title={t("dashboard.nav-pairing")}
          subtitle={t("dashboard.nav-pairing-sub")}
          icon={Link2}
        />
        <NavCard
          to="/settings"
          title={t("dashboard.nav-settings")}
          subtitle={t("dashboard.nav-settings-sub")}
          icon={Settings}
        />
        <NavCard
          to="/update"
          title={t("dashboard.nav-update")}
          subtitle={t("dashboard.nav-update-sub")}
          icon={Upload}
        />
      </div>
    </div>
  );
}
