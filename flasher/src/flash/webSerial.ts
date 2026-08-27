export function isWebSerialSupported(): boolean {
  return "serial" in navigator;
}

export function isSecureWebSerialContext(): boolean {
  return window.isSecureContext;
}

export async function requestSerialPort(): Promise<SerialPort | "cancelled" | "error"> {
  try {
    return await navigator.serial.requestPort();
  } catch (err) {
    if (err instanceof DOMException && err.name === "NotFoundError") {
      return "cancelled";
    }
    console.error(err);
    return "error";
  }
}
