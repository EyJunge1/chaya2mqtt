import { useCallback, useEffect, useState } from "react";
import type { ShowToast } from "../components/Toast";
import { api } from "../api/client";
import type { SettingsInfo, UiLang, UiTheme } from "../api/types";
import { ConfirmDialog } from "../components/ConfirmDialog";
import { Panel } from "../components/Card";
import { DangerButton, Field, PrimaryButton, SelectInput, TextInput } from "../components/Form";
import { ErrorBlock, LoadingBlock } from "../components/StateBlock";
import { useI18n } from "../i18n/useI18n";
import { setTheme } from "../theme/store";

export function SettingsPage({
  onToast,
  onDeviceRefresh,
}: {
  onToast: ShowToast;
  onDeviceRefresh: () => Promise<void>;
}) {
  const { t, setLanguage } = useI18n();
  const [settings, setSettings] = useState<SettingsInfo | null>(null);
  const [busy, setBusy] = useState(false);
  const [loadError, setLoadError] = useState(false);
  const [confirmReboot, setConfirmReboot] = useState(false);

  const load = useCallback(async () => {
    setLoadError(false);
    setSettings(null);
    try {
      const next = await api.getSettings();
      setSettings(next);
      if (next.lang === "de" || next.lang === "en") setLanguage(next.lang);
      if (next.theme === "dark" || next.theme === "light") setTheme(next.theme);
    } catch {
      setLoadError(true);
      onToast(t("toast.settings-load-failed"), "error");
    }
  }, [onToast, setLanguage, t]);

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
        lang: settings.lang,
        theme: settings.theme,
      });
      if (!res.ok) {
        onToast(t("toast.save-failed"), "error");
        return;
      }
      setLanguage(settings.lang);
      setTheme(settings.theme);
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
          <Field label={t("settings.language")}>
            <SelectInput
              value={settings.lang}
              onChange={(e) => setSettings({ ...settings, lang: e.target.value as UiLang })}
            >
              <option value="en">{t("settings.lang-en")}</option>
              <option value="de">{t("settings.lang-de")}</option>
            </SelectInput>
          </Field>
          <Field label={t("settings.theme")}>
            <SelectInput
              value={settings.theme}
              onChange={(e) => setSettings({ ...settings, theme: e.target.value as UiTheme })}
            >
              <option value="light">{t("settings.theme-light")}</option>
              <option value="dark">{t("settings.theme-dark")}</option>
            </SelectInput>
          </Field>
          <PrimaryButton type="submit" loading={busy}>
            {t("common.save")}
          </PrimaryButton>
        </form>
      </Panel>

      <Panel title={t("settings.device")}>
        <DangerButton disabled={busy} onClick={() => setConfirmReboot(true)}>
          {t("settings.reboot")}
        </DangerButton>
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
    </div>
  );
}
