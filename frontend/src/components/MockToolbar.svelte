<script lang="ts">
  import { ChevronRight, GripVertical, X } from "@lucide/svelte";
  import { untrack } from "svelte";
  import type { DeviceMode } from "../api/types.ts";
  import { router } from "../nav/router.svelte.ts";
  import { cn } from "../ui/cn.ts";
  import { ACTIVE_ACCENT, HOVER_SURFACE } from "../ui/styles.ts";

  const scenarioGroups = [
    {
      title: "Connection",
      items: [
        { id: "sta-connected", label: "STA online", path: "/" },
        { id: "sse-disconnected", label: "SSE reconnecting", path: "/" },
        { id: "device-unreachable", label: "Chaya unreachable", path: "/" },
      ],
    },
    {
      title: "Dashboard",
      items: [
        { id: "battery-full", label: "Battery full", path: "/" },
        { id: "battery-medium", label: "Battery medium", path: "/" },
        { id: "battery-low", label: "Battery low", path: "/" },
        { id: "battery-critical", label: "Battery critical", path: "/" },
        { id: "heart-busy", label: "Busy · tap Send", path: "/" },
        { id: "heart-send-fail", label: "Send failed", path: "/" },
      ],
    },
    {
      title: "MQTT",
      items: [
        { id: "sta-mqtt-offline", label: "Offline", path: "/mqtt" },
        { id: "sta-mqtt-unconfigured", label: "Unconfigured", path: "/mqtt" },
        { id: "sta-mqtt-unpaired", label: "Unpaired", path: "/mqtt" },
        { id: "mqtt-no-auth", label: "No password", path: "/mqtt" },
        { id: "mqtt-load-fail", label: "Load failed", path: "/mqtt" },
        { id: "mqtt-save-fail", label: "Save failed", path: "/mqtt" },
      ],
    },
    {
      title: "Settings",
      items: [
        { id: "settings-load-fail", label: "Load failed", path: "/settings/device" },
        { id: "settings-save-fail", label: "Save failed", path: "/settings/device" },
        { id: "settings-nvs-fail", label: "NVS persist failed", path: "/settings/device" },
        { id: "settings-reboot-fail", label: "Reboot failed", path: "/settings/device" },
        {
          id: "settings-factory-reset-fail",
          label: "Factory reset failed",
          path: "/settings/device",
        },
      ],
    },
    {
      title: "Wi-Fi",
      items: [
        { id: "wifi-weak", label: "Weak signal", path: "/" },
        { id: "wifi-static", label: "Static IP", path: "/wifi" },
        { id: "wifi-sta-save-fail", label: "Save failed", path: "/wifi" },
      ],
    },
    {
      title: "AP setup",
      items: [
        { id: "ap-setup", label: "AP mode", path: "/" },
        { id: "wifi-scan-empty", label: "Scan empty", path: "/" },
        { id: "wifi-scan-fail", label: "Scan failed", path: "/" },
        { id: "ap-test-idle", label: "Test idle", path: "/wifi-testing" },
        { id: "ap-test-testing", label: "Test running", path: "/wifi-testing" },
        { id: "ap-test-ok", label: "Test success", path: "/wifi-testing" },
        { id: "ap-test-failed", label: "Test failed", path: "/wifi-testing" },
        { id: "wifi-test-start-fail", label: "Test start failed", path: "/wifi-testing" },
        { id: "wifi-test-save-fail", label: "Test save failed", path: "/wifi-testing" },
        { id: "wifi-test-retry-fail", label: "Test retry failed", path: "/wifi-testing" },
        { id: "wifi-test-abort-fail", label: "Test abort failed", path: "/wifi-testing" },
      ],
    },
    {
      title: "Update",
      items: [
        { id: "update-uptodate", label: "Up to date", path: "/update" },
        { id: "update-available", label: "Update ready", path: "/update" },
        { id: "update-beta", label: "Beta ready", path: "/update" },
        { id: "update-checking", label: "Checking", path: "/update" },
        { id: "update-busy", label: "Downloading", path: "/update" },
        { id: "update-progress-unknown", label: "Progress unknown", path: "/update" },
        { id: "update-verifying", label: "Verifying", path: "/update" },
        { id: "update-rebooting", label: "Rebooting", path: "/update" },
        { id: "update-error", label: "Error", path: "/update" },
        { id: "update-check-fail", label: "Check failed", path: "/update" },
        { id: "update-install-fail", label: "Install failed", path: "/update" },
        { id: "update-status-fail", label: "Status load failed", path: "/update" },
      ],
    },
  ] as const;

  type ScenarioId = (typeof scenarioGroups)[number]["items"][number]["id"];
  type SectionKey = (typeof scenarioGroups)[number]["title"] | "Open page";

  const POS_KEY = "chaya2mqtt-simulator-pos";
  const MARGIN = 12;

  function readPos(): { x: number; y: number } | null {
    try {
      const raw = localStorage.getItem(POS_KEY);
      if (!raw) return null;
      const parsed = JSON.parse(raw) as { x?: unknown; y?: unknown };
      if (typeof parsed.x === "number" && typeof parsed.y === "number") {
        return { x: parsed.x, y: parsed.y };
      }
    } catch {
      /* ignore */
    }
    return null;
  }

  const pages = [
    { path: "/", label: "Dashboard" },
    { path: "/wifi", label: "Wi-Fi" },
    { path: "/wifi-testing", label: "Testing page" },
    { path: "/mqtt", label: "MQTT" },
    { path: "/settings", label: "Settings" },
    { path: "/settings/device", label: "Device" },
    { path: "/update", label: "Update" },
  ] as const;

  let {
    onChanged,
    mode = "sta",
  }: {
    onChanged: () => Promise<void>;
    mode?: DeviceMode;
  } = $props();

  let open = $state(true);
  let busy = $state(false);
  let panelEl = $state<HTMLDivElement | null>(null);
  let pos = $state<{ x: number; y: number } | null>(
    typeof window !== "undefined" ? readPos() : null,
  );
  let dragging = $state(false);
  let activeScenario = $state<ScenarioId>("sta-connected");
  let openSections = $state<Record<SectionKey, boolean>>({
    Connection: false,
    Dashboard: false,
    MQTT: false,
    Settings: false,
    "Wi-Fi": false,
    "AP setup": false,
    Update: false,
    "Open page": false,
  });

  let dragSession: {
    pointerId: number;
    startX: number;
    startY: number;
    origX: number;
    origY: number;
    moved: boolean;
  } | null = null;

  const visiblePages = $derived(
    mode === "ap"
      ? [
          { path: "/", label: "Setup" },
          { path: "/wifi-testing", label: "Testing page" },
        ]
      : pages,
  );

  function defaultPos(width: number): { x: number; y: number } {
    return {
      x: Math.max(MARGIN, window.innerWidth - width - MARGIN),
      y: MARGIN,
    };
  }

  function clampPos(x: number, y: number, el: HTMLElement): { x: number; y: number } {
    const maxX = Math.max(MARGIN, window.innerWidth - el.offsetWidth - MARGIN);
    const maxY = Math.max(MARGIN, window.innerHeight - el.offsetHeight - MARGIN);
    return {
      x: Math.min(Math.max(MARGIN, x), maxX),
      y: Math.min(Math.max(MARGIN, y), maxY),
    };
  }

  function savePos(next: { x: number; y: number }) {
    try {
      localStorage.setItem(POS_KEY, JSON.stringify(next));
    } catch {
      /* ignore */
    }
  }

  function ensurePos(el: HTMLDivElement) {
    const base = untrack(() => pos) ?? readPos() ?? defaultPos(el.offsetWidth);
    const next = clampPos(base.x, base.y, el);
    const prev = untrack(() => pos);
    if (!prev || prev.x !== next.x || prev.y !== next.y) {
      pos = next;
    }
  }

  function onDragPointerDown(e: PointerEvent) {
    if (e.button !== 0 || !panelEl) return;
    e.preventDefault();
    ensurePos(panelEl);
    const current = untrack(() => pos) ?? defaultPos(panelEl.offsetWidth);
    dragSession = {
      pointerId: e.pointerId,
      startX: e.clientX,
      startY: e.clientY,
      origX: current.x,
      origY: current.y,
      moved: false,
    };
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  }

  function onDragPointerMove(e: PointerEvent) {
    if (!dragSession || dragSession.pointerId !== e.pointerId || !panelEl) return;
    const dx = e.clientX - dragSession.startX;
    const dy = e.clientY - dragSession.startY;
    if (!dragSession.moved && dx * dx + dy * dy < 25) return;
    dragSession.moved = true;
    dragging = true;
    pos = clampPos(dragSession.origX + dx, dragSession.origY + dy, panelEl);
  }

  function onDragPointerUp(e: PointerEvent) {
    if (!dragSession || dragSession.pointerId !== e.pointerId) return;
    dragSession = null;
    dragging = false;
    if (pos) savePos(pos);
  }

  function toggleSection(key: SectionKey) {
    openSections[key] = !openSections[key];
  }

  $effect(() => {
    if (!import.meta.env.DEV || !panelEl) return;
    void open;
    ensurePos(panelEl);
    const onResize = () => {
      const el = panelEl;
      const current = untrack(() => pos);
      if (!el || !current) return;
      const next = clampPos(current.x, current.y, el);
      if (next.x !== current.x || next.y !== current.y) {
        pos = next;
        savePos(next);
      }
    };
    window.addEventListener("resize", onResize);
    return () => window.removeEventListener("resize", onResize);
  });

  $effect(() => {
    if (!import.meta.env.DEV) return;
    void mode;
    let cancelled = false;
    void (async () => {
      try {
        const res = await fetch("/api/_mock/state");
        if (!res.ok || cancelled) return;
        const data = (await res.json()) as { scenario?: ScenarioId };
        if (data.scenario) activeScenario = data.scenario;
      } catch {
        /* ignore sync failures */
      }
    })();
    return () => {
      cancelled = true;
    };
  });

  async function setScenario(scenario: { id: ScenarioId; path: string }) {
    busy = true;
    try {
      const body = new URLSearchParams({ scenario: scenario.id });
      const response = await fetch("/api/_mock/scenario", { method: "POST", body });
      if (!response.ok) throw new Error(`scenario failed (${response.status})`);
      activeScenario = scenario.id;
      router.navigate(scenario.path);
      await onChanged();
    } finally {
      busy = false;
    }
  }
