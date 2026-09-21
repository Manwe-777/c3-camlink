/*
 * The slice of esptool-js the flash panel uses.
 *
 * It is loaded straight from a CDN by URL rather than installed, because the
 * published page is a single self-contained HTML file with no bundler behind
 * it -- so the import specifier is the URL, and this is the ambient
 * declaration that makes the URL resolvable to the compiler.
 *
 * Pin the version in BOTH places when it changes: the specifier below and the
 * import in usb/flash.ts must name the same build, or the types describe
 * something other than the code that runs.
 */
declare module 'https://cdn.jsdelivr.net/npm/esptool-js@0.5.4/bundle.js' {
  /* The terminal esptool-js writes its progress chatter to. */
  export interface IEspLoaderTerminal {
    clean(): void;
    writeLine(data: string): void;
    write(data: string): void;
  }

  export interface FlashFile {
    /* A binary string -- one character per byte -- not a Uint8Array. */
    data: string;
    address: number;
  }

  export interface FlashOptions {
    fileArray: FlashFile[];
    flashSize: string;
    eraseAll: boolean;
    compress: boolean;
    reportProgress?: (fileIndex: number, written: number, total: number) => void;
  }

  export interface LoaderOptions {
    transport: Transport;
    baudrate: number;
    romBaudrate?: number;
    terminal?: IEspLoaderTerminal;
  }

  export class Transport {
    constructor(port: SerialPort, tracing?: boolean);
    disconnect(): Promise<void>;
  }

  export class ESPLoader {
    constructor(options: LoaderOptions);
    /* Connects and identifies the chip; resolves to its name. */
    main(): Promise<string>;
    writeFlash(options: FlashOptions): Promise<void>;
    /* Post-flash reset behaviour. */
    after(): Promise<void>;
  }
}
