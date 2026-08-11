import { useCallback, useEffect, useState } from "react";
import type { ShowToast } from "../components/Toast";
import { api } from "../api/client";
import type { SettingsInfo } from "../api/types";
import { ConfirmDialog } from "../components/ConfirmDialog";
import { Panel } from "../components/Card";
import { DangerButton, Field, PrimaryButton, Switch, TextInput } from "../components/Form";
import { InfoTip } from "../components/InfoTip";
import { ErrorBlock, LoadingBlock } from "../components/StateBlock";
import { useI18n } from "../i18n/useI18n";

export function SettingsPage({
  onToast,
  onDeviceRefresh,
}: {
  onToast: ShowToast;
  onDeviceRefresh: () => Promise<void>;
}) {
  const { t } = useI18n();
  const [settings, setSettings] = useState<SettingsInfo | null>(null);
  const [busy, setBusy] = useState(false);
  const [loadError, setLoadError] = useState(false);
  const [confirmReboot, setConfirmReboot] = useState(false);
  const [confirmFactory, setConfirmFactory] = useState(false);

  const load = useCallback(async () => {
    setLoadError(false);
    setSettings(null);
    try {
      setSettings(await api.getSettings());
    } catch {
      setLoadError(true);
      onToast(t("toast.settings-load-failed"), "error");
    }
  }, [onToast, t]);

  useEffect(() => {
    void load();
  }, [load]);

  async function save(e: React.FormEvent) {
    e.preventDefault();
    if (!settings) return;
    setBusy(true);
    try {
      const res = await api.saveSettings({
        reset_days: settings.resetDays,
        display_dark: settings.displayDark ? 1 : 0,
      });
      if (!res.ok) {
        onToast(t("toast.save-failed"), "error");
        return;
      }
      onToast(t("toast.saved"), "success");
      await onDeviceRefresh();
    } catch {
      onToast(t("toast.save-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  async function reboot() {
    setBusy(true);
    try {
      const res = await api.reboot();
      onToast(res.ok ? t("toast.rebooting") : t("toast.reboot-failed"), res.ok ? "info" : "error");
      setConfirmReboot(false);
    } catch {
      onToast(t("toast.reboot-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  async function factoryReset() {
    setBusy(true);
    try {
      const res = await api.factoryReset();
      onToast(
        res.ok ? t("toast.reset-factory") : t("toast.reset-factory-failed"),
        res.ok ? "info" : "error",
      );
      if (res.ok) setConfirmFactory(false);
    } catch {
      onToast(t("toast.reset-factory-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  if (loadError) {
    return (
      <ErrorBlock
        title={t("settings.load-error-title")}
        message={t("settings.load-error")}
        retryLabel={t("common.retry")}
        onRetry={() => void load()}
      />
    );
  }

  if (!settings) {
    return <LoadingBlock label={t("settings.loading")} />;
  }

  return (
    <div className="space-y-4">
      <Panel title={t("settings.general")}>
        <form className="space-y-3" onSubmit={(e) => void save(e)}>
          <Field label={t("settings.reset-days")} hint={t("settings.reset-hint")}>
            <TextInput
              type="number"
              min={0}
              max={30}
              value={settings.resetDays}
              onChange={(e) => setSettings({ ...settings, resetDays: Number(e.target.value) })}
            />
          </Field>
          <Field label={t("settings.display-dark")} hint={t("settings.display-dark-hint")}>
            <Switch
              label={t("settings.display-dark")}
              checked={settings.displayDark}
              disabled={busy}
              onChange={(displayDark) => setSettings({ ...settings, displayDark })}
            />
          </Field>
          <PrimaryButton type="submit" loading={busy}>
            {t("common.save")}
          </PrimaryButton>
        </form>
      </Panel>

      <Panel>
        <div className="space-y-3">
          <DangerButton disabled={busy} onClick={() => setConfirmReboot(true)}>
            {t("settings.reboot")}
          </DangerButton>
          <div className="flex flex-col gap-3 border-t border-border pt-3">
            <h3 className="inline-flex items-center gap-1.5 text-sm font-semibold text-text-bright">
              {t("settings.factory-reset")}
              <InfoTip text={t("settings.factory-reset-hint")} />
            </h3>
            <DangerButton disabled={busy} onClick={() => setConfirmFactory(true)}>
              {t("settings.factory-reset-confirm")}
            </DangerButton>
          </div>
        </div>
      </Panel>

      <ConfirmDialog
        open={confirmReboot}
        title={t("settings.reboot-title")}
        description={t("settings.reboot-text")}
        confirmLabel={t("settings.reboot-confirm")}
        cancelLabel={t("common.cancel")}
        confirming={busy}
        onConfirm={() => void reboot()}
        onCancel={() => setConfirmReboot(false)}
      />
      <ConfirmDialog
        open={confirmFactory}
        title={t("settings.factory-reset-title")}
        description={t("settings.factory-reset-text")}
        confirmLabel={t("settings.factory-reset-confirm")}
        cancelLabel={t("common.cancel")}
        confirming={busy}
        onConfirm={() => void factoryReset()}
        onCancel={() => setConfirmFactory(false)}
      />
    </div>
  );
}
