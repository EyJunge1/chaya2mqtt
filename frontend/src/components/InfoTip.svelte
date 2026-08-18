<script lang="ts">
  import { Info } from "@lucide/svelte";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { cn } from "../ui/cn.ts";
  import { ACTIVE_ACCENT } from "../ui/styles.ts";

  const HOVER_QUERY = "(hover: hover) and (pointer: fine)";

  let { text, class: className = "" }: { text: string; class?: string } = $props();

  const tipId = $props.id();
  let open = $state(false);
  let rootEl: HTMLSpanElement | undefined = $state();
  let canHover = $state(true);

  function readHoverCapability() {
    if (typeof window.matchMedia !== "function") return true;
    return window.matchMedia(HOVER_QUERY).matches;
  }

  $effect(() => {
    canHover = readHoverCapability();
    if (typeof window.matchMedia !== "function") return;
    const mql = window.matchMedia(HOVER_QUERY);
    const onChange = () => {
      canHover = readHoverCapability();
    };
    mql.addEventListener("change", onChange);
    return () => mql.removeEventListener("change", onChange);
  });

  $effect(() => {
    if (canHover) open = false;
  });

  $effect(() => {
    if (canHover || !open) return;
    const onPointerDown = (event: PointerEvent) => {
      if (rootEl && !rootEl.contains(event.target as Node)) {
        open = false;
      }
    };
    document.addEventListener("pointerdown", onPointerDown);
    return () => document.removeEventListener("pointerdown", onPointerDown);
  });
</script>

<span bind:this={rootEl} class={cn("relative inline-flex w-fit shrink-0", className)}>
  <button
    type="button"
    tabindex={0}
    aria-label={i18n.t("common.info")}
    aria-describedby={tipId}
    aria-expanded={canHover ? undefined : open}
    class={cn(
      "inline-flex size-5 items-center justify-center rounded-full text-muted transition focus-ring",
      canHover ? "cursor-not-allowed" : "cursor-pointer",
      open && ACTIVE_ACCENT,
    )}
    onmouseenter={() => {
      if (canHover) open = true;
    }}
    onmouseleave={() => {
      if (canHover) open = false;
    }}
    onfocus={(event) => {
      if (canHover && event.currentTarget.matches(":focus-visible")) {
        open = true;
      }
    }}
    onblur={() => {
      if (canHover) open = false;
    }}
    onclick={(e) => {
      e.preventDefault();
      e.stopPropagation();
      if (!canHover) open = !open;
    }}
  >
    <Info size={14} strokeWidth={2.25} class="pointer-events-none" aria-hidden="true" />
  </button>
  <span
    id={tipId}
    role="tooltip"
    class={cn(
      "pointer-events-none absolute bottom-full left-1/2 z-20 mb-2 w-max max-w-[16rem] -translate-x-1/2 rounded-lg border border-border bg-surface px-2.5 py-1.5 text-left text-xs font-normal text-text-bright shadow-elevated transition",
      open ? "visible opacity-100" : "invisible opacity-0",
    )}
  >
    {text}
  </span>
</span>
