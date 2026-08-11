import { useCallback, useEffect, useState } from "react";
import type { ShowToast } from "../components/Toast";
import { api } from "../api/client";
import type { MqttConfigView, MqttStatus } from "../api/types";
import { ActionRow } from "../components/ActionRow";
import { Panel } from "../components/Card";
import { Field, PrimaryButton, SecondaryButton, TextInput } from "../components/Form";
import { KeyValueGrid } from "../components/KeyValueGrid";
import { ErrorBlock, LoadingBlock } from "../components/StateBlock";
import { StatusBadge } from "../components/StatusBadge";
import { useI18n } from "../i18n/useI18n";
import { dash } from "../ui/styles";

export function MqttPage({
  mqtt,
  refreshSeq = 0,
  onToast,
  onDeviceRefresh,
}: {
  mqtt: MqttStatus;
  /** Bumps when DeviceProvider refreshes (mock scenario switch, resets). */
  refreshSeq?: number;
  onToast: ShowToast;
  onDeviceRefresh?: () => Promise<void>;
}) {
  const { t } = useI18n();
  const [cfg, setCfg] = useState<MqttConfigView | null>(null);
  const [password, setPassword] = useState("");
  const [partner, setPartner] = useState("");
  const [busy, setBusy] = useState(false);
  const [loadError, setLoadError] = useState(false);

  const load = useCallback(async () => {
    setLoadError(false);
    try {
      const next = await api.getMqttConfig();
      setCfg(next);
      setPartner(next.partnerId);
    } catch {
      setCfg(null);
      setLoadError(true);
      onToast(t("toast.mqtt-load-failed"), "error");
    }
  }, [onToast, t]);

  useEffect(() => {
    void load();
  }, [load, refreshSeq]);

  async function persist(nextPartner: string) {
    if (!cfg) return;
    setBusy(true);
    try {
      const res = await api.saveMqtt({
        mqtt_server: cfg.server,
        mqtt_port: cfg.port,
        mqtt_user: cfg.username,
        mqtt_pass: password || undefined,
        partner_id: nextPartner.trim().toLowerCase(),
      });
      if (!res.ok) {
        onToast(
          res.error === "partner" ? t("toast.partner-invalid") : t("toast.save-failed"),
          "error",
        );
        return;
      }
      onToast(t("toast.mqtt-saved"), "success");
      setPassword("");
      const next = await api.getMqttConfig();
      setCfg(next);
      setPartner(next.partnerId);
      await onDeviceRefresh?.();
    } catch {
      onToast(t("toast.save-failed"), "error");
    } finally {
      setBusy(false);
    }
  }

  async function save(e: React.FormEvent) {
    e.preventDefault();
    await persist(partner);
  }

  async function unpair() {
    setPartner("");
    await persist("");
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

  const brokerConfigured = Boolean(cfg.server.trim());
  const paired = Boolean(cfg.partnerId);

  return (
    <div className="space-y-4">
      <Panel
        title={
          <StatusBadge
            ok={mqtt.connected}
            label={t("mqtt.status")}
            detailOk={t("status.mqtt-ok")}
            detailBad={t("status.mqtt-bad")}
          />
        }
      >
        <KeyValueGrid
          items={[
            {
              label: t("mqtt.device-id"),
              value: <span className="tracking-widest">{dash(cfg.deviceId)}</span>,
            },
            {
              label: t("mqtt.partner-id"),
              value: dash(cfg.partnerId),
            },
            {
              label: t("mqtt.server"),
              value: brokerConfigured ? `${cfg.server}:${cfg.port}` : "-",
            },
            { label: t("mqtt.user"), value: dash(cfg.username) },
            { label: t("mqtt.topic-pub"), value: dash(cfg.topicPub) },
            { label: t("mqtt.topic-sub"), value: dash(cfg.topicSub) },
          ]}
        />
      </Panel>

      <Panel>
        <form className="space-y-3" onSubmit={(e) => void save(e)}>
          <Field label={t("mqtt.server")} hint={t("mqtt.server-hint")}>
            <TextInput
              value={cfg.server}
              onChange={(e) => setCfg({ ...cfg, server: e.target.value })}
              maxLength={127}
              required
            />
          </Field>
          <Field label={t("mqtt.port")} hint={t("mqtt.port-hint")}>
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

          <Field label={t("mqtt.partner-id")} hint={t("mqtt.partner-hint")}>
            <TextInput
              value={partner}
              onChange={(e) => setPartner(e.target.value)}
              maxLength={6}
              pattern="[0-9a-fA-F]{0,6}"
              placeholder="f5e6d7"
            />
          </Field>

          <ActionRow>
            <PrimaryButton type="submit" loading={busy} className="sm:flex-1">
              {t("common.save")}
            </PrimaryButton>
            {paired ? (
              <SecondaryButton
                type="button"
                loading={busy}
                onClick={() => void unpair()}
                className="sm:flex-1"
              >
                {t("mqtt.unpair")}
              </SecondaryButton>
            ) : null}
          </ActionRow>
        </form>
      </Panel>
    </div>
  );
}
