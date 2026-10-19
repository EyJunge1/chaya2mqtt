<script lang="ts">
  import {
    ArrowLeft,
    ChevronLeft,
    ChevronRight,
    Heart,
    PanelLeftClose,
    PanelLeftOpen,
    Settings,
  } from "@lucide/svelte";
  import type { Snippet } from "svelte";
  import type { DeviceMode } from "../api/types.ts";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import type { TranslationKey } from "../i18n/translations.ts";
  import type { IconComponent } from "../nav/settingsNav.ts";
  import { settingsNavItems } from "../nav/settingsNav.ts";
  import { router } from "../nav/router.svelte.ts";
  import type { LiveState } from "../state/device.svelte.ts";
  import { cn } from "../ui/cn.ts";
  import {
    ACTIVE_ACCENT,
    HOVER_ROW,
    HOVER_SURFACE,
    ICON_WELL,
    SURFACE_CARD,
  } from "../ui/styles.ts";
  import Alert from "./Alert.svelte";
  import IconButton from "./IconButton.svelte";
  import Link from "./Link.svelte";
  import ShellUtilityButtons from "./ShellUtilityButtons.svelte";

  const SIDEBAR_COLLAPSED_KEY = "chaya2mqtt-sidebar-collapsed";
  const SETTINGS_OPEN_KEY = "chaya2mqtt-settings-nav-open";
  const CONTENT_SHELL = "mx-auto w-full max-w-3xl px-4 lg:px-6";

  type NavItem = {
    to: string;
    labelKey: TranslationKey;
    icon: IconComponent;
  };

  const homeItem: NavItem = { to: "/", labelKey: "nav.dashboard", icon: Heart };

  const settingsChildren: NavItem[] = settingsNavItems.map(({ to, labelKey, icon }) => ({
    to,
    labelKey,
    icon,
  }));

  const titleKeys: Record<string, TranslationKey> = {
    "/": "nav.dashboard",
    "/wifi": "nav.wifi",
    "/wifi-testing": "nav.wifi-testing",
    "/mqtt": "nav.mqtt",
    "/settings": "nav.settings",
    "/settings/device": "nav.device",
    "/update": "nav.update",
  };

  function isSettingsPath(pathname: string): boolean {
    return pathname === "/settings" || settingsChildren.some((item) => item.to === pathname);
  }

  function isSettingsChildPath(pathname: string): boolean {
    return settingsChildren.some((item) => item.to === pathname);
  }

  function readCollapsed(): boolean {
    try {
      return localStorage.getItem(SIDEBAR_COLLAPSED_KEY) === "1";
    } catch {
      return false;
    }
  }

  function readSettingsOpen(): boolean {
    try {
      return localStorage.getItem(SETTINGS_OPEN_KEY) !== "0";
    } catch {
      return true;
    }
  }

  function navClass(active: boolean, collapsed: boolean): string {
    return cn(
      "flex items-center rounded-xl text-sm font-semibold transition focus-ring",
      collapsed ? "justify-center px-0 py-2.5" : "gap-3 px-3 py-2.5",
      active ? ACTIVE_ACCENT : cn("text-text", HOVER_SURFACE),
    );
  }

  function mobileNavClass(active: boolean): string {
    return cn(
      "flex min-w-0 flex-1 flex-col items-center gap-1 rounded-lg px-1 py-2 text-[0.65rem] font-semibold transition focus-ring",
      active ? "text-accent" : "text-muted hover:text-text-bright",
    );
  }

  function pathActive(to: string, end: boolean): boolean {
    return end
      ? router.pathname === to
      : router.pathname === to || router.pathname.startsWith(`${to}/`);
  }

  let {
    children,
    mode = "sta",
    live = "live",
    updateAvailable = false,
  }: {
    children: Snippet;
    mode?: DeviceMode;
    live?: LiveState;
    updateAvailable?: boolean;
  } = $props();

  const settingsActive = $derived(isSettingsPath(router.pathname));
  const settingsChild = $derived(isSettingsChildPath(router.pathname));
  const hideChrome = $derived(router.pathname === "/wifi-testing");
  const hideSidebar = $derived(hideChrome || mode === "ap");
  const title = $derived(
    mode === "ap" && (router.pathname === "/" || router.pathname === "/wifi")
      ? i18n.t("app.title")
      : i18n.t(titleKeys[router.pathname] ?? "nav.dashboard"),
  );

  let collapsed = $state(readCollapsed());
  let settingsOpen = $state(readSettingsOpen() || isSettingsPath(router.pathname));
  let headerScrolled = $state(false);

  function onContentScroll(event: Event) {
    const target = event.currentTarget;
    if (!(target instanceof HTMLElement)) return;
    headerScrolled = target.scrollTop > 4;
  }

  $effect(() => {
    try {
      localStorage.setItem(SIDEBAR_COLLAPSED_KEY, collapsed ? "1" : "0");
    } catch {
      /* ignore */
    }
  });

  $effect(() => {
    try {
      localStorage.setItem(SETTINGS_OPEN_KEY, settingsOpen ? "1" : "0");
    } catch {
      /* ignore */
    }
  });

  $effect(() => {
    if (settingsActive) settingsOpen = true;
  });

  $effect(() => {
    void router.pathname;
    headerScrolled = false;
  });
