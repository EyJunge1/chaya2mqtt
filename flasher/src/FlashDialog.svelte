<script lang="ts">
  import { AlertTriangle, CheckCircle2, LoaderCircle, Usb } from "@lucide/svelte";
  import { onDestroy } from "svelte";
  import { closeSerialPort, flashFirmware } from "./flash/flashFirmware";
  import type { FlashProgress } from "./flash/types";
  import { translate, type Lang, type TranslationKey } from "./i18n";

  type Step = "confirm" | "running" | "done" | "error";

  let {
    open,
    lang,
    manifestUrl,
    versionLabel,
    eraseDefault = true,
    port = null,
    onClose,
    onRetryPort,
  }: {
    open: boolean;
    lang: Lang;
    manifestUrl: string;
    versionLabel: string;
    eraseDefault?: boolean;
    port?: SerialPort | null;
    onClose: () => void;
    onRetryPort: () => void;
  } = $props();

  let dialogEl: HTMLDialogElement | undefined = $state();
  let step = $state<Step>("confirm");
  let eraseFirst = $state(true);
  let progress = $state<FlashProgress | null>(null);
  let activePort: SerialPort | null = $state(null);
  let busy = $state(false);

  function t(key: TranslationKey, params: Record<string, string | number> = {}): string {
    return translate(lang, key, params);
  }

  function progressLabel(current: FlashProgress | null): string {
    if (!current) return t("flash.working");
    switch (current.message) {
      case "initializing":
        return t("flash.phase.initializing");
      case "initialized":
        return t("flash.phase.initialized", { chip: current.chipFamily ?? "?" });
      case "preparing":
        return t("flash.phase.preparing");
      case "prepared":
        return t("flash.phase.prepared");
      case "erasing":
        return t("flash.phase.erasing");
      case "erased":
        return t("flash.phase.erased");
      case "writing":
        return current.percentage != null
          ? t("flash.phase.writingPct", { pct: String(current.percentage) })
          : t("flash.phase.writing");
      case "written":
        return t("flash.phase.written");
      case "finished":
        return t("flash.phase.finished");
      case "init_failed":
        return t("flash.error.init");
      case "unsupported_chip":
        return t("flash.error.unsupported", { chip: current.chipFamily ?? "?" });
      case "download_failed":
        return t("flash.error.download");
      case "erase_failed":
        return t("flash.error.erase");
      case "write_failed":
        return t("flash.error.write");
      default:
        return current.message;
    }
  }

  $effect(() => {
    if (!open) return;
    step = "confirm";
    progress = null;
    busy = false;
    activePort = port;
    eraseFirst = eraseDefault;
  });

  $effect(() => {
    const el = dialogEl;
    if (!el) return;
    if (open && !el.open) {
      if (typeof el.showModal === "function") el.showModal();
      else el.setAttribute("open", "");
    }
    if (!open && el.open) {
      if (typeof el.close === "function") el.close();
      else el.removeAttribute("open");
    }
  });

  async function releasePort() {
    const current = activePort;
    activePort = null;
    await closeSerialPort(current);
  }

  async function handleClose() {
    if (busy) return;
    await releasePort();
    onClose();
  }

  async function startInstall() {
    if (!activePort || busy) return;
    busy = true;
    step = "running";
    progress = { phase: "initializing", message: "initializing", percentage: null };

    await flashFirmware({
      port: activePort,
      manifestPath: manifestUrl,
      eraseFirst,
      onProgress: (next) => {
        progress = next;
        if (next.phase === "finished") {
          step = "done";
          busy = false;
          activePort = null;
        } else if (next.phase === "error") {
          step = "error";
          busy = false;
          activePort = null;
        }
      },
    });

    // Safety if the engine returned without a terminal progress event.
    if (busy) {
      busy = false;
      if (step === "running") {
        step = "error";
        progress = {
          phase: "error",
          message: "write_failed",
          percentage: null,
        };
      }
    }
  }

  async function retryFromError() {
    await releasePort();
    onClose();
    onRetryPort();
  }

  onDestroy(() => {
    void releasePort();
  });
</script>

<dialog
  bind:this={dialogEl}
  aria-labelledby="flash-title"
  oncancel={(e) => {
    e.preventDefault();
    void handleClose();
  }}
  class="m-auto w-[min(92vw,24rem)] rounded-2xl border border-border bg-surface p-0 text-text shadow-dialog backdrop:bg-black/55 open:flex open:flex-col"
