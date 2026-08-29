<script lang="ts">
  import { cn } from "../ui/cn.ts";
  import ShellUtilityButtons from "./ShellUtilityButtons.svelte";

  let { collapsed = false }: { collapsed?: boolean } = $props();
</script>

<div class={cn("mt-auto", collapsed ? "p-2" : "p-3")}>
  <ShellUtilityButtons stacked={collapsed} class={collapsed ? "" : "w-full justify-center"} />
</div>