</script>

<div class="flex h-dvh overflow-hidden bg-bg">
  {#if !hideSidebar}
    <aside
      class={cn(
        "hidden h-full shrink-0 flex-col overflow-hidden border-r border-border bg-surface transition-[width] duration-200 ease-out lg:flex",
        collapsed ? "w-16" : "w-64",
      )}
    >
      <Link
        href="/"
        class={cn(
          "flex items-center py-4 transition hover:opacity-90 focus-ring",
          collapsed ? "justify-center px-2" : "gap-3 px-4",
        )}
        aria-label={i18n.t("nav.dashboard")}
        title={i18n.t("app.title")}
      >
        <span class={cn(ICON_WELL, "size-11 shrink-0 rounded-xl")}>
          <Heart size={18} fill="currentColor" />
        </span>
        {#if !collapsed}
          <span class="min-w-0 truncate text-xl font-bold text-text-bright">
            {i18n.t("app.title")}
          </span>
        {/if}
      </Link>
      <nav
        class="flex min-h-0 flex-1 flex-col gap-1 overflow-y-auto p-2"
        aria-label={i18n.t("nav.main")}
      >
        {@render NavItemLink(homeItem, collapsed, updateAvailable)}
        <div class="space-y-1">
          <div class="relative">
            {#if collapsed}
              <Link
                href="/settings"
                end
                data-testid="settings-nav-link"
                title={i18n.t("nav.settings")}
                aria-label={i18n.t("nav.settings")}
                class={cn(navClass(pathActive("/settings", true), true), "w-full")}
              >
                <span class="relative inline-flex">
                  <Settings size={18} aria-hidden="true" />
                  {#if updateAvailable}
                    <span
                      class="absolute -top-0.5 -right-0.5 size-2 rounded-full bg-warning"
                      aria-hidden="true"
                    ></span>
                  {/if}
                </span>
              </Link>
            {:else}
              <Link
                href="/settings"
                end
                data-testid="settings-nav-link"
                title={i18n.t("nav.settings")}
                class={cn(navClass(pathActive("/settings", true), false), "w-full pl-11")}
              >
                <span class="flex min-w-0 flex-1 items-center gap-2">
                  <span class="truncate">{i18n.t("nav.settings")}</span>
                  {#if updateAvailable && !settingsOpen}
                    <span class="size-2 shrink-0 rounded-full bg-warning" aria-hidden="true"></span>
                  {/if}
                </span>
                <Settings size={18} aria-hidden="true" class="shrink-0" />
              </Link>
              <IconButton
                type="button"
                size="sm"
                data-testid="settings-nav-toggle"
                onclick={() => (settingsOpen = !settingsOpen)}
                aria-expanded={settingsOpen}
                aria-controls="settings-nav-group"
                aria-label={settingsOpen
                  ? i18n.t("nav.settings-collapse")
                  : i18n.t("nav.settings-expand")}
                class={cn(
                  "absolute top-1/2 left-1 -translate-y-1/2",
                  router.pathname === "/settings" ? "text-accent" : "text-muted",
                )}
              >
                <ChevronRight
                  size={18}
                  aria-hidden="true"
                  class={cn("transition-transform duration-200", settingsOpen ? "rotate-90" : "")}
                />
              </IconButton>
            {/if}
          </div>
          {#if settingsOpen && !collapsed}
            <div id="settings-nav-group" data-testid="settings-nav-group" class="ml-3" role="group">
              {#each settingsChildren as item, index (item.to)}
                {@const isLast = index === settingsChildren.length - 1}
                <div class="relative pl-3">
                  <span
                    aria-hidden="true"
                    class={cn(
                      "pointer-events-none absolute top-0 left-0 w-px bg-border",
                      isLast ? "h-1/2" : "bottom-0",
                    )}
                  ></span>
                  <span
                    aria-hidden="true"
                    class="pointer-events-none absolute top-1/2 left-0 h-px w-3 -translate-y-1/2 bg-border"
                  ></span>
                  {@render NavItemLink(item, false, updateAvailable)}
                </div>
              {/each}
            </div>
          {/if}
        </div>
      </nav>
    </aside>
  {/if}

  <div class="flex min-h-0 min-w-0 flex-1 flex-col overflow-hidden">
    <div
      data-testid="content-scroll"
      class="min-h-0 flex-1 overflow-y-auto"
      onscroll={onContentScroll}
    >
      <header
        data-testid="app-header"
        class={cn(
          "sticky top-0 z-30 shrink-0 border-b border-border pt-[max(0.75rem,env(safe-area-inset-top))] pb-3 transition-[background-color,backdrop-filter] duration-200",
          headerScrolled
            ? "bg-bg/55 backdrop-blur-xl backdrop-saturate-150"
            : "bg-bg/95 backdrop-blur",
        )}
      >
        <div
          data-testid="header-bar"
          class={cn("flex w-full items-center gap-3", hideSidebar ? CONTENT_SHELL : "px-3 lg:px-4")}
        >
          {#if !hideSidebar}
            <div class="hidden lg:contents">
              <IconButton
                type="button"
                data-testid="sidebar-collapse-toggle"
                class="size-10 rounded-xl"
                onclick={() => (collapsed = !collapsed)}
                aria-label={collapsed ? i18n.t("nav.expand") : i18n.t("nav.collapse")}
                title={collapsed ? i18n.t("nav.expand") : i18n.t("nav.collapse")}
                aria-expanded={!collapsed}
              >
                {#if collapsed}
                  <PanelLeftOpen size={18} aria-hidden="true" />
                {:else}
                  <PanelLeftClose size={18} aria-hidden="true" />
                {/if}
              </IconButton>
            </div>
          {/if}
          {#if hideSidebar}
            <span
              data-testid="setup-brand-heart"
              class={cn(ICON_WELL, "size-10 shrink-0 rounded-xl")}
              aria-hidden="true"
            >
              <Heart size={16} fill="currentColor" />
            </span>
          {:else}
            <Link
              href="/"
              data-testid="mobile-brand-home"
              class={cn(ICON_WELL, "size-10 shrink-0 rounded-xl lg:hidden")}
              aria-label={i18n.t("nav.dashboard")}
              title={i18n.t("app.title")}
            >
              <Heart size={16} fill="currentColor" aria-hidden="true" />
            </Link>
          {/if}
          {#if router.pathname === "/wifi-testing"}
            <Link
              href="/"
              data-testid="wifi-test-back"
              class={cn(
                "focus-ring inline-flex size-10 shrink-0 items-center justify-center rounded-xl text-muted transition",
                HOVER_SURFACE,
              )}
              aria-label={i18n.t("nav.back")}
              title={i18n.t("nav.back")}
            >
              <ArrowLeft size={18} aria-hidden="true" />
            </Link>
          {:else if settingsChild}
            <Link
              href="/settings"
              data-testid="settings-back"
              class={cn(
                "focus-ring inline-flex size-10 shrink-0 items-center justify-center rounded-xl text-muted transition lg:hidden",
                HOVER_SURFACE,
              )}
              aria-label={i18n.t("nav.back")}
              title={i18n.t("nav.back")}
            >
              <ArrowLeft size={18} aria-hidden="true" />
            </Link>
          {/if}
          <h1 class="min-w-0 flex-1 truncate text-xl font-bold text-text-bright lg:text-2xl">
            {title}
          </h1>
          <div class="shrink-0">
            <ShellUtilityButtons />
          </div>
        </div>
        {#if live === "reconnecting"}
          <div class={cn("mt-2", hideSidebar ? CONTENT_SHELL : "mx-3 lg:mx-4")}>
            <Alert variant="warning" class="rounded-lg px-3 py-2">
              <span class="font-medium text-warning">{i18n.t("status.live-reconnecting")}</span>
            </Alert>
          </div>
        {/if}
      </header>

      <main
        class={cn(
          CONTENT_SHELL,
          "py-4 lg:pb-8",
          hideSidebar
            ? "pb-[max(1.5rem,env(safe-area-inset-bottom))]"
            : "pb-[max(4.5rem,calc(env(safe-area-inset-bottom)+3.5rem))]",
        )}
      >
        {#if settingsChild && !hideChrome}
          <div class="mb-3 flex items-center gap-2">
            <Link
              href="/settings"
              data-testid="settings-back-link"
              aria-label={i18n.t("nav.back-to-settings")}
              title={i18n.t("nav.back-to-settings")}
              class={cn(
                "inline-flex size-8 shrink-0 items-center justify-center focus-ring",
                SURFACE_CARD,
                HOVER_ROW,
              )}
            >
              <ChevronLeft size={16} aria-hidden="true" />
            </Link>
            <span class="text-sm font-semibold text-text-bright"
              >{i18n.t("nav.back-to-settings")}</span
            >
          </div>
        {/if}
        {@render children()}
      </main>
    </div>

    {#if !hideSidebar}
      <nav
        class="fixed inset-x-0 bottom-0 z-30 border-t border-border bg-surface/95 px-2 pb-[max(0.35rem,env(safe-area-inset-bottom))] pt-1 backdrop-blur lg:hidden"
        aria-label={i18n.t("nav.main")}
      >
        <div class="mx-auto flex max-w-3xl gap-1">
          <Link
            href="/"
            end
            title={i18n.t("nav.dashboard")}
            aria-label={i18n.t("nav.dashboard")}
            class={mobileNavClass(pathActive("/", true))}
          >
            <Heart size={18} aria-hidden="true" />
            <span class="truncate">{i18n.t("nav.dashboard")}</span>
          </Link>
          <Link
            href="/settings"
            end
            title={i18n.t("nav.settings")}
            aria-label={i18n.t("nav.settings")}
            class={mobileNavClass(pathActive("/settings", true) || settingsActive)}
          >
            <span class="relative inline-flex">
              <Settings size={18} aria-hidden="true" />
              {#if updateAvailable}
                <span
                  class="absolute -top-0.5 -right-0.5 size-2 rounded-full bg-warning"
                  aria-hidden="true"
                ></span>
              {/if}
            </span>
            <span class="truncate">{i18n.t("nav.settings")}</span>
          </Link>
        </div>
      </nav>
    {/if}
  </div>
</div>

{#snippet NavItemLink(item: NavItem, itemCollapsed: boolean, itemUpdateAvailable: boolean)}
  {@const Icon = item.icon}
  {@const label = i18n.t(item.labelKey)}
  {@const showDot = itemUpdateAvailable && item.to === "/update"}
  {@const active = pathActive(item.to, item.to === "/")}
  <Link
    href={item.to}
    end={item.to === "/"}
    title={label}
    aria-label={showDot ? `${label} (${i18n.t("dashboard.update-available-title")})` : label}
    class={navClass(active, itemCollapsed)}
  >
    {#if !itemCollapsed}
      <span class="flex min-w-0 flex-1 items-center gap-2">
        <span class="truncate">{label}</span>
        {#if showDot}
          <span class="size-2 shrink-0 rounded-full bg-warning" aria-hidden="true"></span>
        {/if}
      </span>
    {/if}
    <span class="relative inline-flex shrink-0">
      <Icon size={18} aria-hidden="true" />
      {#if showDot && itemCollapsed}
        <span class="absolute -top-0.5 -right-0.5 size-2 rounded-full bg-warning" aria-hidden="true"
        ></span>
      {/if}
    </span>
  </Link>
{/snippet}
