<script lang="ts">
  import { translate, type Lang, type TranslationKey } from "./i18n";

  let {
    open,
    lang,
    onRetry,
    onClose,
  }: {
    open: boolean;
    lang: Lang;
    onRetry?: () => void;
    onClose: () => void;
  } = $props();

  let dialogEl: HTMLDialogElement | undefined = $state();
  const isLinux = $derived(/linux/i.test(navigator.userAgent));

  function t(key: TranslationKey, params: Record<string, string | number> = {}): string {
    return translate(lang, key, params);
  }

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
</script>

<dialog
  bind:this={dialogEl}
  aria-labelledby="no-port-title"
  aria-describedby="no-port-desc"
  oncancel={(e) => {
    e.preventDefault();
    onClose();
  }}
  class="m-auto w-[min(92vw,24rem)] rounded-2xl border border-border bg-surface p-0 text-text shadow-dialog backdrop:bg-black/55 open:flex open:flex-col"
>
  <div class="space-y-2 px-5 pt-5 pb-4">
    <h2 id="no-port-title" class="text-lg font-semibold text-text-bright">
      {t("noPortTitle")}
    </h2>
    <p id="no-port-desc" class="text-sm text-muted">{t("noPortIntro")}</p>
  </div>

  <div
    class="max-h-[min(55vh,24rem)] overflow-y-auto border-t border-border px-5 py-4 text-sm leading-6 text-text"
  >
    <ol class="list-decimal space-y-3 pl-5">
      <li>{t("noPortStepConnected")}</li>
      <li>{t("noPortStepPower")}</li>
      <li>{t("noPortStepCable")}</li>
      {#if isLinux}
        <li>
          {t("noPortStepLinux")}
          <code
            class="mt-2 block rounded-xl border border-border bg-bg px-3 py-2 font-mono text-xs text-text-bright"
            >sudo usermod -a -G dialout $USER</code
          >
          <span class="mt-2 block text-xs text-muted">{t("noPortStepLinuxHint")}</span>
        </li>
      {/if}
      <li>
        {t("noPortStepDrivers")}
        <ul class="mt-2 list-disc space-y-2 pl-5 text-muted">
          <li>
            CP2102:
            <a
              class="focus-ring rounded text-accent underline"
              href="https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers"
              target="_blank"
              rel="noopener noreferrer">{t("noPortDriversWinMac")}</a
            >
          </li>
          <li>
            CH342 / CH343 / CH9102:
            <a
              class="focus-ring rounded text-accent underline"
              href="https://www.wch.cn/downloads/CH343SER_ZIP.html"
              target="_blank"
              rel="noopener noreferrer">Windows</a
            >,
            <a
              class="focus-ring rounded text-accent underline"
              href="https://www.wch.cn/downloads/CH34XSER_MAC_ZIP.html"
              target="_blank"
              rel="noopener noreferrer">Mac</a
            >
          </li>
          <li>
            CH340 / CH341:
            <a
              class="focus-ring rounded text-accent underline"
              href="https://www.wch.cn/downloads/CH341SER_ZIP.html"
              target="_blank"
              rel="noopener noreferrer">Windows</a
            >,
            <a
              class="focus-ring rounded text-accent underline"
              href="https://www.wch.cn/downloads/CH341SER_MAC_ZIP.html"
              target="_blank"
              rel="noopener noreferrer">Mac</a
            >
          </li>
        </ul>
      </li>
    </ol>
  </div>

  <div class="flex flex-col gap-2 border-t border-border px-5 py-4 sm:flex-row-reverse">
    {#if onRetry}
      <button
        type="button"
        onclick={onRetry}
        class="focus-ring w-full rounded-xl bg-accent px-5 py-2.5 text-sm font-bold text-bg transition hover:opacity-90 sm:w-auto sm:min-w-32"
      >
        {t("noPortRetry")}
      </button>
    {/if}
    <button
      type="button"
      onclick={onClose}
      class="focus-ring w-full rounded-xl border border-border bg-surface px-5 py-2.5 text-sm font-semibold text-text-bright transition hover:bg-surface-hover sm:w-auto sm:min-w-32"
    >
      {t("noPortClose")}
    </button>
  </div>
</dialog>
