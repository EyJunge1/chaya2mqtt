import type { ShowToast } from "../components/Toast";
import type { DeviceInfo, WifiStatus } from "../api/types";
import { WifiSetup } from "../components/WifiSetup";

export function WifiPage({
  device,
  wifi,
  onToast,
}: {
  device: DeviceInfo;
  wifi: WifiStatus;
  onToast: ShowToast;
}) {
  return <WifiSetup device={device} wifi={wifi} onToast={onToast} showStatus />;
}