>
  <div class="space-y-2 px-5 pt-5 pb-4">
    <div class="flex items-start gap-3">
      <span
        class="mt-0.5 inline-flex size-10 shrink-0 items-center justify-center rounded-xl bg-accent/15 text-accent"
      >
        {#if step === "done"}
          <CheckCircle2 size={18} aria-hidden="true" />
        {:else if step === "error"}
          <AlertTriangle size={18} aria-hidden="true" />
        {:else if step === "running"}
          <LoaderCircle size={18} class="animate-spin" aria-hidden="true" />
        {:else}
          <Usb size={18} aria-hidden="true" />
        {/if}
      </span>
      <div class="min-w-0 flex-1">
        <h2 id="flash-title" class="text-lg font-semibold text-text-bright">
          {#if step === "done"}
            {t("flash.doneTitle")}
          {:else if step === "error"}
            {t("flash.errorTitle")}
          {:else if step === "running"}
            {t("flash.runningTitle")}
          {:else}
            {t("flash.confirmTitle")}
          {/if}
        </h2>
        <p class="mt-1 text-sm text-muted">
          {#if step === "confirm"}
            {t("flash.confirmText", { version: versionLabel })}
          {:else if step === "running"}
            {progressLabel(progress)}
          {:else if step === "done"}
            {t("flash.doneText")}
          {:else}
            {progressLabel(progress)}
          {/if}
        </p>
      </div>
    </div>
  </div>

  <div class="space-y-4 border-t border-border px-5 py-4">
    {#if step === "confirm"}
      <label
        class="flex cursor-pointer items-start gap-3 rounded-xl border border-border bg-bg/60 p-3"
      >
        <input
          type="checkbox"
          bind:checked={eraseFirst}
          class="mt-1 size-4 accent-[var(--ui-accent)]"
        />
        <span>
          <span class="block text-sm font-semibold text-text-bright">{t("flash.eraseLabel")}</span>
          <span class="mt-1 block text-xs leading-5 text-muted">{t("flash.eraseHint")}</span>
        </span>
      </label>
    {:else if step === "running"}
      <div class="space-y-2">
        <div
          class="h-2 overflow-hidden rounded-full bg-border"
          role="progressbar"
          aria-label={t("flash.working")}
          aria-valuemin="0"
          aria-valuemax="100"
          aria-valuenow={progress?.percentage ?? (progress?.phase === "writing" ? 0 : 12)}
        >
          <div
            class="h-full rounded-full bg-accent transition-[width] duration-300"
            style:width={`${progress?.percentage ?? (progress?.phase === "writing" ? 0 : 12)}%`}
          ></div>
        </div>
        <p class="text-xs text-muted">
          {progress?.chipFamily
            ? t("flash.chip", { chip: progress.chipFamily })
            : t("flash.working")}
        </p>
      </div>
    {:else if step === "done"}
      <ol class="list-decimal space-y-2 pl-5 text-sm leading-6 text-text">
        <li>
          {t("flash.nextUsb")}
        </li>
        <li>
          {t("flash.nextWifiBefore")}
          <code class="font-mono text-text-bright">Chaya2MQTT</code>
          {t("flash.nextWifiAfter")}
        </li>
        <li>
          {t("flash.nextOpenBefore")}
          <a href="http://4.3.2.1/" class="focus-ring rounded text-accent underline">4.3.2.1</a>{t(
            "flash.nextOpenAfter",
          )}
        </li>
      </ol>
    {/if}
  </div>

  <div class="flex flex-col gap-2 border-t border-border px-5 py-4 sm:flex-row-reverse">
    {#if step === "confirm"}
      <button
        type="button"
        onclick={() => void startInstall()}
        class="focus-ring w-full rounded-xl bg-accent px-5 py-2.5 text-sm font-bold text-bg transition hover:opacity-90 sm:w-auto sm:min-w-32"
      >
        {t("flash.install")}
      </button>
      <button
        type="button"
        onclick={() => void handleClose()}
        class="focus-ring w-full rounded-xl border border-border bg-surface px-5 py-2.5 text-sm font-semibold text-text-bright transition hover:bg-surface-hover sm:w-auto sm:min-w-32"
      >
        {t("flash.cancel")}
      </button>
    {:else if step === "running"}
      <button
        type="button"
        disabled
        class="focus-ring w-full cursor-not-allowed rounded-xl border border-border px-5 py-2.5 text-sm font-semibold text-muted opacity-70 sm:w-auto sm:min-w-32"
      >
        {t("flash.working")}
      </button>
    {:else if step === "done"}
      <button
        type="button"
        onclick={() => void handleClose()}
        class="focus-ring w-full rounded-xl bg-accent px-5 py-2.5 text-sm font-bold text-bg transition hover:opacity-90 sm:w-auto sm:min-w-32"
      >
        {t("flash.close")}
      </button>
    {:else}
      <button
        type="button"
        onclick={() => void retryFromError()}
        class="focus-ring w-full rounded-xl bg-accent px-5 py-2.5 text-sm font-bold text-bg transition hover:opacity-90 sm:w-auto sm:min-w-32"
      >
        {t("flash.retry")}
      </button>
      <button
        type="button"
        onclick={() => void handleClose()}
        class="focus-ring w-full rounded-xl border border-border bg-surface px-5 py-2.5 text-sm font-semibold text-text-bright transition hover:bg-surface-hover sm:w-auto sm:min-w-32"
      >
        {t("flash.close")}
      </button>
    {/if}
  </div>
</dialog>
