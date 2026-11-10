import { LengthMode, Note } from "../state/types";

const REST = "http://127.0.0.1:8080/api/v1";
const UNITS_PER_BEAT = 8; // 32 units per 4-beat bar

let idCounter = 0;
const nextId = () => `r${++idCounter}`;

export interface ClipInfo {
  key: string;
  deckIndex: number;
  layer: number;
  column: number;
  name: string;
  state: string;
  hasFx: boolean;
  color: number;
  id: number | null;
  notesId: number | null;
  notesValue: string;
  lengthValue: string;
  bars: number;
}

export interface LayerInfo {
  name: string;
  clips: ClipInfo[];
}

export interface CompositionInfo {
  decks: { name: string; selected: boolean }[];
  deckIndex: number;
  layers: LayerInfo[];
  columns: { connected: boolean }[];
  tempo: number;
  playState: string;
}

function isConnected(state: unknown): boolean {
  return typeof state === "string" && state.startsWith("Connected");
}

function parse(data: any): CompositionInfo {
  const tempo = data.tempocontroller?.tempo?.value ?? 120;

  const decks = (data.decks ?? []).map((deck: any) => ({
    name: deck.name?.value ?? "",
    selected: Boolean(deck.selected?.value),
  }));

  const deckIndex = Math.max(0, decks.findIndex((deck: any) => deck.selected));

  const columns = (data.columns ?? []).map((column: any) => ({
    connected: isConnected(column.connected?.value) || Boolean(column.clipconnected?.value),
  }));

  const layers = (data.layers ?? []).map((layer: any, layerIndex: number) => ({
    name: layer.name?.value ?? "",
    clips: (layer.clips ?? []).map((clip: any, clipIndex: number) => {
      const effect = (clip.video?.effects ?? []).find((entry: any) => entry.name === "Midi Roll");
      const params = effect?.params ?? {};

      return {
        // The deck is part of the address: the same layer/column in another
        // deck is a different clip with different notes.
        key: `${deckIndex + 1}/${layerIndex + 1}/${clipIndex + 1}`,
        deckIndex,
        layer: layerIndex + 1,
        column: clipIndex + 1,
        name: clip.name?.value ?? "",
        state: clip.connected?.value ?? "Empty",
        hasFx: Boolean(effect),
        color: Number(clip.colorid?.value ?? 1),
        id: clip.id ?? null,
        notesId: params.Notes?.id ?? null,
        notesValue: params.Notes?.value ?? "",
        lengthValue: params.Grid?.value ?? params.Length?.value ?? "",
        bars: barsOf(clip, tempo),
      };
    }),
  }));

  return {
    decks,
    deckIndex,
    layers,
    columns,
    tempo,
    playState: data.tempocontroller?.play_state?.value ?? "Play",
  };
}

// Resolume stores the clip length in the transport settings. BPM sync clips
// express it in beats, timeline clips in seconds; both are converted to whole
// bars so the editor and the plugin agree on the loop length.
function barsOf(clip: any, tempo: number): number {
  const type = clip.transporttype?.value ?? "";
  const controls = clip.transport?.controls ?? {};
  const duration = Number(controls.duration?.value ?? 0);
  const syncMode = controls.syncmode?.value ?? "";

  let beats = 4;

  if (duration > 0) {
    if (type === "BPM Sync") beats = syncMode === "Beats" ? duration : duration * 4;
    else beats = (duration * tempo) / 60;
  }

  return Math.max(1, Math.min(16, Math.ceil(beats / 4)));
}

export async function fetchComposition(): Promise<CompositionInfo | null> {
  try {
    const response = await fetch(`${REST}/composition`, {
      cache: "no-store",
      signal: AbortSignal.timeout(2500),
    });
    if (!response.ok) return null;
    return parse(await response.json());
  } catch {
    return null;
  }
}

export async function putParam(id: number, value: string): Promise<boolean> {
  try {
    const response = await fetch(`${REST}/parameter/by-id/${id}`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ value }),
    });
    return response.ok;
  } catch {
    // Offline: the local copy stays authoritative until the next edit.
    return false;
  }
}

export async function postAction(path: string): Promise<void> {
  try {
    await fetch(`${REST}${path}`, { method: "POST" });
  } catch {
    // Offline: nothing to trigger.
  }
}

