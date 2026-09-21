/*
 * The contract between the page and the firmware.
 *
 * This file has no import or export, which is what keeps it AMBIENT: every
 * declaration below is global, so shared/app.ts and usb/transport.ts can both
 * see it while remaining plain scripts rather than ES modules. That matters
 * because tools/build_web.py inlines them into a bare <script> tag, and a
 * module emit would arrive with import statements a bare script cannot run.
 *
 * The shapes mirror webcfg_state_json() and webcfg_scan_json() in
 * components/webcfg/webcfg.c. They are hand-written rather than generated, so
 * adding a field to the firmware's JSON means adding it here too.
 */

/* Every endpoint the shared page may reach.
 *
 * THIS UNION IS LOAD-BEARING. The shared page reaches an endpoint by name; the
 * USB transport has to recognise that name to turn it into a serial verb. A
 * name in one and not the other works perfectly over Wi-Fi and fails silently
 * over the cable -- which is how switching saved cameras was dead on USB for
 * several versions with nobody noticing.
 *
 * usb/transport.ts types its verb table as Record<Endpoint, string>, so a
 * missing entry is a compile error rather than a silent runtime hole. Add an
 * endpoint here and the transport stops compiling until it is handled.
 *
 * /api/update is deliberately absent: it is a raw XMLHttpRequest upload rather
 * than an api() call, because fetch has no upload progress events.
 */
type Endpoint =
  | 'state'
  | 'scan'
  | 'config'
  | 'osd'
  | 'bind'
  | 'profile'
  | 'forget'
  | 'repair'
  | 'exit';

/* The persisted settings. Mirrors camlink_cfg_t as the page sees it: every
 * value here is a number because it arrives from a form or a JSON integer. */
interface Cfg {
  mode: number;       /* 0 arm, 1 switch, 2 cut, 3 both                     */
  aux: number;        /* 1..14, the record channel                          */
  min: number;        /* switch window, microseconds                        */
  max: number;
  tx: number;         /* 0..3, the transmit rung                            */
  kind: number;       /* 0 switch, 1 button                                 */
  txauto: number;     /* 1 = turn the radio down while armed                */
  cfgaux: number;     /* 0 = off, else the setup channel                    */
  cfgkind: number;    /* 0 switch, 1 button                                 */
  cfghold: number;    /* hold time, tenths of a second                      */
  hilight: number;    /* 0 = off, else the HiLight channel                  */
  stopdelay: number;  /* seconds to keep recording after an automatic stop   */
}

/* The bound camera, and why its link is down when it is. */
interface CamState {
  bound: boolean;
  mac: string;        /* colon-separated, for display                       */
  fault: string;      /* camlink_ble_fault_text(); '' when there is none     */
  fails: number;
  everup: boolean;    /* has it ever connected since power-up                */
}

/* What the flight controller is reporting over MSP. */
interface FcState {
  link: boolean;
  armed: boolean;
  n: number;          /* how many RC channels are present in `rc`            */
  rc: number[];       /* every channel the FC sends, never a prefix          */
}

interface Profile {
  label: string;
  mac: string;
}

interface Profiles {
  active: number;     /* index into `list`, or -1                            */
  list: Profile[];
}

/* One OSD field, straight from osd_fields[] in the firmware. The width is the
 * renderer's worst case and is what the row budget is computed from, which is
 * why it is sent rather than duplicated in the page. */
interface OsdField {
  id: number;
  name: string;
  ex: string;         /* example rendering, for the preview                  */
  w: number;          /* worst-case width in characters                      */
}

/* GET /api/state */
interface State {
  ssid: string;
  fw: string;
  cfg: Cfg;
  cam: CamState;
  fc: FcState;
  profiles?: Profiles;
  osdmax: number;
  fields: OsdField[];
  osd: number[][];    /* OSD_ROWS x OSD_ROW_FIELDS, 0 meaning empty          */
}

/* One camera the scanner can currently hear. */
interface Cam {
  mac: string;        /* colon-separated, for display                        */
  id: string;         /* twelve hex digits, the form the firmware parses     */
  name: string;
  model: string;
  vendor: 'gopro' | 'dji';
  rssi: number;
  bound: boolean;
  note: string;       /* advisory only; never a reason to refuse a tap       */
}

/* GET /api/scan */
interface ScanResult {
  cams: Cam[];
}

interface Window {
  /* Optional transport hook. The USB tool defines this to put the board's
   * version in its connection banner; the Wi-Fi page has nowhere to show it
   * and leaves it undefined. The shared page calls it if it exists. */
  camlinkOnState?: (s: State) => void;
  /* The last scan result, read by the link-fault hint to say whether the bound
   * camera is being heard at all. */
  _lastCams?: Cam[];
  /* The Web Serial link, exposed for the connection badge to poll. */
  _board?: { connected: boolean };
}
