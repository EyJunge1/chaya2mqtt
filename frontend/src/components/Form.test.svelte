<script lang="ts">
  import PrimaryButton from "./PrimaryButton.svelte";
  import Switch from "./Switch.svelte";

  let {
    kind,
    loading = false,
    checked = false,
    onChange = () => undefined,
  }: {
    kind: "button" | "switch";
    loading?: boolean;
    checked?: boolean;
    onChange?: (next: boolean) => void;
  } = $props();
</script>

{#if kind === "button"}
  <PrimaryButton {loading} type="button">Speichern</PrimaryButton>
{:else}
  <Switch label="Dark" {checked} {onChange} />
{/if}
