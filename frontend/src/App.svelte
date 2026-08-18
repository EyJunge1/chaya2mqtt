<script lang="ts">
  import { otaHasPendingUpdate } from "./api/ota.ts";
  import Layout from "./components/Layout.svelte";
  import MockToolbar from "./components/MockToolbar.svelte";
  import { KNOWN_ROUTES, router } from "./nav/router.svelte.ts";
  import DashboardPage from "./pages/DashboardPage.svelte";
  import MqttPage from "./pages/MqttPage.svelte";
  import SettingsOverviewPage from "./pages/SettingsOverviewPage.svelte";
  import SettingsPage from "./pages/SettingsPage.svelte";
  import UpdatePage from "./pages/UpdatePage.svelte";
  import WifiPage from "./pages/WifiPage.svelte";
  import WifiTestingPage from "./pages/WifiTestingPage.svelte";
  import { device } from "./state/device.svelte.ts";
  import DeviceRoot from "./state/DeviceRoot.svelte";

  $effect(() => {
    if (router.pathname === "/pairing") {
      router.replace("/mqtt");
    } else if (!KNOWN_ROUTES.has(router.pathname)) {
      router.replace("/");
    }
  });
</script>

<DeviceRoot>
  {#snippet chrome()}
    <MockToolbar
      onChanged={device.reload}
      mode={device.device?.mode}
      bootError={device.bootError}
    />
  {/snippet}

  {#if device.device}
    <Layout
      mode={device.device.mode}
      live={device.live}
      updateAvailable={otaHasPendingUpdate(device.ota)}
    >
      {#if router.pathname === "/"}
        <DashboardPage
          device={device.device}
          chaya={device.chaya}
          wifi={device.wifi}
          ota={device.ota}
          onToast={device.showToast}
        />
      {:else if router.pathname === "/wifi"}
        <WifiPage device={device.device} wifi={device.wifi} onToast={device.showToast} />
      {:else if router.pathname === "/wifi-testing"}
        <WifiTestingPage onToast={device.showToast} />
      {:else if router.pathname === "/mqtt"}
        <MqttPage
          mqtt={device.mqtt}
          refreshSeq={device.refreshSeq}
          onToast={device.showToast}
          onDeviceRefresh={device.refreshDevice}
        />
      {:else if router.pathname === "/settings"}
        <SettingsOverviewPage />
      {:else if router.pathname === "/settings/device"}
        <SettingsPage onToast={device.showToast} onDeviceRefresh={device.refreshDevice} />
      {:else if router.pathname === "/update"}
        <UpdatePage onToast={device.showToast} otaStatus={device.ota} />
      {/if}
    </Layout>
  {/if}
</DeviceRoot>
