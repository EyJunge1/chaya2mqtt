import { useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";
import type { ShowToast } from "../components/Toast";
import { api } from "../api/client";
import type { WifiConnectStatus } from "../api/types";
import { Alert } from "../components/Alert";
import { Panel } from "../components/Card";
import { DangerButton, PrimaryButton } from "../components/Form";
import { Spinner } from "../components/Spinner";
import { useI18n } from "../i18n/useI18n";

export function WifiTestingPage({ onToast }: { onToast: ShowToast }) {
  const { t } = useI18n();
  const navigate = useNavigate();
  const [status, setStatus] = useState<WifiConnectStatus>({ state: "testing", ssid: "" });
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    let alive = true;
    const tick = async () => {
      try {
        const s = await api.getWifiConnectStatus();
        if (alive) setStatus(s);
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
  }, []);

  async function commit() {
    setBusy(true);
    try {
      const res = await api.commitWifiConnect();
      if (!res.ok) {
        onToast(t("toast.wifi-commit-failed"), "error");
        return;
      }
      onToast(t("toast.wifi-committed"), "success");
      navigate("/");
    } catch {
      onToast(t("toast.wifi-commit-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  async function abort() {
    setBusy(true);
    try {
      await api.abortWifiConnect();
      navigate("/wifi");
    } catch {
      onToast(t("toast.wifi-abort-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  const statusText =
    status.state === "testing"
      ? t("wifi-test.testing")
      : status.state === "ok"
        ? t("wifi-test.ok")
        : status.state === "fail"
          ? t("wifi-test.fail")
          : t("wifi-test.idle");

  return (
    <div className="space-y-4">
      <Panel
        title={t("wifi-test.title")}
        hint={status.state !== "ok" ? t("wifi-test.commit-hint") : undefined}
      >
        <p className="mb-2 text-sm text-muted">
          {t("wifi-test.ssid")} <span className="text-text-bright">{status.ssid || "…"}</span>
        </p>
        <p
          role="status"
          aria-busy={status.state === "testing" || undefined}
          className="inline-flex items-center gap-2 text-sm text-text-bright"
        >
          {status.state === "testing" ? <Spinner size={16} /> : null}
          {statusText}
        </p>
      </Panel>
      {status.state === "fail" ? <Alert variant="error">{t("wifi-test.fail")}</Alert> : null}
      <div className="space-y-3">
        <PrimaryButton
          loading={busy}
          disabled={status.state !== "ok"}
          onClick={() => void commit()}
        >
          {t("wifi-test.commit")}
        </PrimaryButton>
        <DangerButton loading={busy} onClick={() => void abort()}>
          {t("wifi-test.abort")}
        </DangerButton>
      </div>
    </div>
  );
}
