import { useState } from "react";
import type { ShowToast } from "../components/Toast";
import { api } from "../api/client";
import { Panel } from "../components/Card";
import { PrimaryButton } from "../components/Form";
import { useI18n } from "../i18n/useI18n";

export function UpdatePage({ onToast }: { onToast: ShowToast }) {
  const { t } = useI18n();
  const [busy, setBusy] = useState(false);

  async function check() {
    setBusy(true);
    try {
      const res = await api.checkUpdate();
      onToast(
        res.ok ? t("toast.update-checking") : t("toast.update-failed"),
        res.ok ? "info" : "error",
      );
    } catch {
      onToast(t("toast.update-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  return (
    <Panel title={t("update.title")} hint={t("update.text")}>
      <PrimaryButton loading={busy} onClick={() => void check()}>
        {t("update.check")}
      </PrimaryButton>
    </Panel>
  );
}