</script>

{#snippet sectionHeader(key: SectionKey, title: string)}
  <button
    type="button"
    class={cn(
      "flex w-full items-center gap-1 rounded-md px-1 py-1 text-left text-xs font-semibold uppercase tracking-wider text-muted",
      HOVER_SURFACE,
    )}
    aria-expanded={openSections[key]}
    onclick={() => toggleSection(key)}
  >
    <ChevronRight
      size={14}
      aria-hidden="true"
      class={cn("shrink-0 transition-transform duration-200", openSections[key] ? "rotate-90" : "")}
    />
    <span>{title}</span>
  </button>
{/snippet}

{#snippet treeLines(isLast: boolean)}
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
{/snippet}

{#if import.meta.env.DEV}
  <div
    bind:this={panelEl}
    class="fixed z-40 w-[min(16rem,calc(100vw-1.5rem))] rounded-xl border border-border bg-surface/95 p-2 text-xs shadow-lg backdrop-blur"
    style:left={pos ? `${pos.x}px` : undefined}
    style:top={pos ? `${pos.y}px` : "0.75rem"}
    style:right={pos ? "auto" : "0.75rem"}
  >
    <div class="flex w-full items-center gap-1">
      <button
        type="button"
        aria-label="Move simulator"
        title="Move simulator"
        class={cn(
          "inline-flex size-7 shrink-0 touch-none items-center justify-center rounded-md text-muted",
          dragging ? "cursor-grabbing" : "cursor-grab",
          HOVER_SURFACE,
        )}
        onpointerdown={onDragPointerDown}
        onpointermove={onDragPointerMove}
        onpointerup={onDragPointerUp}
        onpointercancel={onDragPointerUp}
      >
        <GripVertical size={14} aria-hidden="true" />
      </button>
      <span class="min-w-0 flex-1 truncate px-1 py-0.5 font-semibold text-accent"> Simulator </span>
      <button
        type="button"
        class={cn(
          "inline-flex h-7 shrink-0 items-center justify-center rounded-md px-1.5 text-accent",
          HOVER_SURFACE,
        )}
        aria-expanded={open}
        aria-label={open ? "Close simulator" : "Open simulator"}
        title={open ? "Close" : "Open"}
        onclick={() => (open = !open)}
      >
        {#if open}
          <X size={14} aria-hidden="true" />
        {:else}
          <span class="text-xs font-semibold">Open</span>
        {/if}
      </button>
    </div>
    {#if open}
      <div
        class="mt-2 max-h-[min(75vh,36rem)] space-y-1 overflow-y-auto border-t border-border pt-2"
      >
        {#each scenarioGroups as group, groupIndex (group.title)}
          <section class={cn(groupIndex > 0 && "mt-2 border-t border-border pt-3")}>
            {@render sectionHeader(group.title, group.title)}
            {#if openSections[group.title]}
              <div class="ml-2 mt-1" role="group">
                {#each group.items as scenario, index (scenario.id)}
                  {@const isLast = index === group.items.length - 1}
                  {@const active = activeScenario === scenario.id}
                  <div class="relative pl-3">
                    {@render treeLines(isLast)}
                    <button
                      type="button"
                      disabled={busy}
                      class={cn(
                        "block w-full rounded-md px-2 py-1.5 text-left disabled:opacity-50",
                        active
                          ? cn(ACTIVE_ACCENT, "font-semibold")
                          : cn("text-muted", HOVER_SURFACE),
                      )}
                      onclick={() => void setScenario(scenario)}
                    >
                      {scenario.label}
                    </button>
                  </div>
                {/each}
              </div>
            {/if}
          </section>
        {/each}

        <section class="mt-2 border-t border-border pt-3">
          {@render sectionHeader("Open page", "Open page")}
          {#if openSections["Open page"]}
            <div class="ml-2 mt-1 grid grid-cols-2 gap-1 pl-3" role="group">
              {#each visiblePages as page (page.path)}
                <button
                  type="button"
                  disabled={busy}
                  class={cn(
                    "rounded-md px-2 py-1.5 text-left",
                    router.pathname === page.path ? ACTIVE_ACCENT : cn("text-muted", HOVER_SURFACE),
                  )}
                  onclick={() => router.navigate(page.path)}
                >
                  {page.label}
                </button>
              {/each}
            </div>
          {/if}
        </section>
      </div>
    {/if}
  </div>
{/if}
