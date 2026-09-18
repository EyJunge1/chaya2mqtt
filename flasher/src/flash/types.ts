export type ChipFamily = "ESP32-S3";

export type FlashManifest = {
  name: string;
  version: string;
  builds: Array<{
    chipFamily: ChipFamily;
    parts: Array<{ path: string; offset: number; sha512?: string }>;
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
