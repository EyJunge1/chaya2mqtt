<script lang="ts">
  import {
    AlertTriangle,
    CheckCircle2,
    ExternalLink,
    Heart,
    Languages,
    Moon,
    Sun,
    Upload,
    Usb,
  } from "@lucide/svelte";
  import { onMount } from "svelte";
  import FlashDialog from "./FlashDialog.svelte";
  import GithubIcon from "./GithubIcon.svelte";
  import NoPortPickedDialog from "./NoPortPickedDialog.svelte";
  import {
    isSecureWebSerialContext,
    isWebSerialSupported,
    requestSerialPort,
  } from "./flash/webSerial";
  import { detectLanguage, translate, type Lang, type TranslationKey } from "./i18n";

  type Channel = "stable" | "beta";

  type ChannelInfo = {
    channel: Channel;
    tag: string;
    version: string;
    manifest: string;
    factory: string;
  };

  type VersionsResponse = {
    channels: Partial<Record<Channel, ChannelInfo>>;
  };

  const repositoryUrl = "https://github.com/EyJunge1/chaya2mqtt";
  const serialSupported = isWebSerialSupported();
  const serialAllowed = isSecureWebSerialContext();

  let channels: Partial<Record<Channel, ChannelInfo>> = $state({});
  let selected: Channel = $state("stable");
  let loading = $state(true);
  let error = $state("");
  let dark = $state(true);
  let scrolled = $state(false);
  let lang: Lang = $state("en");
  let scrollEl: HTMLElement | undefined = $state();
  let connecting = $state(false);
  let noPortOpen = $state(false);
  let flashOpen = $state(false);
  let flashPort: SerialPort | null = $state(null);
  let portError = $state(false);

  const selectedInfo = $derived(channels[selected] ?? null);
  const stable = $derived(channels.stable ?? null);
  const beta = $derived(channels.beta ?? null);
  const manifestUrl = $derived(
    selectedInfo ? new URL(selectedInfo.manifest, window.location.href).href : "",
  );

  function t(key: TranslationKey, params: Record<string, string | number> = {}): string {
    return translate(lang, key, params);
  }

  function applyLanguage(nextLang: Lang) {
    lang = nextLang;
    document.documentElement.lang = nextLang;
    try {
      localStorage.setItem("chaya2mqtt.lang", nextLang);
    } catch {
      // Language still applies when storage is unavailable.
    }
  }

  function applyTheme(nextDark: boolean) {
    dark = nextDark;
    document.documentElement.dataset.theme = nextDark ? "dark" : "light";
    document.documentElement.style.colorScheme = nextDark ? "dark" : "light";
    try {
      localStorage.setItem("chaya2mqtt.theme", nextDark ? "dark" : "light");
    } catch {
      // Theme still applies when storage is unavailable.
    }
  }

  function closeNoPortDialog() {
    noPortOpen = false;
  }

  function closeFlashDialog() {
    flashOpen = false;
    flashPort = null;
  }

  async function connectAndFlash() {
    if (!selectedInfo || connecting || flashOpen) return;
    connecting = true;
    portError = false;
    try {
      const result = await requestSerialPort();
      if (result === "cancelled") {
        noPortOpen = true;
        return;
      }
      if (result === "error") {
        portError = true;
        return;
      }
      flashPort = result;
      flashOpen = true;
    } finally {
      connecting = false;
    }
  }

  async function loadVersions() {
    loading = true;
    error = "";
    try {
      const response = await fetch(new URL("./versions.json", window.location.href), {
        cache: "no-store",
      });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      const payload = (await response.json()) as VersionsResponse;
      channels = payload.channels;
      if (!payload.channels.stable && payload.channels.beta) {
        selected = "beta";
      }
    } catch (cause) {
      error = cause instanceof Error ? cause.message : "Unknown error";
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    const el = scrollEl;
    if (!el) return;
    const updateScrollState = () => {
      scrolled = el.scrollTop > 8;
    };
    updateScrollState();
    el.addEventListener("scroll", updateScrollState, { passive: true });
    return () => el.removeEventListener("scroll", updateScrollState);
  });

  onMount(() => {
    let stored: string | null = null;
    try {
      stored = localStorage.getItem("chaya2mqtt.theme");
    } catch {
      // Use the dark Chaya default.
    }
    applyTheme(stored ? stored === "dark" : true);
    applyLanguage(detectLanguage());
    void loadVersions();
  });