const thumbCache = new Map<number, string>();
const thumbFailed = new Set<number>();

export async function getThumbnail(layer: number, column: number, id: number): Promise<string | null> {
  if (thumbFailed.has(id)) return null;

  const cached = thumbCache.get(id);
  if (cached) return cached;

  try {
    const response = await fetch(`${REST}/composition/layers/${layer}/clips/${column}/thumbnail`);
    if (!response.ok) {
      thumbFailed.add(id);
      return null;
    }

    const url = URL.createObjectURL(await response.blob());
    thumbCache.set(id, url);
    return url;
  } catch {
    thumbFailed.add(id);
    return null;
  }
}

export function clearThumbnailFailures() {
  thumbFailed.clear();
}

export function divisionOf(lengthValue: string): number {
  const value = Number(String(lengthValue).split("/")[1]);
  return [4, 8, 16, 32].includes(value) ? value : 16;
}

function toNote(pitch: number, units: number, lengthUnits: number, velocity: number): Note {
  return {
    id: nextId(),
    pitch,
    start: units / UNITS_PER_BEAT,
    length: Math.max(1 / 32, lengthUnits / UNITS_PER_BEAT),
    velocity,
  };
}

export interface ParsedNotes {
  notes: Note[];
  bars: number;
  loopStart: number; // beats
  loopEnd: number; // beats
  lengthMode: LengthMode;
}

export function notesFromParam(value: string, division: number): ParsedNotes | null {
  const raw = (value || "").trim();
  if (!raw) return null;

  if (raw.startsWith("{")) {
    try {
      const parsed = JSON.parse(raw);
      if (parsed && Array.isArray(parsed.notes)) {
        const bars = Math.max(1, Math.min(16, Number(parsed.bars ?? 1)));
        const clipBeats = bars * 4;
        const loopStart = Math.max(0, Math.min(clipBeats - 1 / 32, Number(parsed.loopStart ?? 0) / UNITS_PER_BEAT));
        const loopEnd = Math.max(
          loopStart + 1 / 32,
          Math.min(clipBeats, Number(parsed.loopEnd ?? bars * 32) / UNITS_PER_BEAT),
        );

        return {
          bars,
          loopStart,
          loopEnd,
          lengthMode: parsed.lengthMode === "manual" ? "manual" : "auto",
          notes: parsed.notes
            .filter((entry: unknown) => Array.isArray(entry) && entry.length >= 4)
            .map((entry: number[]) => toNote(entry[0], entry[1], entry[2], entry[3])),
        };
      }
    } catch {
      return null;
    }
  }

  if (raw.startsWith("[")) {
    try {
      const parsed = JSON.parse(raw);
      if (Array.isArray(parsed)) {
        const scale = 32 / Math.max(division, 4);
        return {
          bars: 1,
          loopStart: 0,
          loopEnd: 4,
          lengthMode: "auto",
          notes: parsed
            .filter((entry: unknown) => Array.isArray(entry) && entry.length >= 4)
            .map((entry: number[]) =>
              toNote(
                entry[0],
                Math.round(entry[1] * scale),
                Math.max(1, Math.round(entry[2] * scale)),
                entry[3],
              ),
            ),
        };
      }
    } catch {
      return null;
    }
  }

  return null;
}

export function notesToParam(
  notes: Note[],
  bars: number,
  loopStart = 0,
  loopEnd = bars * 4,
  lengthMode: LengthMode = "auto",
): string {
  const clamped = Math.max(1, Math.min(16, Math.round(bars)));
  const totalSteps = clamped * 32;
  const start = Math.max(0, Math.min(totalSteps - 1, Math.round(loopStart * UNITS_PER_BEAT)));
  const end = Math.max(start + 1, Math.min(totalSteps, Math.round(loopEnd * UNITS_PER_BEAT)));

  return JSON.stringify({
    res: 32,
    bars: clamped,
    lengthMode,
    loopStart: start,
    loopEnd: end,
    notes: notes.map((note) => [
      note.pitch,
      Math.round(note.start * UNITS_PER_BEAT),
      Math.max(1, Math.round(note.length * UNITS_PER_BEAT)),
      note.velocity,
    ]),
  });
}
