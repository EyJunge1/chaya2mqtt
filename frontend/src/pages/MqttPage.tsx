import { useCallback, useEffect, useState } from "react";
import type { ShowToast } from "../components/Toast";
import { api } from "../api/client";
import type { MqttConfigView, MqttStatus } from "../api/types";
import { Panel } from "../components/Card";
import { Field, PrimaryButton, TextInput } from "../components/Form";
import { ErrorBlock, LoadingBlock } from "../components/StateBlock";
import { StatusBadge } from "../components/StatusBadge";
import { useI18n } from "../i18n/useI18n";

export function MqttPage({ mqtt, onToast }: { mqtt: MqttStatus; onToast: ShowToast }) {
  const { t } = useI18n();
  const [cfg, setCfg] = useState<MqttConfigView | null>(null);
  const [password, setPassword] = useState("");
  const [busy, setBusy] = useState(false);
  const [loadError, setLoadError] = useState(false);

  const load = useCallback(async () => {
    setLoadError(false);
    setCfg(null);
    try {
      setCfg(await api.getMqttConfig());
    } catch {
      setLoadError(true);
      onToast(t("toast.mqtt-load-failed"), "error");
    }
  }, [onToast, t]);

  useEffect(() => {
    void load();
  }, [load]);

  async function save(e: React.FormEvent) {
    e.preventDefault();
    if (!cfg) return;
    setBusy(true);
    try {
      const res = await api.saveMqtt({
        mqtt_server: cfg.server,
        mqtt_port: cfg.port,
        mqtt_user: cfg.username,
        mqtt_pass: password || undefined,
        mqtt_topic_pub: cfg.topicPub,
        mqtt_topic_sub: cfg.topicSub,
      });
      onToast(
        res.ok ? t("toast.mqtt-saved") : t("toast.save-failed"),
        res.ok ? "success" : "error",
      );
      if (res.ok) setPassword("");
    } catch {
      onToast(t("toast.save-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  if (loadError) {
    return (
      <ErrorBlock
        title={t("mqtt.load-error-title")}
        message={t("mqtt.load-error")}
        retryLabel={t("common.retry")}
        onRetry={() => void load()}
      />
    );
  }

  if (!cfg) {
    return <LoadingBlock label={t("mqtt.loading")} />;
  }

  return (
    <div className="space-y-4">
      <Panel title={t("mqtt.status")}>
        <StatusBadge
          ok={mqtt.connected}
          label={t("status.mqtt")}
          detailOk={t("status.mqtt-ok")}
          detailBad={t("status.mqtt-bad")}
        />
      </Panel>
      <Panel title={t("mqtt.broker")}>
        <form className="space-y-3" onSubmit={(e) => void save(e)}>
          <Field label={t("mqtt.server")} hint={t("mqtt.server-hint")}>
            <TextInput
              value={cfg.server}
              onChange={(e) => setCfg({ ...cfg, server: e.target.value })}
              maxLength={127}
              required
            />
          </Field>
          <Field label={t("mqtt.port")}>
            <TextInput
              type="number"
              min={1}
              max={65535}
              value={cfg.port}
              onChange={(e) => setCfg({ ...cfg, port: Number(e.target.value) })}
              required
            />
          </Field>
          <Field label={t("mqtt.user")} hint={t("mqtt.user-hint")}>
            <TextInput
              value={cfg.username}
              onChange={(e) => setCfg({ ...cfg, username: e.target.value })}
              maxLength={63}
            />
          </Field>
          <Field
            label={t("mqtt.pass")}
            hint={cfg.hasPassword ? t("mqtt.pass-hint") : t("mqtt.pass-hint-empty")}
          >
            <TextInput
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              maxLength={63}
              autoComplete="current-password"
              placeholder={cfg.hasPassword ? t("mqtt.pass-placeholder") : ""}
            />
          </Field>
          <Field label={t("mqtt.topic-pub")}>
            <TextInput
              value={cfg.topicPub}
              onChange={(e) => setCfg({ ...cfg, topicPub: e.target.value })}
              maxLength={127}
              required
            />
          </Field>
          <Field label={t("mqtt.topic-sub")}>
            <TextInput
              value={cfg.topicSub}
              onChange={(e) => setCfg({ ...cfg, topicSub: e.target.value })}
              maxLength={127}
              required
            />
          </Field>
          <PrimaryButton type="submit" loading={busy}>
            {t("common.save")}
          </PrimaryButton>
        </form>
      </Panel>
    </div>
  );
}