</script>

<svelte:head>
  <meta name="theme-color" content={dark ? "#0a0a0a" : "#faf6f8"} />
</svelte:head>

<div class="relative flex h-dvh flex-col overflow-hidden bg-bg">
  <div
    class="pointer-events-none absolute -top-32 left-1/2 h-96 w-96 -translate-x-1/2 rounded-full bg-accent/10 blur-3xl"
  ></div>

  <header
    class={[
      "z-50 shrink-0 border-b transition-all duration-300",
      scrolled
        ? "border-border/80 bg-surface/75 shadow-lg shadow-black/10 backdrop-blur-xl"
        : "border-border/80 bg-surface",
    ]}
  >
    <div class="mx-auto flex h-16 max-w-6xl items-center justify-between px-4 sm:px-6">
      <a href="./" class="focus-ring flex items-center gap-3 rounded-xl" aria-label="Chaya2MQTT">
        <span
          class="inline-flex size-10 items-center justify-center rounded-xl bg-accent/15 text-accent"
        >
          <Heart size={19} fill="currentColor" aria-hidden="true" />
        </span>
        <span>
          <span class="block text-base font-bold leading-none text-text-bright">Chaya2MQTT</span>
          <span class="mt-1 block text-[0.68rem] font-semibold tracking-widest text-muted uppercase"
            >Web Flasher</span
          >
        </span>
      </a>

      <div class="flex items-center gap-1">
        <button
          type="button"
          onclick={() => applyLanguage(lang === "de" ? "en" : "de")}
          class="focus-ring inline-flex h-10 w-[4.25rem] shrink-0 items-center justify-center gap-1.5 rounded-xl text-muted transition hover:bg-surface-hover hover:text-text-bright"
          aria-label={t("language")}
          title={`${t("language")}: ${lang.toUpperCase()}`}
        >
          <Languages size={17} class="shrink-0" aria-hidden="true" />
          <span class="w-5 text-center text-xs font-bold tabular-nums">{lang.toUpperCase()}</span>
        </button>
        <button
          type="button"
          onclick={() => applyTheme(!dark)}
          class="focus-ring inline-flex size-10 items-center justify-center rounded-xl text-muted transition hover:bg-surface-hover hover:text-text-bright"
          aria-label={dark ? t("lightTheme") : t("darkTheme")}
          title={dark ? t("lightTheme") : t("darkTheme")}
        >
          {#if dark}
            <Moon size={18} aria-hidden="true" />
          {:else}
            <Sun size={18} aria-hidden="true" />
          {/if}
        </button>
        <a
          href={repositoryUrl}
          target="_blank"
          rel="noopener noreferrer"
          class="focus-ring inline-flex size-10 items-center justify-center rounded-xl text-muted transition hover:bg-surface-hover hover:text-text-bright"
          aria-label="GitHub Repository"
          title="GitHub Repository"
        >
          <GithubIcon size={18} />
        </a>
      </div>
    </div>
  </header>

  <div bind:this={scrollEl} class="relative min-h-0 flex-1 overflow-y-auto overscroll-y-none">
    <div class="flex min-h-full flex-col">
      <main class="relative mx-auto w-full max-w-6xl flex-1 px-4 py-10 sm:px-6 sm:py-16">
        <section class="mx-auto mb-28 max-w-3xl text-center sm:mb-40">
          <h1
            class="text-balance text-5xl font-bold tracking-tight text-text-bright sm:text-7xl sm:leading-tight"
          >
            Chaya2MQTT
          </h1>
        </section>

        <div class="grid items-stretch gap-6 lg:grid-cols-[minmax(0,1.25fr)_minmax(19rem,0.75fr)]">
          <section
            class="overflow-hidden rounded-2xl border border-border bg-surface shadow-elevated"
          >
            <div class="border-b border-border p-5 sm:p-6">
              <div class="flex items-center gap-4">
                <span
                  class="inline-flex size-11 shrink-0 items-center justify-center rounded-xl bg-accent/15 text-accent"
                >
                  <Usb size={21} aria-hidden="true" />
                </span>
                <div>
                  <h2 class="text-2xl font-bold leading-none text-text-bright">
                    {t("installFirmware")}
                  </h2>
                </div>
              </div>
            </div>

            <div class="space-y-6 p-5 sm:p-6">
              <fieldset role="radiogroup" aria-label={t("releaseChannel")}>
                <legend class="mb-3 text-xs font-bold tracking-wider text-muted uppercase">
                  {t("releaseChannel")}
                </legend>
                <div class="grid gap-3 sm:grid-cols-2">
                  <button
                    type="button"
                    role="radio"
                    aria-checked={selected === "stable"}
                    onclick={() => (selected = "stable")}
                    class={[
                      "focus-ring rounded-xl border p-4 text-left transition",
                      selected === "stable"
                        ? "border-accent bg-accent/10"
                        : "border-border hover:border-accent/40 hover:bg-surface-hover",
                    ]}
                  >
                    <span class="flex items-center justify-between gap-3">
                      <span class="font-semibold text-text-bright">{t("stable")}</span>
                      {#if selected === "stable"}
                        <CheckCircle2 size={17} class="text-accent" aria-hidden="true" />
                      {/if}
                    </span>
                    <span class="mt-2 block font-mono text-sm text-muted">
                      {stable?.tag ?? (loading ? t("loading") : t("unavailable"))}
                    </span>
                  </button>

                  <button
                    type="button"
                    role="radio"
                    aria-checked={selected === "beta"}
                    onclick={() => (selected = "beta")}
                    class={[
                      "focus-ring rounded-xl border p-4 text-left transition",
                      selected === "beta"
                        ? "border-accent bg-accent/10"
                        : "border-border hover:border-accent/40 hover:bg-surface-hover",
                    ]}
                  >
                    <span class="flex items-center justify-between gap-3">
                      <span class="font-semibold text-text-bright">{t("beta")}</span>
                      {#if selected === "beta"}
                        <CheckCircle2 size={17} class="text-accent" aria-hidden="true" />
                      {/if}
                    </span>
                    <span class="mt-2 block font-mono text-sm text-muted">
                      {beta?.tag ?? (loading ? t("loading") : t("noBeta"))}
                    </span>
                  </button>
                </div>
              </fieldset>

              {#if error}
                <div
                  class="flex gap-3 rounded-xl border border-danger/30 bg-danger/10 p-4 text-sm text-danger"
                  role="alert"
                >
                  <AlertTriangle size={19} class="mt-0.5 shrink-0" aria-hidden="true" />
                  <div>
                    <p class="font-semibold">{t("firmwareUnavailable")}</p>
                    <p class="mt-1 opacity-90">{t("versionLoadError", { detail: error })}</p>
                    <button
                      type="button"
                      onclick={loadVersions}
                      class="focus-ring mt-2 rounded font-semibold underline"
                    >
                      {t("retry")}
                    </button>
                  </div>
                </div>
              {:else if loading}
                <div
                  class="h-12 animate-pulse rounded-xl bg-surface-hover"
                  aria-label={t("loadingLabel")}
                ></div>
              {:else if !serialSupported}
                <div
                  class="rounded-xl border border-warning/30 bg-warning/10 p-4 text-sm text-warning"
                >
                  {t("unsupported")}
                </div>
              {:else if !serialAllowed}
                <div
                  class="rounded-xl border border-warning/30 bg-warning/10 p-4 text-sm text-warning"
                >
                  {t("secureContext")}
                </div>
              {:else if selectedInfo}
                {#if portError}
                  <div
                    class="flex gap-3 rounded-xl border border-danger/30 bg-danger/10 p-4 text-sm text-danger"
                    role="alert"
                  >
                    <AlertTriangle size={19} class="mt-0.5 shrink-0" aria-hidden="true" />
                    <p>{t("portError")}</p>
                  </div>
                {/if}
                <button
                  type="button"
                  disabled={connecting || flashOpen}
                  onclick={() => void connectAndFlash()}
                  class="focus-ring flex w-full items-center justify-center gap-2 rounded-xl bg-accent px-5 py-4 text-base font-bold text-bg transition hover:opacity-90 disabled:cursor-not-allowed disabled:opacity-60"
                >
                  <Upload size={19} aria-hidden="true" />
                  {t("connectAndFlash")}
                </button>
              {:else}
                <div
                  class="flex gap-3 rounded-xl border border-warning/30 bg-warning/10 p-4 text-sm text-warning"
                  role="status"
                >
                  <AlertTriangle size={19} class="mt-0.5 shrink-0" aria-hidden="true" />
                  <div>
                    <p class="font-semibold">
                      {selected === "beta" ? t("noBetaPublished") : t("noStablePublished")}
                    </p>
                    <p class="mt-1 opacity-90">
                      {t("selectAvailable")}
                    </p>
                  </div>
                </div>
              {/if}
            </div>
          </section>

          <aside class="h-full">
            <section class="h-full rounded-2xl border border-border bg-surface p-5 sm:p-6">
              <h2 class="text-base font-bold text-text-bright">{t("howItWorks")}</h2>
              <ol class="mt-5 space-y-5">
                <li class="flex gap-3">
                  <span
                    class="inline-flex size-7 shrink-0 items-center justify-center rounded-lg bg-accent/15 text-xs font-bold text-accent"
                    >1</span
                  >
                  <div>
                    <p class="text-sm font-semibold text-text-bright">{t("connectUsb")}</p>
                    <p class="mt-1 text-xs leading-5 text-muted">
                      {t("connectUsbHint")}
                    </p>
                  </div>
                </li>
                <li class="flex gap-3">
                  <span
                    class="inline-flex size-7 shrink-0 items-center justify-center rounded-lg bg-accent/15 text-xs font-bold text-accent"
                    >2</span
                  >
                  <div>
                    <p class="text-sm font-semibold text-text-bright">{t("selectPort")}</p>
                    <p class="mt-1 text-xs leading-5 text-muted">
                      {t("selectPortHint")}
                    </p>
                  </div>
                </li>
                <li class="flex gap-3">
                  <span
                    class="inline-flex size-7 shrink-0 items-center justify-center rounded-lg bg-accent/15 text-xs font-bold text-accent"
                    >3</span
                  >
                  <div>
                    <p class="text-sm font-semibold text-text-bright">{t("configure")}</p>
                    <p class="mt-1 text-xs leading-5 text-muted">
                      {t("configureBefore")}
                      <code class="font-mono text-text">Chaya2MQTT</code>
                      {t("configureAfter")}
                      <a href="http://4.3.2.1/" class="focus-ring rounded text-accent underline"
                        >4.3.2.1</a
                      >{t("configureEnd")}
                    </p>
                  </div>
                </li>
              </ol>
            </section>
          </aside>
        </div>
      </main>

      <footer class="relative mt-auto border-t border-border/80 bg-surface/60">
        <div class="mx-auto flex max-w-6xl justify-end px-4 py-6 text-xs text-muted sm:px-6">
          <div class="flex flex-wrap gap-x-4 gap-y-2">
            <a
              href={`${repositoryUrl}/blob/main/docs/README.md`}
              target="_blank"
              rel="noopener noreferrer"
              class="focus-ring inline-flex items-center gap-1 rounded transition hover:text-accent"
            >
              {t("documentation")}
              <ExternalLink size={12} aria-hidden="true" />
            </a>
          </div>
        </div>
      </footer>
    </div>
  </div>

  <NoPortPickedDialog
    open={noPortOpen}
    {lang}
    onRetry={() => {
      closeNoPortDialog();
      void connectAndFlash();
    }}
    onClose={closeNoPortDialog}
  />

  {#if selectedInfo}
    <FlashDialog
      open={flashOpen}
      {lang}
      {manifestUrl}
      versionLabel={selectedInfo.tag}
      eraseDefault={false}
      port={flashPort}
      onClose={closeFlashDialog}
      onRetryPort={() => void connectAndFlash()}
    />
  {/if}
</div>
