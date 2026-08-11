import { createContext, useContext } from "react";
import type { ChayaStatus, DeviceInfo, MqttStatus, OtaStatus, WifiStatus } from "../api/types";
import type { ShowToast } from "../components/Toast";

export type LiveState = "connecting" | "live" | "reconnecting";

export type DeviceContextValue = {
  device: DeviceInfo;
  chaya: ChayaStatus;
  wifi: WifiStatus;
  mqtt: MqttStatus;
  ota: OtaStatus | null;
  live: LiveState;
  /** Increments on each explicit refreshDevice() (e.g. mock scenario switch). */
  refreshSeq: number;
  showToast: ShowToast;
  refreshDevice: () => Promise<void>;
};

export const DeviceContext = createContext<DeviceContextValue | null>(null);

export function useDevice(): DeviceContextValue {
  const ctx = useContext(DeviceContext);
  if (!ctx) {
    throw new Error("useDevice must be used within DeviceProvider");
  }
  return ctx;
}
