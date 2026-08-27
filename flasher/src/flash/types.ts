export type ChipFamily =
  | "ESP32"
  | "ESP32-C2"
  | "ESP32-C3"
  | "ESP32-C5"
  | "ESP32-C6"
  | "ESP32-C61"
  | "ESP32-H2"
  | "ESP32-P4"
  | "ESP32-S2"
  | "ESP32-S3"
  | "ESP8266";

export type FlashManifest = {
  name: string;
  version: string;
  new_install_prompt_erase?: boolean;
  builds: Array<{
    chipFamily: ChipFamily;
    parts: Array<{ path: string; offset: number }>;
    serialType?: "cdc" | "uart";
  }>;
};

export type FlashPhase =
  "initializing" | "preparing" | "erasing" | "writing" | "finished" | "error";

export type FlashProgress = {
  phase: FlashPhase;
  message: string;
  percentage: number | null;
  chipFamily?: string;
};
