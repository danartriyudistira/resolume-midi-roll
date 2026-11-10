import { useSyncExternalStore } from "react";
import { EditorState, LengthMode, Note, Tool } from "./types";

type Listener = () => void;

// One undo entry. Loop points and clip length live outside the note list, so
// they travel with it — otherwise Ctrl+Z would restore notes into a timeline
// of a different length.
interface Snapshot {
  notes: Note[];
  bars: number;
  loopStart: number;
  loopEnd: number;
  lengthMode: LengthMode;
}

let counter = 0;
export const makeId = () => `n${++counter}`;

const MIN_BEAT = 1 / 32;
const MAX_BARS = 16;

function clampBars(bars: number): number {
  return Math.max(1, Math.min(MAX_BARS, Math.round(bars)));
}

function clampLoop(loopStart: number, loopEnd: number, clipBeats: number) {
  const total = Math.max(MIN_BEAT, clipBeats);
  const start = Math.max(0, Math.min(total - MIN_BEAT, loopStart));
  const end = Math.max(start + MIN_BEAT, Math.min(total, loopEnd));

  return { start, end };
}

// Time-stretches a pattern to a new clip length. 1/32 beat resolution keeps the
// result on the sequencer's grid, and notes never spill past the new end.
export function stretchNotes(notes: Note[], fromBars: number, toBars: number): Note[] {
  const fromBeats = Math.max(1, fromBars * 4);
  const toBeats = Math.max(1, toBars * 4);
  const scale = toBeats / fromBeats;

  if (Math.abs(scale - 1) < 1e-6) return notes;

  const quantize = (value: number) => Math.round(value * 32) / 32;

  return notes.map((note) => {
    const start = Math.max(0, quantize(note.start * scale));
    const length = Math.max(1 / 32, quantize(note.length * scale));

    return { ...note, start, length };
  });
}

class EditorStore {
  private state: EditorState;
  private listeners = new Set<Listener>();
  private undoStack: Snapshot[] = [];
  private redoStack: Snapshot[] = [];
  private txBase: Snapshot | null = null;

  constructor() {
    this.state = {
      notes: [],
      selection: [],
      grid: 16,
      fold: false,
      scaleRoot: null,
      scaleType: "Major",
      tool: "select",
      bars: 1,
      clipBeats: 4,
      loopStart: 0,
      loopEnd: 4,
      lengthMode: "auto",
    };
  }

  subscribe = (fn: Listener) => {
    this.listeners.add(fn);
    return () => {
      this.listeners.delete(fn);
    };
  };

  getSnapshot = (): EditorState => this.state;

  private emit() {
    this.listeners.forEach((fn) => fn());
  }

  private set(patch: Partial<EditorState>) {
    this.state = { ...this.state, ...patch };
    this.emit();
  }

  private snapshot(): Snapshot {
    const { notes, bars, loopStart, loopEnd, lengthMode } = this.state;
    return { notes, bars, loopStart, loopEnd, lengthMode };
  }

  private restore(snapshot: Snapshot) {
    this.state = {
      ...this.state,
      notes: snapshot.notes,
      bars: snapshot.bars,
      clipBeats: snapshot.bars * 4,
      loopStart: snapshot.loopStart,
      loopEnd: snapshot.loopEnd,
      lengthMode: snapshot.lengthMode,
      selection: [],
    };
  }

  setGrid(grid: number) {
    this.set({ grid });
  }

  setFold(fold: boolean) {
    this.set({ fold });
  }

  setScale(root: number | null) {
    this.set({ scaleRoot: root });
  }

  setScaleType(type: string) {
    this.set({ scaleType: type });
  }

  setTool(tool: Tool) {
    this.set({ tool });
  }

  select(ids: string[]) {
    this.set({ selection: ids });
  }

  // Drag transactions: one undo entry per gesture, not per frame.
  begin() {
    this.txBase = this.snapshot();
  }

  live(notes: Note[]) {
    this.state = { ...this.state, notes };
    this.emit();
  }

  commit() {
    if (this.txBase === null) return;

    const base = this.txBase;
    const current = this.state;

    if (
      base.notes !== current.notes ||
      base.bars !== current.bars ||
      base.loopStart !== current.loopStart ||
      base.loopEnd !== current.loopEnd ||
      base.lengthMode !== current.lengthMode
    ) {
      this.pushUndo(base);
      this.redoStack = [];
    }

    this.txBase = null;
    this.emit();
  }

