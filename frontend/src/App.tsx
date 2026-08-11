import { Navigate, Route, Routes } from "react-router-dom";
import { Layout } from "./components/Layout";
import { MockToolbar } from "./components/MockToolbar";
import { DashboardPage } from "./pages/DashboardPage";
import { MqttPage } from "./pages/MqttPage";
import { SettingsOverviewPage } from "./pages/SettingsOverviewPage";
import { SettingsPage } from "./pages/SettingsPage";
import { UpdatePage } from "./pages/UpdatePage";
import { WifiPage } from "./pages/WifiPage";
import { WifiTestingPage } from "./pages/WifiTestingPage";
import { otaHasPendingUpdate } from "./api/ota";
import { DeviceProvider } from "./state/DeviceProvider";
import { useDevice } from "./state/deviceContext";

function AppShell() {
  const { device, chaya, wifi, mqtt, ota, live, refreshSeq, showToast, refreshDevice } =
    useDevice();

  return (
    <Layout mode={device.mode} live={live} updateAvailable={otaHasPendingUpdate(ota)}>
      <Routes>
        <Route
          path="/"
          element={
            <DashboardPage
              device={device}
              chaya={chaya}
              wifi={wifi}
              ota={ota}
              onToast={showToast}
            />
          }
        />
        <Route
          path="/wifi"
          element={<WifiPage device={device} wifi={wifi} onToast={showToast} />}
        />
        <Route path="/wifi-testing" element={<WifiTestingPage onToast={showToast} />} />
        <Route
          path="/mqtt"
          element={
            <MqttPage
              mqtt={mqtt}
              refreshSeq={refreshSeq}
              onToast={showToast}
              onDeviceRefresh={refreshDevice}
            />
          }
        />
        <Route path="/pairing" element={<Navigate to="/mqtt" replace />} />
        <Route path="/settings" element={<SettingsOverviewPage />} />
        <Route
          path="/settings/device"
          element={<SettingsPage onToast={showToast} onDeviceRefresh={refreshDevice} />}
        />
        <Route path="/update" element={<UpdatePage onToast={showToast} otaStatus={ota} />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Routes>
    </Layout>
  );
}

export default function App() {
  return (
    <DeviceProvider
      chrome={(api) => (
        <MockToolbar onChanged={api.reload} mode={api.mode} bootError={api.bootError} />
      )}
    >
      <AppShell />
    </DeviceProvider>
  );
}
