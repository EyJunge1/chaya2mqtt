<script lang="ts">
  import { otaHasPendingUpdate } from "./api/ota.ts";
  import Layout from "./components/Layout.svelte";
  import MockToolbar from "./components/MockToolbar.svelte";
  import { isKnownRoute, router } from "./nav/router.svelte.ts";
  import DashboardPage from "./pages/DashboardPage.svelte";
  import MqttPage from "./pages/MqttPage.svelte";
  import SettingsOverviewPage from "./pages/SettingsOverviewPage.svelte";
  import SettingsPage from "./pages/SettingsPage.svelte";
  import UpdatePage from "./pages/UpdatePage.svelte";
  import WifiPage from "./pages/WifiPage.svelte";
  import WifiTestingPage from "./pages/WifiTestingPage.svelte";
  import { device } from "./state/device.svelte.ts";
  import DeviceRoot from "./state/DeviceRoot.svelte";

  const renderedPath = $derived(
    device.device?.mode === "ap" && router.pathname !== "/wifi-testing" ? "/" : router.pathname,
  );

  $effect(() => {
    if (
      device.device?.mode === "ap" &&
      router.pathname !== "/" &&
      router.pathname !== "/wifi-testing"
    ) {
      router.replace("/");
    } else if (!isKnownRoute(router.pathname)) {
      router.replace("/");
    }
  });
</script>

<DeviceRoot>
  {#snippet chrome()}
    <MockToolbar onChanged={device.boot} mode={device.device?.mode} />
  {/snippet}

  {#if device.device}
    <Layout
      mode={device.device.mode}
      live={device.live}
      updateAvailable={otaHasPendingUpdate(device.ota)}
    >
      {#if renderedPath === "/"}
        <DashboardPage
          device={device.device}
          chaya={device.chaya}
          wifi={device.wifi}
          ota={device.ota}
          onToast={device.showToast}
        />
      {:else if renderedPath === "/wifi"}
        <WifiPage device={device.device} wifi={device.wifi} onToast={device.showToast} />
      {:else if renderedPath === "/wifi-testing"}
        <WifiTestingPage onToast={device.showToast} />
      {:else if renderedPath === "/mqtt"}
        <MqttPage
          mqtt={device.mqtt}
          refreshSeq={device.refreshSeq}
          onToast={device.showToast}
          onDeviceRefresh={device.refreshDevice}
        />
      {:else if renderedPath === "/settings"}
        <SettingsOverviewPage />
      {:else if renderedPath === "/settings/device"}
        <SettingsPage onToast={device.showToast} onDeviceRefresh={device.refreshDevice} />
      {:else if renderedPath === "/update"}
        <UpdatePage onToast={device.showToast} otaStatus={device.ota} />
      {/if}
    </Layout>
  {/if}
</DeviceRoot>
