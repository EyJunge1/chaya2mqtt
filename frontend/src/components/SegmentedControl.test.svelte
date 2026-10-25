<script lang="ts">
  import SegmentedControl, { type SegmentOption } from "./SegmentedControl.svelte";

  type Mode = "dhcp" | "static";

  let { onChange }: { onChange?: (next: Mode) => void } = $props();

  let current = $state<Mode>("dhcp");

  const options: SegmentOption<Mode>[] = [
    { value: "dhcp", label: "DHCP" },
    { value: "static", label: "Statisch" },
  ];
</script>

<SegmentedControl
  value={current}
  {options}
  label="IP"
  onChange={(next) => {
    current = next;
    onChange?.(next);
  }}
/>
