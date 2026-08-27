<script lang="ts">
  import { untrack } from "svelte";
  import { api } from "../api/client.ts";
  import { otaHasPendingUpdate } from "../api/ota.ts";
  import type { OtaChannel, OtaPhase, OtaStatus } from "../api/types.ts";
  import ActionRow from "../components/ActionRow.svelte";
  import Alert from "../components/Alert.svelte";
  import ConfirmDialog from "../components/ConfirmDialog.svelte";
  import ErrorBlock from "../components/ErrorBlock.svelte";
  import Field from "../components/Field.svelte";
  import KeyValueGrid from "../components/KeyValueGrid.svelte";
  import LoadingBlock from "../components/LoadingBlock.svelte";
  import Panel from "../components/Panel.svelte";
  import PrimaryButton from "../components/PrimaryButton.svelte";
  import ProgressBar from "../components/ProgressBar.svelte";
  import SecondaryButton from "../components/SecondaryButton.svelte";
  import SelectInput from "../components/SelectInput.svelte";
  import type { ShowToast } from "../components/toastStack.ts";
  import { i18n } from "../i18n/i18n.svelte.ts";

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

  let {
    onToast,
    otaStatus = null,
  }: {
    onToast: ShowToast;
    otaStatus?: OtaStatus | null;
  } = $props();

  let status = $state<OtaStatus | null>(null);
  let channel = $state<OtaChannel>("stable");
  let loadError = $state(false);
  let busy = $state(false);
  let confirmInstall = $state(false);

  function applyStatus(next: OtaStatus) {
    status = next;
    channel = next.channel;
  }

  async function load() {
    loadError = false;
    try {
      applyStatus(await api.getUpdateStatus());
    } catch {
      loadError = true;
      onToast(i18n.t("toast.update-status-failed"), "error");
    }
  }

  $effect(() => {
    untrack(() => {
      void load();
    });
  });

  $effect(() => {
    if (otaStatus) applyStatus(otaStatus);
  });

  const installing = $derived(
    status?.phase === "downloading" ||
      status?.phase === "verifying" ||
      status?.phase === "rebooting",
  );
  const checking = $derived(status?.phase === "checking");
  const pendingUpdate = $derived(otaHasPendingUpdate(status));
  const canInstall = $derived(
    !!status &&
      pendingUpdate &&
      (status.phase === "available" || status.phase === "error") &&
      !installing &&
      !checking,
  );
  const progressPct = $derived(
    !status || status.bytesTotal <= 0
      ? null
      : Math.min(100, Math.round((status.bytesDone / status.bytesTotal) * 100)),
  );

  async function check() {
    busy = true;
    try {
      const res = await api.checkUpdate(channel);
      onToast(
        res.ok ? i18n.t("toast.update-checking") : i18n.t("toast.update-failed"),
        res.ok ? "info" : "error",
      );
      if (res.ok) {
        status = {
          ...(status ?? emptyStatus()),
          phase: "checking",
          channel,
          error: "",
        };
      }
    } catch {
      onToast(i18n.t("toast.update-failed"), "error");
    } finally {
      busy = false;
    }
  }

  async function install() {
    busy = true;
    try {
      const res = await api.installUpdate();
      if (!res.ok) {
        onToast(i18n.t("toast.update-install-failed"), "error");
        confirmInstall = false;
        return;
      }
      onToast(i18n.t("toast.update-installing"), "info");
      confirmInstall = false;
      status = {
        ...(status ?? emptyStatus()),
        phase: "downloading",
        error: "",
      };
    } catch {
      onToast(i18n.t("toast.update-install-failed"), "error");
    } finally {
      busy = false;
    }
  }
</script>

{#if loadError && !status}
  <ErrorBlock
    title={i18n.t("update.load-error-title")}
    message={i18n.t("update.load-error")}
    retryLabel={i18n.t("common.retry")}
    onRetry={() => void load()}
  />
{:else if !status}
  <LoadingBlock label={i18n.t("app.connecting")} />
{:else}
  <Panel title={i18n.t("update.title")} hint={i18n.t("update.text")}>
    <div class="space-y-4">
      <KeyValueGrid
        items={[
          { label: i18n.t("update.installed"), value: status.localVersion?.trim() || "-" },
          {
            label: i18n.t("update.available"),
            value: status.availableVersion || i18n.t("update.none"),
          },
          {
            label: i18n.t("update.status"),
            value:
              status.phase === "available" && !pendingUpdate
                ? i18n.t("update.phase.idle")
                : i18n.t(phaseLabelKey(status.phase)),
            span: 2,
          },
        ]}
      />

      <Field label={i18n.t("update.channel")} hint={i18n.t("update.channel-hint")}>
        <SelectInput bind:value={channel} disabled={busy || checking || installing}>
          <option value="stable">{i18n.t("update.channel.stable")}</option>
          <option value="beta">{i18n.t("update.channel.beta")}</option>
        </SelectInput>
      </Field>

      {#if status.phase === "downloading" || status.phase === "verifying"}
        <div class="space-y-2">
          <ProgressBar value={progressPct} label={i18n.t("update.status")} />
          <p class="text-sm text-muted">
            {progressPct != null
              ? i18n.t("update.progress", { pct: String(progressPct) })
              : i18n.t("update.progress-unknown")}
          </p>
        </div>
      {/if}

      {#if status.phase === "rebooting"}
        <p class="text-sm text-muted">{i18n.t("update.rebooting-hint")}</p>
      {/if}

      {#if status.phase === "error" && status.error}
        <Alert variant="error" title={i18n.t("update.error-title")}>
          {i18n.t("update.error", { code: status.error })}
        </Alert>
      {/if}

      <ActionRow>
        <PrimaryButton
          loading={busy || checking}
          disabled={installing}
          onclick={() => void check()}
          class="sm:flex-1"
        >
          {i18n.t("update.check")}
        </PrimaryButton>
        <SecondaryButton
          disabled={!canInstall || busy}
          onclick={() => (confirmInstall = true)}
          class="sm:flex-1"
        >
          {i18n.t("update.install")}
        </SecondaryButton>
      </ActionRow>
    </div>
  </Panel>

  <ConfirmDialog
    open={confirmInstall}
    title={i18n.t("update.confirm-title")}
    description={i18n.t("update.confirm-text", {
      version: status.availableVersion || "?",
    })}
    confirmLabel={i18n.t("update.install")}
    cancelLabel={i18n.t("common.cancel")}
    confirming={busy}
    confirmVariant="primary"
    onConfirm={() => void install()}
    onCancel={() => (confirmInstall = false)}
  />
{/if}
