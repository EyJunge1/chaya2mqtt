/** Ambient Web Serial API types. https://wicg.github.io/serial/ */

type ParityType = "none" | "even" | "odd";
type FlowControlType = "none" | "hardware";

interface SerialOptions {
  baudRate: number;
  dataBits?: 7 | 8;
  stopBits?: 1 | 2;
  parity?: ParityType;
  bufferSize?: number;
  flowControl?: FlowControlType;
}

interface SerialOutputSignals {
  dataTerminalReady?: boolean;
  requestToSend?: boolean;
  break?: boolean;
}

interface SerialInputSignals {
  dataCarrierDetect: boolean;
  clearToSend: boolean;
  ringIndicator: boolean;
  dataSetReady: boolean;
}

interface SerialPortInfo {
  usbVendorId?: number;
  usbProductId?: number;
  bluetoothServiceClassId?: number | string;
}

declare class SerialPort extends EventTarget {
  onconnect: ((this: this, ev: Event) => void) | null;
  ondisconnect: ((this: this, ev: Event) => void) | null;
  readonly connected: boolean;
  readonly readable: ReadableStream<Uint8Array> | null;
  readonly writable: WritableStream<Uint8Array> | null;

  open(options: SerialOptions): Promise<void>;
  setSignals(signals: SerialOutputSignals): Promise<void>;
  getSignals(): Promise<SerialInputSignals>;
  getInfo(): SerialPortInfo;
  close(): Promise<void>;
  forget(): Promise<void>;
}

interface SerialPortFilter {
  usbVendorId?: number;
  usbProductId?: number;
  bluetoothServiceClassId?: number | string;
}

interface SerialPortRequestOptions {
  filters?: SerialPortFilter[];
  allowedBluetoothServiceClassIds?: Array<number | string>;
}

declare class Serial extends EventTarget {
  onconnect: ((this: this, ev: Event) => void) | null;
  ondisconnect: ((this: this, ev: Event) => void) | null;

  getPorts(): Promise<SerialPort[]>;
  requestPort(options?: SerialPortRequestOptions): Promise<SerialPort>;
}

interface Navigator {
  readonly serial: Serial;
}

interface WorkerNavigator {
  readonly serial: Serial;
}
