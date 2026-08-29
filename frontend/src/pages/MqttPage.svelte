<script lang="ts">
  import { Check, Copy, Radio, RadioOff } from "@lucide/svelte";
  import { untrack } from "svelte";
  import { api } from "../api/client.ts";
  import type { MqttConfigView, MqttStatus } from "../api/types.ts";
  import ActionRow from "../components/ActionRow.svelte";
  import ErrorBlock from "../components/ErrorBlock.svelte";
  import Field from "../components/Field.svelte";
  import KeyValueGrid from "../components/KeyValueGrid.svelte";
  import LoadingBlock from "../components/LoadingBlock.svelte";
  import Panel from "../components/Panel.svelte";
  import PrimaryButton from "../components/PrimaryButton.svelte";
  import SecondaryButton from "../components/SecondaryButton.svelte";
  import SegmentedControl from "../components/SegmentedControl.svelte";
  import StatusBadge from "../components/StatusBadge.svelte";
  import TextInput from "../components/TextInput.svelte";
  import type { ShowToast } from "../components/toastStack.ts";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { cn } from "../ui/cn.ts";
  import { dash, HOVER_SURFACE } from "../ui/styles.ts";

  const MQTT_PLAIN_PORT = 1883;
  const MQTT_TLS_PORT = 8883;

  type MqttProtocol = "mqtt" | "mqtts";

  let {
    mqtt,
    refreshSeq = 0,
    onToast,
    onDeviceRefresh,
  }: {
    mqtt: MqttStatus;
    refreshSeq?: number;
    onToast: ShowToast;
    onDeviceRefresh?: () => Promise<void>;
  } = $props();

  let cfg = $state<MqttConfigView | null>(null);
  let password = $state("");
  let partner = $state("");
  let busy = $state(false);
  let loadError = $state(false);
  let copied = $state(false);
  let copiedReset: ReturnType<typeof setTimeout> | undefined;

  async function load() {
    loadError = false;
    try {
      const next = await api.getMqttConfig();
      cfg = next;
      partner = next.partnerId;
    } catch {
      cfg = null;
      loadError = true;
      onToast(i18n.t("toast.mqtt-load-failed"), "error");
    }
  }

  $effect(() => {
    void refreshSeq;
    untrack(() => {
      void load();
    });
  });

  function protocolOf(tls: boolean): MqttProtocol {
    return tls ? "mqtts" : "mqtt";
  }

  function setProtocol(next: MqttProtocol) {
    if (!cfg) return;
    const nextTls = next === "mqtts";
    if (cfg.tls === nextTls) return;
    const oldDefault = cfg.tls ? MQTT_TLS_PORT : MQTT_PLAIN_PORT;
    const newDefault = nextTls ? MQTT_TLS_PORT : MQTT_PLAIN_PORT;
    const port = Number(cfg.port);
    if (port === oldDefault) {
      cfg.port = newDefault;
    }
    cfg.tls = nextTls;
  }

  async function persist(nextPartner: string) {
    if (!cfg) return;
    busy = true;
    try {
      const res = await api.saveMqtt({
        mqtt_server: cfg.server,
        mqtt_port: cfg.port,
        mqtt_tls: cfg.tls ? 1 : 0,
        mqtt_user: cfg.username,
        mqtt_pass: password || undefined,
        partner_id: nextPartner.trim().toLowerCase(),
      });
      if (!res.ok) {
        onToast(
          res.error === "partner" ? i18n.t("toast.partner-invalid") : i18n.t("toast.save-failed"),
          "error",
        );
        return;
      }
      onToast(i18n.t("toast.mqtt-saved"), "success");
      password = "";
      const next = await api.getMqttConfig();
      cfg = next;
      partner = next.partnerId;
      await onDeviceRefresh?.();
    } catch {
      onToast(i18n.t("toast.save-failed"), "error");
    } finally {
      busy = false;
    }
  }

  async function save(e: SubmitEvent) {
    e.preventDefault();
    await persist(partner);
  }

  async function unpair() {
    partner = "";
    await persist("");
  }

  async function copyDeviceId() {
    const id = cfg?.deviceId?.trim();
    if (!id) return;
    try {
      await navigator.clipboard.writeText(id);
      copied = true;
      clearTimeout(copiedReset);
      copiedReset = setTimeout(() => {
        copied = false;
      }, 1500);
    } catch {
      onToast(i18n.t("toast.device-id-copy-failed"), "error");
    }
  }

  const brokerConfigured = $derived(Boolean(cfg?.server.trim()));
  const paired = $derived(Boolean(cfg?.partnerId));
  const MqttIcon = $derived(brokerConfigured ? Radio : RadioOff);
  const hasDeviceId = $derived(Boolean(cfg?.deviceId?.trim()));
  const brokerDisplay = $derived(
    cfg && brokerConfigured ? `${cfg.tls ? "mqtts" : "mqtt"}://${cfg.server}:${cfg.port}` : "-",
  );