  load(
    notes: Note[],
    bars = 1,
    loopStart = 0,
    loopEnd = bars * 4,
    lengthMode: LengthMode = "auto",
  ) {
    const clamped = clampBars(bars);
    const clipBeats = clamped * 4;
    const loop = clampLoop(loopStart, loopEnd, clipBeats);

    this.undoStack = [];
    this.redoStack = [];
    this.txBase = null;
    this.state = {
      ...this.state,
      notes,
      selection: [],
      bars: clamped,
      clipBeats,
      loopStart: loop.start,
      loopEnd: loop.end,
      lengthMode,
    };
    this.emit();
  }

  // A clip's beat setting changed in Resolume while length follows the host:
  // keep the pattern in step by time-stretching the notes.
  resizeClip(bars: number) {
    const clamped = clampBars(bars);
    if (clamped === this.state.bars) return;

    const wasFull = this.state.loopEnd >= this.state.clipBeats - 1e-6;
    const clipBeats = clamped * 4;
    const loop = clampLoop(
      this.state.loopStart,
      wasFull ? clipBeats : this.state.loopEnd,
      clipBeats,
    );

    this.state = {
      ...this.state,
      notes: stretchNotes(this.state.notes, this.state.bars, clamped),
      bars: clamped,
      clipBeats,
      loopStart: loop.start,
      loopEnd: loop.end,
    };
    this.emit();
  }

  // Manual length: notes keep their positions, exactly like a DAW clip where
  // shortening the loop hides the tail instead of squeezing it.
  setBarsManual(bars: number) {
    const clamped = clampBars(bars);
    if (clamped === this.state.bars && this.state.lengthMode === "manual") return;

    const wasFull = this.state.loopEnd >= this.state.clipBeats - 1e-6;
    const clipBeats = clamped * 4;
    const loop = clampLoop(
      this.state.loopStart,
      wasFull ? clipBeats : this.state.loopEnd,
      clipBeats,
    );

    this.pushUndo(this.snapshot());
    this.redoStack = [];
    this.state = {
      ...this.state,
      bars: clamped,
      clipBeats,
      loopStart: loop.start,
      loopEnd: loop.end,
      lengthMode: "manual",
    };
    this.emit();
  }

  // Back to following Resolume: adopt the host length and stretch the pattern
  // so nothing falls outside the clip.
  setAutoLength(bars: number) {
    const clamped = clampBars(bars);
    if (clamped === this.state.bars && this.state.lengthMode === "auto") return;

    const wasFull = this.state.loopEnd >= this.state.clipBeats - 1e-6;
    const clipBeats = clamped * 4;
    const loop = clampLoop(
      this.state.loopStart,
      wasFull ? clipBeats : this.state.loopEnd,
      clipBeats,
    );

    this.pushUndo(this.snapshot());
    this.redoStack = [];
    this.state = {
      ...this.state,
      notes: stretchNotes(this.state.notes, this.state.bars, clamped),
      bars: clamped,
      clipBeats,
      loopStart: loop.start,
      loopEnd: loop.end,
      lengthMode: "auto",
    };
    this.emit();
  }

  liveLoop(start: number, end: number) {
    const loop = clampLoop(start, end, this.state.clipBeats);
    if (loop.start === this.state.loopStart && loop.end === this.state.loopEnd) return;

    this.state = { ...this.state, loopStart: loop.start, loopEnd: loop.end };
    this.emit();
  }

  setLoop(start: number, end: number) {
    const loop = clampLoop(start, end, this.state.clipBeats);
    if (loop.start === this.state.loopStart && loop.end === this.state.loopEnd) return;

    this.pushUndo(this.snapshot());
    this.redoStack = [];
    this.state = { ...this.state, loopStart: loop.start, loopEnd: loop.end };
    this.emit();
  }

  resetLoop() {
    this.setLoop(0, this.state.clipBeats);
  }

  edit(fn: (notes: Note[]) => Note[]) {
    const base = this.state.notes;
    const next = fn(base);
    if (next === base) return;

    this.pushUndo(this.snapshot());
    this.redoStack = [];
    this.state = { ...this.state, notes: next };
    this.emit();
  }

  private pushUndo(snapshot: Snapshot) {
    this.undoStack.push(snapshot);
    if (this.undoStack.length > 200) this.undoStack.shift();
  }

  undo() {
    const prev = this.undoStack.pop();
    if (!prev) return;

    this.redoStack.push(this.snapshot());
    this.restore(prev);
    this.emit();
  }

  redo() {
    const next = this.redoStack.pop();
    if (!next) return;

    this.pushUndo(this.snapshot());
    this.restore(next);
    this.emit();
  }
}

export const store = new EditorStore();

export function useEditor(): EditorState {
  return useSyncExternalStore(store.subscribe, store.getSnapshot);
}
