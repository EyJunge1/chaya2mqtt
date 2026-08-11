import { useCallback, useEffect, useMemo, useState } from "react";
import type { ShowToast } from "../components/Toast";
import { api } from "../api/client";
import { otaHasPendingUpdate } from "../api/ota";
import type { OtaChannel, OtaPhase, OtaStatus } from "../api/types";
import { ActionRow } from "../components/ActionRow";
import { ConfirmDialog } from "../components/ConfirmDialog";
import { Panel } from "../components/Card";
import { Alert } from "../components/Alert";
import { Field, PrimaryButton, SecondaryButton, SelectInput } from "../components/Form";
import { KeyValueGrid } from "../components/KeyValueGrid";
import { ProgressBar } from "../components/ProgressBar";
import { ErrorBlock, LoadingBlock } from "../components/StateBlock";
import { useI18n } from "../i18n/useI18n";

const emptyStatus = (): OtaStatus => ({
  phase: "idle",
  channel: "stable",
  localVersion: "",
  availableVersion: "",
  bytesDone: 0,
  bytesTotal: 0,
  error: "",
  generation: 0,
});

function phaseLabelKey(phase: OtaPhase): `update.phase.${OtaPhase}` {
  return `update.phase.${phase}`;
}

export function UpdatePage({
  onToast,
  otaStatus = null,
}: {
  onToast: ShowToast;
  otaStatus?: OtaStatus | null;
}) {
  const { t } = useI18n();
  const [status, setStatus] = useState<OtaStatus | null>(otaStatus);
  const [channel, setChannel] = useState<OtaChannel>("stable");
  const [loadError, setLoadError] = useState(false);
  const [busy, setBusy] = useState(false);
  const [confirmInstall, setConfirmInstall] = useState(false);

  const applyStatus = useCallback((next: OtaStatus) => {
    setStatus(next);
    setChannel(next.channel);
  }, []);

  const load = useCallback(async () => {
    setLoadError(false);
    try {
      const next = await api.getUpdateStatus();
      applyStatus(next);
    } catch {
      setLoadError(true);
      onToast(t("toast.update-status-failed"), "error");
    }
  }, [applyStatus, onToast, t]);

  useEffect(() => {
    void load();
  }, [load]);

  useEffect(() => {
    if (otaStatus) applyStatus(otaStatus);
  }, [applyStatus, otaStatus]);

  const installing =
    status?.phase === "downloading" ||
    status?.phase === "verifying" ||
    status?.phase === "rebooting";
  const checking = status?.phase === "checking";
  const pendingUpdate = otaHasPendingUpdate(status);
  const canInstall =
    !!status &&
    pendingUpdate &&
    (status.phase === "available" || status.phase === "error") &&
    !installing &&
    !checking;

  const progressPct = useMemo(() => {
    if (!status || status.bytesTotal <= 0) return null;
    return Math.min(100, Math.round((status.bytesDone / status.bytesTotal) * 100));
  }, [status]);

  async function check() {
    setBusy(true);
    try {
      const res = await api.checkUpdate(channel);
      onToast(
        res.ok ? t("toast.update-checking") : t("toast.update-failed"),
        res.ok ? "info" : "error",
      );
      if (res.ok) {
        setStatus((prev) => ({
          ...(prev ?? emptyStatus()),
          phase: "checking",
          channel,
          error: "",
        }));
      }
    } catch {
      onToast(t("toast.update-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  async function install() {
    setBusy(true);
    try {
      const res = await api.installUpdate();
      if (!res.ok) {
        onToast(t("toast.update-install-failed"), "error");
        setConfirmInstall(false);
        return;
      }
      onToast(t("toast.update-installing"), "info");
      setConfirmInstall(false);
      setStatus((prev) => ({
        ...(prev ?? emptyStatus()),
        phase: "downloading",
        error: "",
      }));
    } catch {
      onToast(t("toast.update-install-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  if (loadError && !status) {
    return (
      <ErrorBlock
        title={t("update.load-error-title")}
        message={t("update.load-error")}
        retryLabel={t("common.retry")}
        onRetry={() => void load()}
      />
    );
  }

  if (!status) {
    return <LoadingBlock label={t("app.connecting")} />;
  }

  return (
    <>
      <Panel title={t("update.title")} hint={t("update.text")}>
        <div className="space-y-4">
          <KeyValueGrid
            items={[
              { label: t("update.installed"), value: status.localVersion?.trim() || "-" },
              {
                label: t("update.available"),
                value: status.availableVersion || t("update.none"),
              },
              {
                label: t("update.status"),
                value:
                  status.phase === "available" && !pendingUpdate
                    ? t("update.phase.idle")
                    : t(phaseLabelKey(status.phase)),
                span: 2,
              },
            ]}
          />

          <Field label={t("update.channel")} hint={t("update.channel-hint")}>
            <SelectInput
              value={channel}
              disabled={busy || checking || installing}
              onChange={(e) => setChannel(e.target.value as OtaChannel)}
            >
              <option value="stable">{t("update.channel.stable")}</option>
              <option value="beta">{t("update.channel.beta")}</option>
            </SelectInput>
          </Field>

          {(status.phase === "downloading" || status.phase === "verifying") && (
            <div className="space-y-2">
              <ProgressBar value={progressPct} label={t("update.status")} />
              <p className="text-sm text-muted">
                {progressPct != null
                  ? t("update.progress", { pct: String(progressPct) })
                  : t("update.progress-unknown")}
              </p>
            </div>
          )}

          {status.phase === "rebooting" ? (
            <p className="text-sm text-muted">{t("update.rebooting-hint")}</p>
          ) : null}

          {status.phase === "error" && status.error ? (
            <Alert variant="error" title={t("update.error-title")}>
              {t("update.error", { code: status.error })}
            </Alert>
          ) : null}

          <ActionRow>
            <PrimaryButton
              loading={busy || checking}
              disabled={installing}
              onClick={() => void check()}
              className="sm:flex-1"
            >
              {t("update.check")}
            </PrimaryButton>
            <SecondaryButton
              disabled={!canInstall || busy}
              onClick={() => setConfirmInstall(true)}
              className="sm:flex-1"
            >
              {t("update.install")}
            </SecondaryButton>
          </ActionRow>
        </div>
      </Panel>

      <ConfirmDialog
        open={confirmInstall}
        title={t("update.confirm-title")}
        description={t("update.confirm-text", {
          version: status.availableVersion || "?",
        })}
        confirmLabel={t("update.install")}
        cancelLabel={t("common.cancel")}
        confirming={busy}
        confirmVariant="primary"
        onConfirm={() => void install()}
        onCancel={() => setConfirmInstall(false)}
      />
    </>
  );
}
