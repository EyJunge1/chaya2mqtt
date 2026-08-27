<script lang="ts">
  import { cn } from "../ui/cn.ts";

  let { class: className = "", children }: { class?: string; children: import("svelte").Snippet } =
    $props();
</script>

<div class={cn("flex flex-col gap-2 sm:flex-row", className)}>
  {@render children()}
</div>
