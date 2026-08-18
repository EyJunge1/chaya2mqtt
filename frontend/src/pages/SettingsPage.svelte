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

  let {
    onToast,
    onDeviceRefresh,
  }: {
    onToast: ShowToast;
    onDeviceRefresh: () => Promise<void>;
  } = $props();

  let settings = $state<SettingsInfo | null>(null);
  let busy = $state(false);
  let loadError = $state(false);
  let confirmReboot = $state(false);
  let confirmFactory = $state(false);

  async function load() {
    loadError = false;
    settings = null;
    try {
      settings = await api.getSettings();
    } catch {
      loadError = true;
      onToast(i18n.t("toast.settings-load-failed"), "error");
    }
  }

  $effect(() => {
    untrack(() => {
      void load();
    });
  });

  async function save(e: SubmitEvent) {
    e.preventDefault();
    if (!settings) return;
    busy = true;
    try {
      const res = await api.saveSettings({
        reset_days: settings.resetDays,
        display_dark: settings.displayDark ? 1 : 0,
      });
      if (!res.ok) {
        onToast(i18n.t("toast.save-failed"), "error");
        return;
      }
      onToast(i18n.t("toast.saved"), "success");
      await onDeviceRefresh();
    } catch {
      onToast(i18n.t("toast.save-failed"), "error");
    } finally {
      busy = false;
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
      if (res.ok) confirmFactory = false;
    } catch {
      onToast(i18n.t("toast.reset-factory-failed"), "error");
    } finally {
      busy = false;
    }
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
        <Field label={i18n.t("settings.display-dark")} hint={i18n.t("settings.display-dark-hint")}>
          <Switch
            label={i18n.t("settings.display-dark")}
            checked={settings.displayDark}
            disabled={busy}
            onChange={(displayDark) => {
              if (settings) settings = { ...settings, displayDark };
            }}
          />
        </Field>
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
          <h3 class="inline-flex items-center gap-1.5 text-sm font-semibold text-text-bright">
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
