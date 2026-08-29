<script lang="ts">
  import { ChevronRight, X } from "@lucide/svelte";
  import type { DeviceMode } from "../api/types.ts";
  import { router } from "../nav/router.svelte.ts";
  import { cn } from "../ui/cn.ts";
  import { ACTIVE_ACCENT, HOVER_SURFACE } from "../ui/styles.ts";

  const scenarioGroups = [
    {
      title: "Device",
      items: [
        { id: "sta-connected", label: "STA online", path: "/" },
        { id: "boot-unreachable", label: "Boot unreachable", path: "/" },
        { id: "boot-slow", label: "Boot slow", path: "/" },
        { id: "sse-disconnected", label: "SSE reconnecting", path: "/" },
      ],
    },
    {
      title: "Network",
      items: [
        { id: "offline", label: "STA offline", path: "/" },
        { id: "sta-mqtt-offline", label: "MQTT offline", path: "/" },
        { id: "sta-mqtt-unconfigured", label: "MQTT unconfigured", path: "/mqtt" },
        { id: "sta-mqtt-unpaired", label: "MQTT unpaired", path: "/mqtt" },
      ],
    },
    {
      title: "Wi-Fi / Setup",
      items: [
        { id: "ap-setup", label: "AP Setup", path: "/" },
        { id: "wifi-scan-empty", label: "Scan empty", path: "/" },
        { id: "wifi-scan-fail", label: "Scan failed", path: "/" },
        { id: "ap-test-idle", label: "Test idle", path: "/wifi-testing" },
        { id: "ap-test-testing", label: "Wi-Fi test", path: "/wifi-testing" },
        { id: "ap-test-ok", label: "Wi-Fi test success", path: "/wifi-testing" },
        { id: "ap-test-failed", label: "Wi-Fi test failed", path: "/wifi-testing" },
      ],
    },
    {
      title: "Update",
      items: [
        { id: "update-available", label: "Update ready", path: "/update" },
        { id: "update-checking", label: "Checking", path: "/update" },
        { id: "update-busy", label: "Downloading", path: "/update" },
        { id: "update-verifying", label: "Verifying", path: "/update" },
        { id: "update-rebooting", label: "Rebooting", path: "/update" },
        { id: "update-error", label: "Update error", path: "/update" },
      ],
    },
  ] as const;

  type ScenarioId = (typeof scenarioGroups)[number]["items"][number]["id"];

  const loadFaultItems = [
    { id: "mqtt", label: "MQTT load fail", path: "/mqtt" },
    { id: "settings", label: "Settings load fail", path: "/settings/device" },
    { id: "update-status", label: "Update load fail", path: "/update" },
    { id: "device", label: "Device boot fail", path: "/" },
    { id: "sse", label: "SSE stream fail", path: "/" },
  ] as const;

  const actionFaultItems = [
    { id: "mqtt-save", label: "MQTT save", path: "/mqtt" },
    { id: "settings-save", label: "Settings save", path: "/settings/device" },
    { id: "reboot", label: "Reboot", path: "/settings/device" },
    { id: "factory-reset", label: "Factory reset", path: "/settings/device" },
    { id: "heart", label: "Send heart", path: "/" },
    { id: "wifi-scan", label: "Wi-Fi scan", path: "/wifi" },
    { id: "wifi-connect", label: "Wi-Fi test start", path: "/wifi-testing" },
    { id: "wifi-commit", label: "Wi-Fi test save", path: "/wifi-testing" },
    { id: "wifi-retry", label: "Wi-Fi test retry", path: "/wifi-testing" },
    { id: "update-check", label: "Update check", path: "/update" },
    { id: "update-install", label: "Update install", path: "/update" },
  ] as const;

  type LoadFaultId = (typeof loadFaultItems)[number]["id"];
  type ActionFaultId = (typeof actionFaultItems)[number]["id"];
  type FaultId = LoadFaultId | ActionFaultId;

  type SectionKey =
    (typeof scenarioGroups)[number]["title"] | "Load errors" | "Action errors" | "Open page";

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
    bootError = false,
  }: {
    onChanged: () => Promise<void>;
    mode?: DeviceMode;
    bootError?: boolean;
  } = $props();

  let open = $state(true);
  let busy = $state(false);
  let activeScenario = $state<ScenarioId>("sta-connected");
  let activeFaults = $state<Partial<Record<FaultId, boolean>>>({});
  let openSections = $state<Record<SectionKey, boolean>>({
    Device: false,
    Network: false,
    "Wi-Fi / Setup": false,
    Update: false,
    "Load errors": false,
    "Action errors": false,
    "Open page": false,
  });

  const visiblePages = $derived(
    mode === "ap"
      ? [
          { path: "/", label: "Setup" },
          { path: "/wifi-testing", label: "Testing page" },
        ]
      : pages,
  );

  /** At most one fault selection; when set, scenario buttons are not highlighted. */
  const selectedLoadFault = $derived(
    loadFaultItems.find((item) => activeFaults[item.id])?.id ?? null,
  );
  const selectedActionFault = $derived(
    actionFaultItems.find((item) => activeFaults[item.id])?.id ?? null,
  );
  const scenarioHighlightBlocked = $derived(
    selectedLoadFault !== null || selectedActionFault !== null,
  );

  function toggleSection(key: SectionKey) {
    openSections[key] = !openSections[key];
  }

  $effect(() => {
    if (!import.meta.env.DEV) return;
    void bootError;
    void mode;
    let cancelled = false;
    void (async () => {
      try {
        const res = await fetch("/api/_mock/state");
        if (!res.ok || cancelled) return;
        const data = (await res.json()) as {
          scenario?: ScenarioId;
          faults?: Partial<Record<FaultId, boolean>>;
        };
        if (data.scenario) activeScenario = data.scenario;
        if (data.faults) activeFaults = data.faults;
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
      activeFaults = {};
      router.navigate(scenario.path);
      await onChanged();
    } finally {
      busy = false;
    }
  }

  async function postFault(id: FaultId, enabled: boolean) {
    const response = await fetch("/api/_mock/fault", {
      method: "POST",
      body: new URLSearchParams({ fault: id, enabled: enabled ? "1" : "0" }),
    });
    if (!response.ok) throw new Error(`fault failed (${response.status})`);
    const data = (await response.json()) as { faults?: Partial<Record<FaultId, boolean>> };
    if (data.faults) activeFaults = data.faults;
    else activeFaults = { ...activeFaults, [id]: enabled };
  }

  /** Faults are exclusive with each other and with scenarios. */
  async function selectExclusiveFault(fault: { id: FaultId; path: string }) {
    busy = true;
    try {
      if (activeFaults[fault.id]) {
        await postFault(fault.id, false);
      } else {
        const scenarioBody = new URLSearchParams({ scenario: "sta-connected" });
        const scenarioResponse = await fetch("/api/_mock/scenario", {
          method: "POST",
          body: scenarioBody,
        });
        if (!scenarioResponse.ok) {
          throw new Error(`scenario failed (${scenarioResponse.status})`);
        }
        activeScenario = "sta-connected";
        activeFaults = {};
        await postFault(fault.id, true);
      }
      router.navigate(fault.path);
      await onChanged();
    } finally {
      busy = false;
    }
  }

  async function setLoadFault(fault: { id: LoadFaultId; path: string }) {
    await selectExclusiveFault(fault);
  }

  async function setActionFault(fault: { id: ActionFaultId; path: string }) {
    await selectExclusiveFault(fault);
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
    class="fixed right-3 top-3 z-40 w-[min(16rem,calc(100vw-1.5rem))] rounded-xl border border-border bg-surface/95 p-2 text-xs shadow-lg backdrop-blur"
  >
    <button
      type="button"
      class="flex w-full items-center justify-between px-1 py-0.5 text-left font-semibold text-accent"
      onclick={() => (open = !open)}
    >
      <span>Simulator{bootError ? " · offline" : ""}</span>
      {#if open}
        <X size={14} />
      {:else}
        <span>Open</span>
      {/if}
    </button>
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
                  {@const active = !scenarioHighlightBlocked && activeScenario === scenario.id}
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
          {@render sectionHeader("Load errors", "Load errors")}
          {#if openSections["Load errors"]}
            <div class="ml-2 mt-1" role="group">
              {#each loadFaultItems as fault, index (fault.id)}
                {@const isLast = index === loadFaultItems.length - 1}
                {@const active = selectedLoadFault === fault.id}
                <div class="relative pl-3">
                  {@render treeLines(isLast)}
                  <button
                    type="button"
                    disabled={busy}
                    class={cn(
                      "block w-full rounded-md px-2 py-1.5 text-left disabled:opacity-50",
                      active ? cn(ACTIVE_ACCENT, "font-semibold") : cn("text-muted", HOVER_SURFACE),
                    )}
                    onclick={() => void setLoadFault(fault)}
                  >
                    {fault.label}
                  </button>
                </div>
              {/each}
            </div>
          {/if}
        </section>

        <section class="mt-2 border-t border-border pt-3">
          {@render sectionHeader("Action errors", "Action errors")}
          {#if openSections["Action errors"]}
            <div class="ml-2 mt-1" role="group">
              {#each actionFaultItems as fault, index (fault.id)}
                {@const isLast = index === actionFaultItems.length - 1}
                {@const active = selectedActionFault === fault.id}
                <div class="relative pl-3">
                  {@render treeLines(isLast)}
                  <button
                    type="button"
                    disabled={busy}
                    class={cn(
                      "block w-full rounded-md px-2 py-1.5 text-left disabled:opacity-50",
                      active ? cn(ACTIVE_ACCENT, "font-semibold") : cn("text-muted", HOVER_SURFACE),
                    )}
                    onclick={() => void setActionFault(fault)}
                  >
                    {fault.label}
                  </button>
                </div>
              {/each}
            </div>
          {/if}
        </section>

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