</script>

{#if loadError}
  <ErrorBlock
    title={i18n.t("mqtt.load-error-title")}
    message={i18n.t("mqtt.load-error")}
    retryLabel={i18n.t("common.retry")}
    onRetry={() => void load()}
  />
{:else if !cfg}
  <LoadingBlock label={i18n.t("mqtt.loading")} />
{:else}
  <div class="space-y-4">
    <Panel>
      {#snippet title()}
        <StatusBadge
          ok={mqtt.connected}
          icon={MqttIcon}
          neutral={!brokerConfigured}
          label={i18n.t("mqtt.status")}
          detailOk={i18n.t("status.mqtt-ok")}
          detailBad={i18n.t(brokerConfigured ? "status.mqtt-bad" : "status.mqtt-unconfigured")}
        />
      {/snippet}
      {#snippet deviceIdValue()}
        <span class="inline-flex items-center gap-1.5 tracking-widest">
          {dash(cfg.deviceId)}
          {#if hasDeviceId}
            <button
              type="button"
              aria-label={i18n.t("mqtt.copy-device-id")}
              class={cn(
                "inline-flex size-7 shrink-0 items-center justify-center rounded-full text-muted transition focus-ring",
                HOVER_SURFACE,
              )}
              onclick={() => void copyDeviceId()}
            >
              {#if copied}
                <Check
                  size={14}
                  strokeWidth={2.25}
                  class="pointer-events-none"
                  aria-hidden="true"
                />
              {:else}
                <Copy size={14} strokeWidth={2.25} class="pointer-events-none" aria-hidden="true" />
              {/if}
            </button>
          {/if}
        </span>
      {/snippet}
      <KeyValueGrid
        items={[
          {
            label: i18n.t("mqtt.device-id"),
            value: deviceIdValue,
          },
          { label: i18n.t("mqtt.partner-id"), value: dash(cfg.partnerId) },
          {
            label: i18n.t("mqtt.server"),
            value: brokerDisplay,
          },
          { label: i18n.t("mqtt.user"), value: dash(cfg.username) },
          { label: i18n.t("mqtt.topic-pub"), value: dash(cfg.topicPub) },
          { label: i18n.t("mqtt.topic-sub"), value: dash(cfg.topicSub) },
        ]}
      />
    </Panel>

    <Panel>
      <form class="space-y-3" onsubmit={(e) => void save(e)}>
        <Field label={i18n.t("mqtt.server")} hint={i18n.t("mqtt.server-hint")} required>
          <div class="flex items-stretch gap-2">
            <SegmentedControl
              compact
              class="w-[8.5rem]"
              label={i18n.t("mqtt.protocol")}
              value={protocolOf(cfg.tls)}
              onChange={setProtocol}
              options={[
                { value: "mqtt", label: i18n.t("mqtt.protocol-mqtt"), testId: "mqtt-proto-mqtt" },
                {
                  value: "mqtts",
                  label: i18n.t("mqtt.protocol-mqtts"),
                  testId: "mqtt-proto-mqtts",
                },
              ]}
            />
            <div class="min-w-0 flex-1">
              <TextInput bind:value={cfg.server} maxlength={127} required />
            </div>
          </div>
        </Field>
        <Field label={i18n.t("mqtt.port")} hint={i18n.t("mqtt.port-hint")} required>
          <TextInput type="number" min={1} max={65535} bind:value={cfg.port} required />
        </Field>
        <Field label={i18n.t("mqtt.user")} hint={i18n.t("mqtt.user-hint")}>
          <TextInput bind:value={cfg.username} maxlength={63} />
        </Field>
        <Field
          label={i18n.t("mqtt.pass")}
          hint={cfg.hasPassword ? i18n.t("mqtt.pass-hint") : i18n.t("mqtt.pass-hint-empty")}
        >
          <TextInput
            type="password"
            bind:value={password}
            maxlength={63}
            autocomplete="current-password"
            placeholder={cfg.hasPassword ? i18n.t("mqtt.pass-placeholder") : ""}
          />
        </Field>

        <Field label={i18n.t("mqtt.partner-id")} hint={i18n.t("mqtt.partner-hint")}>
          <TextInput
            bind:value={partner}
            maxlength={6}
            pattern={"[0-9a-fA-F]{6}"}
            placeholder="f5e6d7"
          />
        </Field>

        <ActionRow>
          <PrimaryButton type="submit" loading={busy} class="sm:flex-1">
            {i18n.t("common.save")}
          </PrimaryButton>
          {#if paired}
            <SecondaryButton
              type="button"
              loading={busy}
              onclick={() => void unpair()}
              class="sm:flex-1"
            >
              {i18n.t("mqtt.unpair")}
            </SecondaryButton>
          {/if}
        </ActionRow>
      </form>
    </Panel>
  </div>
{/if}
