<script lang="ts">
  import { untrack } from "svelte";
  import { api } from "../api/client.ts";
  import type { SettingsInfo } from "../api/types.ts";
  import ConfirmDialog from "../components/ConfirmDialog.svelte";
  import DangerButton from "../components/DangerButton.svelte";
  import ErrorBlock from "../components/ErrorBlock.svelte";
  import Field from "../components/Field.svelte";
  import InfoTip from "../components/InfoTip.svelte";
  import LoadingBlock from "../components/LoadingBlock.svelte";
  import Panel from "../components/Panel.svelte";
  import PrimaryButton from "../components/PrimaryButton.svelte";
  import Switch from "../components/Switch.svelte";
  import TextInput from "../components/TextInput.svelte";
  import type { ShowToast } from "../components/toastStack.ts";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { applyDeviceUiPrefs } from "../prefs/uiPrefs.ts";
  import {
    AUDIO_TONE_HZ_MAX,
    AUDIO_TONE_HZ_MIN,
    AUDIO_TONE_MS_MAX,
    AUDIO_TONE_MS_MIN,
    AUDIO_VOLUME_MAX,
    AUDIO_VOLUME_MIN,
  } from "../audio/ranges.ts";

  let {
    onToast,
    onDeviceRefresh,
  }: {
    onToast: ShowToast;
    onDeviceRefresh: () => Promise<void>;
  } = $props();

  let settings = $state<SettingsInfo | null>(null);
  let busy = $state(false);
  let quietHoursEnabled = $state(false);
  let loadError = $state(false);
  let confirmReboot = $state(false);
  let confirmFactory = $state(false);
  let loadSeq = 0;

  async function load() {
    const seq = ++loadSeq;
    loadError = false;
    settings = null;
    try {
      const s = await api.getSettings();
      if (seq !== loadSeq) return;
      settings = s;
      quietHoursEnabled = s.quietHourStart !== s.quietHourEnd;
      applyDeviceUiPrefs(s.lang, s.theme);
    } catch {
      if (seq !== loadSeq) return;
      loadError = true;
    }
  }

  /** Wait for deferred apply; surface NVS failure via nvsOk (QUAL-01). */
  async function waitForSettingsPersist(seq: number): Promise<SettingsInfo | null> {
    for (let i = 0; i < 25; i++) {
      if (seq !== loadSeq) return null;
      const s = await api.getSettings();
      if (seq !== loadSeq) return null;
      if (!s.applyPending) {
        return s;
      }
      await new Promise((r) => setTimeout(r, 80));
    }
    if (seq !== loadSeq) return null;
    return api.getSettings();
  }

  $effect(() => {
    untrack(() => {
      void load();
    });
    return () => {
      loadSeq += 1;
    };
  });

  async function save(e: SubmitEvent) {
    e.preventDefault();
    if (!settings) return;
    const seq = loadSeq;
    busy = true;
    try {
      const res = await api.saveSettings({
        reset_days: settings.resetDays,
        led_enabled: settings.ledEnabled,
        audio_tx_enabled: settings.audioTxEnabled,
        audio_rx_enabled: settings.audioRxEnabled,
        audio_tx_volume: settings.audioTxVolume,
        audio_rx_volume: settings.audioRxVolume,
        quiet_hour_start: settings.quietHourStart,
        quiet_hour_end: quietHoursEnabled ? settings.quietHourEnd : settings.quietHourStart,
        tx_hz: settings.txHz,
        tx_ms: settings.txMs,
        rx_hz: settings.rxHz,
        rx_ms: settings.rxMs,
      });
      if (seq !== loadSeq) return;
      if (!res.ok) {
        onToast(i18n.t("toast.save-failed"), "error");
        return;
      }
      const applied = await waitForSettingsPersist(seq);
      if (seq !== loadSeq) return;
      if (applied) {
        settings = applied;
        quietHoursEnabled = applied.quietHourStart !== applied.quietHourEnd;
      }
      if (applied?.nvsOk === false) {
        onToast(i18n.t("toast.save-failed"), "error");
        return;
      }
      onToast(i18n.t("toast.saved"), "success");
      await onDeviceRefresh();
    } catch {
      if (seq !== loadSeq) return;
      onToast(i18n.t("toast.save-failed"), "error");
    } finally {
      if (seq === loadSeq) busy = false;
    }
  }

  async function reboot() {
    busy = true;
    try {
      const res = await api.reboot();
      onToast(
        res.ok ? i18n.t("toast.rebooting") : i18n.t("toast.reboot-failed"),
        res.ok ? "info" : "error",
      );
      confirmReboot = false;
    } catch {
      onToast(i18n.t("toast.reboot-failed"), "error");
    } finally {
      busy = false;
    }
  }

  async function factoryReset() {
    busy = true;
    try {
      const res = await api.factoryReset();
      onToast(
        res.ok ? i18n.t("toast.reset-factory") : i18n.t("toast.reset-factory-failed"),
        res.ok ? "info" : "error",
      );
      if (res.ok) {
        confirmFactory = false;
        await onDeviceRefresh();
      }
    } catch {
      onToast(i18n.t("toast.reset-factory-failed"), "error");
    } finally {
      busy = false;
    }
  }

  function hourToTimeValue(hour: number): string {
    const h = Math.max(0, Math.min(23, Math.trunc(Number.isFinite(hour) ? hour : 0)));
    return `${String(h).padStart(2, "0")}:00`;
  }

  function timeValueToHour(value: string): number {
    const hour = Number.parseInt(value.slice(0, 2), 10);
    if (!Number.isFinite(hour)) return 0;
    return Math.max(0, Math.min(23, hour));
  }
