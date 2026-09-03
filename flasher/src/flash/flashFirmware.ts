import { ESPLoader, Transport } from "esptool-js";
import type { FlashManifest, FlashProgress } from "./types";
import {
  isSha256Hex,
  parseSha256SidecarText,
  resolvePartUrl,
  sidecarUrlForPart,
} from "./flashVerify";

const sleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

async function hardReset(transport: Transport, esploader: ESPLoader): Promise<void> {
  await transport.setRTS(true);
  await sleep(100);
  await esploader.after();
}

async function loadManifest(manifestPath: string): Promise<FlashManifest> {
  const url = new URL(manifestPath, window.location.href).href;
  const response = await fetch(url, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  return (await response.json()) as FlashManifest;
}

async function fetchPart(url: string): Promise<Uint8Array> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Downloading firmware failed: ${response.status}`);
  }
  return new Uint8Array(await response.arrayBuffer());
}

async function sha256Hex(data: Uint8Array): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", data.buffer as ArrayBuffer);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

async function loadExpectedSha256(
  partUrl: string,
  manifestSha256: string | undefined,
): Promise<string> {
  if (isSha256Hex(manifestSha256)) {
    return manifestSha256!.toLowerCase();
  }
  const sidecarUrl = sidecarUrlForPart(partUrl);
  const response = await fetch(sidecarUrl, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`Missing firmware SHA-256 sidecar (${response.status})`);
  }
  const parsed = parseSha256SidecarText(await response.text());
  if (!parsed) {
    throw new Error("Invalid firmware SHA-256 sidecar");
  }
  return parsed;
}

/**
 * Flash a factory image from an ESP Web Tools-compatible manifest over Web Serial.
 */
export async function flashFirmware(options: {
  port: SerialPort;
  manifestPath: string;
  eraseFirst: boolean;
  onProgress: (progress: FlashProgress) => void;
}): Promise<void> {
  const { port, manifestPath, eraseFirst, onProgress } = options;
  const emit = (progress: FlashProgress) => onProgress(progress);

  const manifest = await loadManifest(manifestPath);
  const transport = new Transport(port);
  const esploader = new ESPLoader({
    transport,
    baudrate: 115200,
    enableTracing: false,
  });

  let chipFamily = "Unknown";

  const fail = async (message: string) => {
    emit({ phase: "error", message, percentage: null, chipFamily });
    try {
      await hardReset(transport, esploader);
    } catch {
      // Best-effort reset after failure.
    }
    try {
      await transport.disconnect();
    } catch {
      // Port may already be closed.
    }
  };

  emit({
    phase: "initializing",
    message: "initializing",
    percentage: null,
  });

  try {
    await esploader.main();
    await esploader.flashId();
  } catch (err) {
    console.error(err);
    await fail("init_failed");
    return;
  }

  chipFamily = esploader.chip.CHIP_NAME;
  emit({
    phase: "initializing",
    message: "initialized",
    percentage: null,
    chipFamily,
  });

  const portInfo = port.getInfo();
  const isCdcUsbPort =
    portInfo.usbVendorId === 0x303a &&
    portInfo.usbProductId !== undefined &&
    [0x1001, 0x1002, 0x1003, 0x0002, 0x0003].includes(portInfo.usbProductId);
  const detectedSerialType = isCdcUsbPort ? "cdc" : "uart";

  const build =
    manifest.builds.find(
      (candidate) =>
        candidate.chipFamily === chipFamily && candidate.serialType === detectedSerialType,
    ) ??
    manifest.builds.find(
      (candidate) => candidate.chipFamily === chipFamily && candidate.serialType === undefined,
    );

  if (!build) {
    await fail("unsupported_chip");
    return;
  }

  emit({
    phase: "preparing",
    message: "preparing",
    percentage: null,
    chipFamily,
  });

  const manifestUrl = new URL(manifestPath, window.location.href);
  const fileArray: Array<{ data: Uint8Array; address: number }> = [];
  let totalSize = 0;

  try {
    for (const part of build.parts) {
      const partUrl = resolvePartUrl(part.path, manifestUrl.href);
      const data = await fetchPart(partUrl);
      const expected = await loadExpectedSha256(partUrl, part.sha256);
      const actual = await sha256Hex(data);
      if (actual !== expected) {
        throw new Error(`Firmware SHA-256 mismatch for ${part.path}`);
      }
      fileArray.push({ data, address: part.offset });
      totalSize += data.length;
    }
  } catch (err) {
    console.error(err);
    const message = err instanceof Error ? err.message : String(err);
    if (/SHA-256 mismatch/i.test(message)) {
      await fail("hash_mismatch");
    } else if (/SHA-256|sidecar/i.test(message)) {
      await fail("hash_missing");
    } else {
      await fail("download_failed");
    }
    return;
  }

  emit({
    phase: "preparing",
    message: "prepared",
    percentage: null,
    chipFamily,
  });

  if (eraseFirst) {
    emit({
      phase: "erasing",
      message: "erasing",
      percentage: null,
      chipFamily,
    });
    try {
      await esploader.eraseFlash();
    } catch (err) {
      console.error(err);
      await fail("erase_failed");
      return;
    }
    emit({
      phase: "erasing",
      message: "erased",
      percentage: null,
      chipFamily,
    });
  }

  emit({
    phase: "writing",
    message: "writing",
    percentage: 0,
    chipFamily,
  });

  let totalWritten = 0;
  try {
    await esploader.writeFlash({
      fileArray,
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: false,
      compress: true,
      reportProgress: (fileIndex, written, total) => {
        const uncompressedWritten = (written / total) * fileArray[fileIndex].data.length;
        const percentage = Math.min(
          100,
          Math.floor(((totalWritten + uncompressedWritten) / totalSize) * 100),
        );
        if (written === total) {
          totalWritten += uncompressedWritten;
          return;
        }
        emit({
          phase: "writing",
          message: "writing",
          percentage,
          chipFamily,
        });
      },
    });
  } catch (err) {
    console.error(err);
    await fail("write_failed");
    return;
  }

  emit({
    phase: "writing",
    message: "written",
    percentage: 100,
    chipFamily,
  });

  try {
    await hardReset(transport, esploader);
    await transport.disconnect();
  } catch (err) {
    console.error(err);
  }

  emit({
    phase: "finished",
    message: "finished",
    percentage: 100,
    chipFamily,
  });
}

export async function closeSerialPort(port: SerialPort | null | undefined): Promise<void> {
  if (!port) return;
  try {
    if (port.readable || port.writable) {
      await port.close();
    }
  } catch {
    // Already closed by esptool transport.
  }
}
