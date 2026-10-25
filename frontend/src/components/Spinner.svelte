<script lang="ts">
  import { LoaderCircle } from "@lucide/svelte";
  import { cn } from "../ui/cn.ts";

  let { size = 18, class: className = "" }: { size?: number; class?: string } = $props();
</script>

<LoaderCircle {size} class={cn("animate-spin text-accent", className)} aria-hidden="true" />