</script>

{#if loadError}
  <ErrorBlock
    title={i18n.t("settings.load-error-title")}
    message={i18n.t("settings.load-error")}
    retryLabel={i18n.t("common.retry")}
    onRetry={() => void load()}
  />
{:else if !settings}
  <LoadingBlock label={i18n.t("settings.loading")} />
{:else}
  <div class="space-y-4">
    <Panel title={i18n.t("settings.general")}>
      <form class="space-y-3" onsubmit={(e) => void save(e)}>
        <Field label={i18n.t("settings.reset-days")} hint={i18n.t("settings.reset-hint")}>
          <TextInput type="number" min={0} max={30} bind:value={settings.resetDays} />
        </Field>
        <PrimaryButton type="submit" loading={busy}>
          {i18n.t("common.save")}
        </PrimaryButton>
      </form>
    </Panel>

    <Panel title={i18n.t("settings.led")}>
      <form class="space-y-3" onsubmit={(e) => void save(e)}>
        <Field label={i18n.t("settings.led-enabled")} hint={i18n.t("settings.led-enabled-hint")}>
          <Switch
            label={i18n.t("settings.led-enabled")}
            checked={settings.ledEnabled}
            disabled={busy}
            onChange={(ledEnabled) => {
              if (settings) settings = { ...settings, ledEnabled };
            }}
          />
        </Field>
        <PrimaryButton type="submit" loading={busy}>
          {i18n.t("common.save")}
        </PrimaryButton>
      </form>
    </Panel>

    <Panel title={i18n.t("settings.sound")}>
      <form class="space-y-3" onsubmit={(e) => void save(e)}>
        <Field label={i18n.t("settings.audio-tx")} hint={i18n.t("settings.audio-tx-hint")}>
          <Switch
            label={i18n.t("settings.audio-tx")}
            checked={settings.audioTxEnabled}
            disabled={busy}
            onChange={(audioTxEnabled) => {
              if (settings) settings = { ...settings, audioTxEnabled };
            }}
          />
        </Field>
        {#if settings.audioTxEnabled}
          <Field
            label={i18n.t("settings.audio-volume")}
            hint={i18n.t("settings.audio-volume-hint")}
          >
            <TextInput
              type="number"
              min={AUDIO_VOLUME_MIN}
              max={AUDIO_VOLUME_MAX}
              bind:value={settings.audioTxVolume}
            />
          </Field>
          <div class="grid gap-3 sm:grid-cols-2">
            <Field label={i18n.t("settings.tone-tx-hz")} hint={i18n.t("settings.tone-hz-hint")}>
              <TextInput
                type="number"
                min={AUDIO_TONE_HZ_MIN}
                max={AUDIO_TONE_HZ_MAX}
                bind:value={settings.txHz}
              />
            </Field>
            <Field label={i18n.t("settings.tone-tx-ms")} hint={i18n.t("settings.tone-ms-hint")}>
              <TextInput
                type="number"
                min={AUDIO_TONE_MS_MIN}
                max={AUDIO_TONE_MS_MAX}
                bind:value={settings.txMs}
              />
            </Field>
          </div>
        {/if}
        <Field label={i18n.t("settings.audio-rx")} hint={i18n.t("settings.audio-rx-hint")}>
          <Switch
            label={i18n.t("settings.audio-rx")}
            checked={settings.audioRxEnabled}
            disabled={busy}
            onChange={(audioRxEnabled) => {
              if (settings) settings = { ...settings, audioRxEnabled };
            }}
          />
        </Field>
        {#if settings.audioRxEnabled}
          <Field
            label={i18n.t("settings.audio-volume")}
            hint={i18n.t("settings.audio-volume-hint")}
          >
            <TextInput
              type="number"
              min={AUDIO_VOLUME_MIN}
              max={AUDIO_VOLUME_MAX}
              bind:value={settings.audioRxVolume}
            />
          </Field>
          <div class="grid gap-3 sm:grid-cols-2">
            <Field label={i18n.t("settings.tone-rx-hz")} hint={i18n.t("settings.tone-hz-hint")}>
              <TextInput
                type="number"
                min={AUDIO_TONE_HZ_MIN}
                max={AUDIO_TONE_HZ_MAX}
                bind:value={settings.rxHz}
              />
            </Field>
            <Field label={i18n.t("settings.tone-rx-ms")} hint={i18n.t("settings.tone-ms-hint")}>
              <TextInput
                type="number"
                min={AUDIO_TONE_MS_MIN}
                max={AUDIO_TONE_MS_MAX}
                bind:value={settings.rxMs}
              />
            </Field>
          </div>
        {/if}
        {#if settings.audioTxEnabled || settings.audioRxEnabled}
          <Field label={i18n.t("settings.quiet")} hint={i18n.t("settings.quiet-hint")}>
            <Switch
              label={i18n.t("settings.quiet")}
              checked={quietHoursEnabled}
              disabled={busy}
              onChange={(on) => {
                quietHoursEnabled = on;
                if (on && settings && settings.quietHourStart === settings.quietHourEnd) {
                  settings = { ...settings, quietHourStart: 23, quietHourEnd: 8 };
                }
              }}
            />
          </Field>
          {#if quietHoursEnabled}
            <div class="grid grid-cols-[1fr_auto_1fr] items-center gap-2">
              <div class="rounded-xl border border-border bg-bg px-3 py-2">
                <input
                  type="time"
                  step="3600"
                  value={hourToTimeValue(settings.quietHourStart)}
                  aria-label={i18n.t("settings.quiet-start")}
                  class="w-full bg-transparent text-center text-text tabular-nums outline-none"
                  oninput={(e) => {
                    if (!settings) return;
                    settings = {
                      ...settings,
                      quietHourStart: timeValueToHour(e.currentTarget.value),
                    };
                  }}
                />
              </div>
              <span class="text-sm font-semibold text-muted" aria-hidden="true">–</span>
              <div class="rounded-xl border border-border bg-bg px-3 py-2">
                <input
                  type="time"
                  step="3600"
                  value={hourToTimeValue(settings.quietHourEnd)}
                  aria-label={i18n.t("settings.quiet-end")}
                  class="w-full bg-transparent text-center text-text tabular-nums outline-none"
                  oninput={(e) => {
                    if (!settings) return;
                    settings = {
                      ...settings,
                      quietHourEnd: timeValueToHour(e.currentTarget.value),
                    };
                  }}
                />
              </div>
            </div>
          {/if}
        {/if}
        <PrimaryButton type="submit" loading={busy}>
          {i18n.t("common.save")}
        </PrimaryButton>
      </form>
    </Panel>

    <Panel>
      <div class="space-y-3">
        <DangerButton disabled={busy} onclick={() => (confirmReboot = true)}>
          {i18n.t("settings.reboot")}
        </DangerButton>
        <div class="flex flex-col gap-3 border-t border-border pt-3">
          <h3 class="inline-flex items-center gap-1.5 text-base font-semibold text-text-bright">
            {i18n.t("settings.factory-reset")}
            <InfoTip text={i18n.t("settings.factory-reset-hint")} />
          </h3>
          <DangerButton disabled={busy} onclick={() => (confirmFactory = true)}>
            {i18n.t("settings.factory-reset-confirm")}
          </DangerButton>
        </div>
      </div>
    </Panel>

    <ConfirmDialog
      open={confirmReboot}
      title={i18n.t("settings.reboot-title")}
      description={i18n.t("settings.reboot-text")}
      confirmLabel={i18n.t("settings.reboot-confirm")}
      cancelLabel={i18n.t("common.cancel")}
      confirming={busy}
      onConfirm={() => void reboot()}
      onCancel={() => (confirmReboot = false)}
    />
    <ConfirmDialog
      open={confirmFactory}
      title={i18n.t("settings.factory-reset-title")}
      description={i18n.t("settings.factory-reset-text")}
      confirmLabel={i18n.t("settings.factory-reset-confirm")}
      cancelLabel={i18n.t("common.cancel")}
      confirming={busy}
      onConfirm={() => void factoryReset()}
      onCancel={() => (confirmFactory = false)}
    />
  </div>
{/if}
