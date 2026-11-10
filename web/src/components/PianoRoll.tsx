import { useEffect, useRef } from "react";
import { makeId, store, useEditor } from "../state/store";
import { useClips } from "../state/clipStore";
import { view } from "../state/view";
import { transport } from "../state/transport";
import { inScale, isBlackKey, noteName } from "../lib/theory";
import { EditorState, Note } from "../state/types";

const KEYS_WIDTH = 64;
const LOOP_STRIP = 14;
const MIN_PITCH = 0;
const MAX_PITCH = 127;
const EDGE_PX = 8;

// Copy/paste clipboard: note positions relative to the earliest copied note.
let clipboard: { pitch: number; start: number; length: number; velocity: number }[] = [];

function visibleRows(state: EditorState): number[] {
  const all = () => {
    const rows: number[] = [];
    for (let pitch = MAX_PITCH; pitch >= MIN_PITCH; pitch--) rows.push(pitch);
    return rows;
  };

  if (!state.fold) return all();

  const used = new Set(state.notes.map((note) => note.pitch));

  // Fold with an empty clip would leave no rows at all, which makes it
  // impossible to draw the first note. Fall back to the full range.
  if (used.size === 0) return all();

  return [...used].sort((a, b) => b - a);
}

function rowMap(rows: number[]): Map<number, number> {
  const map = new Map<number, number>();
  rows.forEach((pitch, index) => map.set(pitch, index));
  return map;
}

function noteRect(note: Note, rows: Map<number, number>) {
  const row = rows.get(note.pitch);
  if (row === undefined) return null;

  return {
    x: KEYS_WIDTH + (note.start - view.scrollX) * view.pxPerBeat,
    y: row * view.rowHeight - view.scrollY,
    w: Math.max(4, note.length * view.pxPerBeat),
    h: view.rowHeight,
  };
}

function drawOverlay(canvas: HTMLCanvasElement) {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;

  const width = canvas.clientWidth;
  const height = canvas.clientHeight;

  ctx.clearRect(0, 0, width, height);

  const loopStart = Math.max(0, transport.loopStart);
  const loopEnd = Math.max(loopStart + 1, transport.loopEnd);
  let step = transport.step;

  if (transport.playing) {
    const elapsed = (performance.now() - transport.at) / 1000;
    const stepsPerSecond = (transport.bpm / 60) * 8;
    const length = loopEnd - loopStart;
    const advanced = (step - loopStart + elapsed * stepsPerSecond) % length;
    step = loopStart + ((advanced % length) + length) % length;
  }

  const beat = step / 8;
  const x = KEYS_WIDTH + (beat - view.scrollX) * view.pxPerBeat;

  if (x < KEYS_WIDTH - 2 || x > width) return;

  ctx.strokeStyle = "rgba(255, 255, 255, 0.18)";
  ctx.lineWidth = 7;
  ctx.beginPath();
  ctx.moveTo(x, 0);
  ctx.lineTo(x, height);
  ctx.stroke();

  ctx.strokeStyle = "rgba(255, 255, 255, 0.92)";
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(x, 0);
  ctx.lineTo(x, height);
  ctx.stroke();
}

function draw(canvas: HTMLCanvasElement, state: EditorState) {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;

  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  const rows = visibleRows(state);
  const rowLookup = rowMap(rows);
  const pxPerBeat = view.pxPerBeat;
  const rowHeight = view.rowHeight;
  const scrollY = view.scrollY;

  ctx.fillStyle = "#060912";
  ctx.fillRect(0, 0, width, height);

  // Row stripes
  const firstRow = Math.max(0, Math.floor(scrollY / rowHeight));
  const lastRow = Math.min(rows.length - 1, Math.ceil((scrollY + height) / rowHeight));

  for (let index = firstRow; index <= lastRow; index++) {
    const pitch = rows[index];
    const y = index * rowHeight - scrollY;

    ctx.fillStyle = isBlackKey(pitch) ? "#070b16" : "#0a1020";
    ctx.fillRect(KEYS_WIDTH, y, width - KEYS_WIDTH, rowHeight);

    if (state.scaleRoot !== null && !inScale(pitch, state.scaleRoot, state.scaleType)) {
      ctx.fillStyle = "rgba(0, 0, 0, 0.35)";
      ctx.fillRect(KEYS_WIDTH, y, width - KEYS_WIDTH, rowHeight);
    }

    ctx.fillStyle = "rgba(255, 255, 255, 0.035)";
    ctx.fillRect(KEYS_WIDTH, y + rowHeight - 1, width - KEYS_WIDTH, 1);
  }

  // Grid lines
  const stepBeats = 4 / state.grid;
  const startBeat = Math.max(0, Math.floor(view.scrollX / stepBeats) * stepBeats);
  const endBeat = view.scrollX + (width - KEYS_WIDTH) / pxPerBeat;

  for (let beat = startBeat; beat <= endBeat; beat += stepBeats) {
    const x = Math.round(KEYS_WIDTH + (beat - view.scrollX) * pxPerBeat) + 0.5;
    const isBar = Math.abs(beat % 4) < 1e-6;
    const isBeat = Math.abs(beat % 1) < 1e-6;

    ctx.strokeStyle = isBar
      ? "rgba(150, 255, 170, 0.55)"
      : isBeat
        ? "rgba(130, 255, 160, 0.22)"
        : "rgba(120, 255, 190, 0.07)";
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
  }

  // Clip end
  const clipX = KEYS_WIDTH + (state.clipBeats - view.scrollX) * pxPerBeat;
  if (clipX < width) {
    ctx.fillStyle = "rgba(0, 0, 0, 0.55)";
    ctx.fillRect(clipX, 0, width - clipX, height);
  }

  // Loop region: dim what the sequencer skips.
  const loopStartX = KEYS_WIDTH + (state.loopStart - view.scrollX) * pxPerBeat;
  const loopEndX = KEYS_WIDTH + (state.loopEnd - view.scrollX) * pxPerBeat;

  ctx.fillStyle = "rgba(0, 0, 0, 0.42)";
  if (loopStartX > KEYS_WIDTH) {
    ctx.fillRect(KEYS_WIDTH, 0, Math.min(loopStartX, width) - KEYS_WIDTH, height);
  }
  if (loopEndX < width) {
    const left = Math.max(loopEndX, KEYS_WIDTH);
    ctx.fillRect(left, 0, width - left, height);
  }

  ctx.strokeStyle = "rgba(56, 214, 255, 0.45)";
  ctx.lineWidth = 1;
  for (const x of [loopStartX, loopEndX]) {
    if (x < KEYS_WIDTH - 1 || x > width) continue;
    ctx.beginPath();
    ctx.moveTo(Math.round(x) + 0.5, 0);
    ctx.lineTo(Math.round(x) + 0.5, height);
    ctx.stroke();
  }

  // Notes
  ctx.save();
  ctx.beginPath();
  ctx.rect(KEYS_WIDTH, 0, width - KEYS_WIDTH, height);
  ctx.clip();

  for (const note of state.notes) {
    const rect = noteRect(note, rowLookup);
    if (!rect) continue;
    if (rect.x + rect.w < KEYS_WIDTH || rect.x > width) continue;
    if (rect.y + rect.h < 0 || rect.y > height) continue;

    const selected = state.selection.includes(note.id);
    const inLoop =
      note.start < state.loopEnd - 1e-6 && note.start + note.length > state.loopStart + 1e-6;
    ctx.globalAlpha = inLoop ? 1 : 0.3;
    const body = selected ? "#d9ff00" : "#c2cdf0";
    const head = selected ? "#a9c800" : "#94a1d4";
    const edge = selected ? "#93ac00" : "#7c88b8";

    const w = Math.max(3, rect.w - 1);
    const h = Math.max(3, rect.h - 2);

    ctx.fillStyle = body;
    ctx.strokeStyle = edge;
    ctx.lineWidth = 1;

    const radius = Math.min(3, w / 2);
    ctx.beginPath();
    ctx.roundRect(rect.x, rect.y + 1, w, h, radius);
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = head;
    ctx.beginPath();
    ctx.roundRect(rect.x, rect.y + 1, w, Math.min(4, h), [radius, radius, 0, 0]);
    ctx.fill();

    if (w > 26) {
      ctx.fillStyle = "#101427";
      ctx.font = "9px 'Segoe UI', system-ui, sans-serif";
      ctx.textBaseline = "middle";
      ctx.save();
      ctx.beginPath();
      ctx.rect(rect.x + 3, rect.y, w - 6, rect.h);
      ctx.clip();
      ctx.fillText(`${noteName(note.pitch)} · ${note.velocity}`, rect.x + 4, rect.y + rect.h / 2 + 1);
      ctx.restore();
    }
  }

  ctx.globalAlpha = 1;
  ctx.restore();

  // Marquee
  const marquee = (window as unknown as { __marquee?: { x: number; y: number; w: number; h: number } }).__marquee;
  if (marquee) {
    ctx.fillStyle = "rgba(217, 255, 0, 0.12)";
    ctx.strokeStyle = "#d9ff00";
    ctx.lineWidth = 1;
    ctx.fillRect(marquee.x, marquee.y, marquee.w, marquee.h);
    ctx.strokeRect(marquee.x + 0.5, marquee.y + 0.5, marquee.w, marquee.h);
  }

  // Loop strip: a thin ruler along the top with draggable in/out handles.
  const stripLeft = Math.max(KEYS_WIDTH, loopStartX);
  const stripRight = Math.min(width, loopEndX);

  ctx.fillStyle = "rgba(6, 9, 18, 0.88)";
  ctx.fillRect(KEYS_WIDTH, 0, width - KEYS_WIDTH, LOOP_STRIP);

  ctx.fillStyle = "rgba(56, 214, 255, 0.18)";
  if (stripRight > stripLeft) ctx.fillRect(stripLeft, 0, stripRight - stripLeft, LOOP_STRIP);

  ctx.fillStyle = "rgba(56, 214, 255, 0.75)";
  for (const x of [loopStartX, loopEndX]) {
    if (x < KEYS_WIDTH - 1 || x > width) continue;
    ctx.fillRect(Math.round(x) - 1, 0, 3, LOOP_STRIP);
  }

  ctx.fillStyle = "rgba(56, 214, 255, 0.9)";
  ctx.font = "8px 'Segoe UI', system-ui, sans-serif";
  ctx.textBaseline = "middle";
  ctx.fillText("LOOP", KEYS_WIDTH + 5, LOOP_STRIP / 2 + 1);

  ctx.strokeStyle = "rgba(56, 214, 255, 0.35)";
  ctx.beginPath();
  ctx.moveTo(KEYS_WIDTH, LOOP_STRIP + 0.5);
  ctx.lineTo(width, LOOP_STRIP + 0.5);
  ctx.stroke();

  // Keyboard
  ctx.fillStyle = "#0b1020";
  ctx.fillRect(0, 0, KEYS_WIDTH, height);

  for (let index = firstRow; index <= lastRow; index++) {
    const pitch = rows[index];
    const y = index * rowHeight - scrollY;
    const black = isBlackKey(pitch);

    ctx.fillStyle = black ? "#151a2b" : "#dfe3ee";
    ctx.fillRect(0, y, black ? KEYS_WIDTH * 0.66 : KEYS_WIDTH, rowHeight);

    ctx.strokeStyle = "rgba(0, 0, 0, 0.35)";
    ctx.beginPath();
    ctx.moveTo(0, Math.round(y + rowHeight) + 0.5);
    ctx.lineTo(KEYS_WIDTH, Math.round(y + rowHeight) + 0.5);
    ctx.stroke();

    if (pitch % 12 === 0) {
      ctx.fillStyle = "#2a3048";
      ctx.font = "9px 'Segoe UI', system-ui, sans-serif";
      ctx.textAlign = "right";
      ctx.textBaseline = "middle";
      ctx.fillText(noteName(pitch), KEYS_WIDTH - 7, y + rowHeight / 2);
      ctx.textAlign = "left";
    }
  }

  ctx.strokeStyle = "#2b3752";
  ctx.beginPath();
  ctx.moveTo(KEYS_WIDTH + 0.5, 0);
  ctx.lineTo(KEYS_WIDTH + 0.5, height);
  ctx.stroke();
}

export default function PianoRoll() {
  const state = useEditor();
  const { currentKey } = useClips();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const overlayRef = useRef<HTMLCanvasElement>(null);
  const centeredRef = useRef("");
  const dragRef = useRef<{
    mode:
      | "move"
      | "resize-l"
      | "resize-r"
      | "marquee"
      | "pan"
      | "draw"
      | "loop-in"
      | "loop-out"
      | "loop-move"
      | "loop-new";
    startX: number;
    startY: number;
    baseNotes: Note[];
    origin: Note | null;
    selection: string[];
    marquee?: { x: number; y: number; w: number; h: number };
    scrollX: number;
    scrollY: number;
    startBeat?: number;
    loopStart?: number;
    loopEnd?: number;
  } | null>(null);
  const spaceRef = useRef(false);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    let raf = 0;
    let dirty = true;

    const mark = () => {
      dirty = true;
    };

    const unsubStore = store.subscribe(mark);
    const unsubView = view.subscribe(mark);

    const resize = () => {
      const dpr = window.devicePixelRatio || 1;
      const width = canvas.clientWidth;
      const height = canvas.clientHeight;

      canvas.width = Math.max(1, Math.floor(width * dpr));
      canvas.height = Math.max(1, Math.floor(height * dpr));

      const ctx = canvas.getContext("2d");
      ctx?.setTransform(dpr, 0, 0, dpr, 0, 0);

      const overlay = overlayRef.current;
      if (overlay) {
        overlay.width = canvas.width;
        overlay.height = canvas.height;
        overlay.getContext("2d")?.setTransform(dpr, 0, 0, dpr, 0, 0);
      }

      mark();
    };

    const ro = new ResizeObserver(resize);
    ro.observe(canvas);
    resize();

    // Open on the notes instead of the top of the pitch range.
    const initialNotes = store.getSnapshot().notes;
    if (initialNotes.length > 0) {
      const average = initialNotes.reduce((sum, note) => sum + note.pitch, 0) / initialNotes.length;
      const row = 127 - average;
      view.scrollY = Math.max(0, row * view.rowHeight - (canvas.clientHeight - 120) / 2);
      view.touch();
    }

    const loop = () => {
      raf = requestAnimationFrame(loop);
      if (!dirty) return;
      dirty = false;
      draw(canvas, store.getSnapshot());
    };
    raf = requestAnimationFrame(loop);

    // The playhead lives on its own transparent layer so it can run at 60 fps
    // without redrawing the grid and notes.
    let overlayRaf = 0;
    const overlayLoop = () => {
      overlayRaf = requestAnimationFrame(overlayLoop);
      const overlay = overlayRef.current;
      if (overlay) drawOverlay(overlay);
    };
    overlayRaf = requestAnimationFrame(overlayLoop);

    const onWheel = (event: WheelEvent) => {
      event.preventDefault();
      const rect = canvas.getBoundingClientRect();
      const x = event.clientX - rect.left;

      if (event.ctrlKey || event.metaKey) {
        const anchorBeat = (x - KEYS_WIDTH) / view.pxPerBeat + view.scrollX;
        const factor = event.deltaY < 0 ? 1.15 : 1 / 1.15;
        view.pxPerBeat = Math.max(24, Math.min(480, view.pxPerBeat * factor));
        view.scrollX = Math.max(0, anchorBeat - (x - KEYS_WIDTH) / view.pxPerBeat);
      } else if (event.shiftKey) {
        view.rowHeight = Math.max(10, Math.min(48, view.rowHeight * (event.deltaY < 0 ? 1.1 : 0.9)));
      } else {
        view.scrollY = Math.max(0, view.scrollY + event.deltaY);
        view.scrollX = Math.max(0, view.scrollX + event.deltaX / view.pxPerBeat);
      }

      view.touch();
    };

    canvas.addEventListener("wheel", onWheel, { passive: false });

    return () => {
      cancelAnimationFrame(raf);
      cancelAnimationFrame(overlayRaf);
      ro.disconnect();
      unsubStore();
      unsubView();
      canvas.removeEventListener("wheel", onWheel);
    };
  }, []);

  // The clip length comes from Resolume's transport setting, so re-fit whenever
  // it changes.
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || canvas.clientWidth === 0) return;

    view.pxPerBeat = Math.max(
      24,
      Math.min(480, (canvas.clientWidth - KEYS_WIDTH) / Math.max(1, state.clipBeats)),
    );
    view.scrollX = 0;
    view.touch();
  }, [state.clipBeats]);

  // Opening or switching clips must show the notes, not whatever pitch range
  // the previous clip was scrolled to.
  useEffect(() => {
    if (!currentKey) return;

    const centerKey = `${currentKey}:${state.fold}`;
    if (centerKey === centeredRef.current) return;

    const canvas = canvasRef.current;
    if (!canvas) return;

    centeredRef.current = centerKey;
    const snapshot = store.getSnapshot();
    const notes = snapshot.notes;
    const pitch = notes.length
      ? notes.reduce((sum, note) => sum + note.pitch, 0) / notes.length
      : 48;

    // Fold changes the row index, so ask the same row list the canvas uses.
    const rows = visibleRows(snapshot);
    let index = 0;
    let best = Infinity;
    rows.forEach((value, row) => {
      const distance = Math.abs(value - pitch);
      if (distance < best) {
        best = distance;
        index = row;
      }
    });

    view.scrollY = Math.max(0, index * view.rowHeight - (canvas.clientHeight - 120) / 2);
    view.touch();
  }, [currentKey, state.fold]);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const tag = (event.target as HTMLElement | null)?.tagName;
      if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA") return;

      if (event.code === "Space") {
        event.preventDefault();
        const target = event.target as HTMLElement | null;
        if (target?.tagName === "BUTTON") target.blur();
        spaceRef.current = true;
        return;
      }

      const modifier = event.ctrlKey || event.metaKey;

      if (modifier && event.key.toLowerCase() === "z") {
        event.preventDefault();
        if (event.shiftKey) store.redo();
        else store.undo();
        return;
      }

      if (modifier && event.key.toLowerCase() === "a") {
        event.preventDefault();
        store.select(store.getSnapshot().notes.map((note) => note.id));
        return;
      }

      if (modifier && event.key.toLowerCase() === "d") {
        event.preventDefault();
        const snapshot = store.getSnapshot();
        const selected = snapshot.notes.filter((note) => snapshot.selection.includes(note.id));
        if (selected.length === 0) return;

        const minStart = Math.min(...selected.map((note) => note.start));
        const maxEnd = Math.max(...selected.map((note) => note.start + note.length));
        const shift = Math.max(1 / 32, maxEnd - minStart);

        const clones = selected.map((note) => ({
          ...note,
          id: makeId(),
          start: Math.max(0, Math.min(snapshot.clipBeats - note.length, note.start + shift)),
        }));

        store.edit((notes) => [...notes, ...clones]);
        store.select(clones.map((clone) => clone.id));
        return;
      }

      if (modifier && event.key.toLowerCase() === "c") {
        const snapshot = store.getSnapshot();
        const selected = snapshot.notes.filter((note) => snapshot.selection.includes(note.id));
        if (selected.length === 0) return;

        event.preventDefault();
        const minStart = Math.min(...selected.map((note) => note.start));
        clipboard = selected.map((note) => ({
          pitch: note.pitch,
          start: note.start - minStart,
          length: note.length,
          velocity: note.velocity,
        }));
        return;
      }

      if (modifier && event.key.toLowerCase() === "v") {
        if (clipboard.length === 0) return;

        event.preventDefault();
        const snapshot = store.getSnapshot();
        const snap = 4 / snapshot.grid;
        const base = Math.max(0, Math.round(transport.step / 8 / snap) * snap);

        const pasted = clipboard.map((entry) => ({
          id: makeId(),
          pitch: entry.pitch,
          start: Math.max(0, Math.min(snapshot.clipBeats - entry.length, base + entry.start)),
          length: entry.length,
          velocity: entry.velocity,
        }));

        store.edit((notes) => [...notes, ...pasted]);
        store.select(pasted.map((note) => note.id));
        return;
      }

      if (!modifier && event.key.toLowerCase() === "b") {
        event.preventDefault();
        const current = store.getSnapshot().tool;
        store.setTool(current === "draw" ? "select" : "draw");
        return;
      }

      if (event.key === "Delete" || event.key === "Backspace") {
        const selected = new Set(store.getSnapshot().selection);
        if (selected.size === 0) return;
        event.preventDefault();
        store.edit((notes) => notes.filter((note) => !selected.has(note.id)));
        store.select([]);
        return;
      }

      if (event.key === "Escape") {
        store.select([]);
        return;
      }

      // Arrows move by grid step; Shift moves a whole beat / octave.
      const step = event.shiftKey ? 1 : 4 / store.getSnapshot().grid;
      const pitches = event.shiftKey ? 12 : 1;

      if (["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(event.key)) {
        const snapshot = store.getSnapshot();
        const selected = new Set(snapshot.selection);
        if (selected.size === 0) return;
        event.preventDefault();

        store.edit((notes) =>
          notes.map((note) => {
            if (!selected.has(note.id)) return note;

            if (event.key === "ArrowLeft") return { ...note, start: Math.max(0, note.start - step) };
            if (event.key === "ArrowRight")
              return {
                ...note,
                start: Math.max(
                  0,
                  Math.min(note.start + step, Math.max(0, snapshot.clipBeats - note.length)),
                ),
              };
            if (event.key === "ArrowUp") return { ...note, pitch: Math.min(MAX_PITCH, note.pitch + pitches) };
            return { ...note, pitch: Math.max(MIN_PITCH, note.pitch - pitches) };
          }),
        );
      }
    };

    const onKeyUp = (event: KeyboardEvent) => {
      if (event.code === "Space") {
        event.preventDefault();
        spaceRef.current = false;
      }
    };

    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);

    return () => {
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
    };
  }, []);

  const pointFromEvent = (event: React.PointerEvent<HTMLCanvasElement>) => {
    const rect = event.currentTarget.getBoundingClientRect();
    return { x: event.clientX - rect.left, y: event.clientY - rect.top };
  };

  const onPointerDown = (event: React.PointerEvent<HTMLCanvasElement>) => {
    const canvas = event.currentTarget;
    const { x, y } = pointFromEvent(event);
    const snapshot = store.getSnapshot();
    const rows = visibleRows(snapshot);
    const lookup = rowMap(rows);

    // Middle click is pan; stop the browser's autoscroll from kicking in.
    if (event.button === 1) event.preventDefault();

    if (event.button === 1 || spaceRef.current) {
      dragRef.current = {
        mode: "pan",
        startX: x,
        startY: y,
        baseNotes: snapshot.notes,
        origin: null,
        selection: snapshot.selection,
        scrollX: view.scrollX,
        scrollY: view.scrollY,
      };
      canvas.setPointerCapture(event.pointerId);
      return;
    }

    if (x < KEYS_WIDTH) return;

    if (y <= LOOP_STRIP) {
      const snap = event.altKey ? 1 / 32 : 4 / snapshot.grid;
      const rawBeat = (x - KEYS_WIDTH) / view.pxPerBeat + view.scrollX;
      const beat = Math.max(0, Math.min(snapshot.clipBeats, Math.round(rawBeat / snap) * snap));
      const startX = KEYS_WIDTH + (snapshot.loopStart - view.scrollX) * view.pxPerBeat;
      const endX = KEYS_WIDTH + (snapshot.loopEnd - view.scrollX) * view.pxPerBeat;
      const edge = 8;

      let mode: "loop-in" | "loop-out" | "loop-move" | "loop-new";
      if (Math.abs(x - startX) <= edge) mode = "loop-in";
      else if (Math.abs(x - endX) <= edge) mode = "loop-out";
      else if (x > startX && x < endX) mode = "loop-move";
      else mode = "loop-new";

      store.begin();
      dragRef.current = {
        mode,
        startX: x,
        startY: y,
        baseNotes: snapshot.notes,
        origin: null,
        selection: snapshot.selection,
        scrollX: view.scrollX,
        scrollY: view.scrollY,
        startBeat: beat,
        loopStart: snapshot.loopStart,
        loopEnd: snapshot.loopEnd,
      };

      canvas.setPointerCapture(event.pointerId);
      return;
    }

    const hit = [...snapshot.notes].reverse().find((note) => {
      const rect = noteRect(note, lookup);
      return rect && x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
    });

    if (snapshot.tool === "draw") {
      if (hit) {
        store.edit((notes) => notes.filter((note) => note.id !== hit.id));
        return;
      }

      const snap = 4 / snapshot.grid;
      const rawBeat = (x - KEYS_WIDTH) / view.pxPerBeat + view.scrollX;
      const beat = Math.max(
        0,
        Math.min(snapshot.clipBeats - snap, Math.round(rawBeat / snap) * snap),
      );
      const row = Math.max(
        0,
        Math.min(rows.length - 1, Math.floor((y + view.scrollY) / view.rowHeight)),
      );
      const pitch = rows[row];
      if (pitch === undefined) return;

      const note: Note = { id: makeId(), pitch, start: beat, length: snap, velocity: 100 };

      store.begin();
      store.live([...snapshot.notes, note]);
      store.select([note.id]);

      dragRef.current = {
        mode: "draw",
        startX: x,
        startY: y,
        baseNotes: snapshot.notes,
        origin: note,
        selection: [note.id],
        scrollX: view.scrollX,
        scrollY: view.scrollY,
        startBeat: beat,
      };

      canvas.setPointerCapture(event.pointerId);
      return;
    }

    if (hit && snapshot.tool === "eraser") {
      store.edit((notes) => notes.filter((note) => note.id !== hit.id));
      return;
    }

    if (hit) {
      const rect = noteRect(hit, lookup)!;
      const edge = Math.min(EDGE_PX, rect.w / 3);
      const mode = x <= rect.x + edge ? "resize-l" : x >= rect.x + rect.w - edge ? "resize-r" : "move";

      let selection = snapshot.selection;
      if (event.shiftKey) {
        selection = selection.includes(hit.id)
          ? selection.filter((id) => id !== hit.id)
          : [...selection, hit.id];
      } else if (!selection.includes(hit.id)) {
        selection = [hit.id];
      }

      let baseNotes = snapshot.notes;
      let origin = hit;

      if (event.altKey) {
        // Alt drag duplicates the selection and moves the clones instead of the
        // original notes.
        const source = selection.includes(hit.id) ? selection : [hit.id];
        const clones = snapshot.notes
          .filter((note) => source.includes(note.id))
          .map((note) => ({ ...note, id: makeId() }));

        baseNotes = [...snapshot.notes, ...clones];
        selection = clones.map((clone) => clone.id);
        origin =
          clones.find((clone) => clone.pitch === hit.pitch && clone.start === hit.start) ??
          clones[0] ??
          hit;

        store.begin();
        store.live(baseNotes);
        store.select(selection);
      } else {
        store.select(selection);
        store.begin();
      }

      dragRef.current = {
        mode,
        startX: x,
        startY: y,
        baseNotes,
        origin,
        selection,
        scrollX: view.scrollX,
        scrollY: view.scrollY,
      };

      canvas.setPointerCapture(event.pointerId);
      return;
    }

    if (!event.shiftKey) store.select([]);
    store.begin();

    dragRef.current = {
      mode: "marquee",
      startX: x,
      startY: y,
      baseNotes: snapshot.notes,
      origin: null,
      selection: store.getSnapshot().selection,
      marquee: { x, y, w: 0, h: 0 },
      scrollX: view.scrollX,
      scrollY: view.scrollY,
    };

    canvas.setPointerCapture(event.pointerId);
  };

  const onPointerMove = (event: React.PointerEvent<HTMLCanvasElement>) => {
    const drag = dragRef.current;
    if (!drag) return;

    const { x, y } = pointFromEvent(event);
    const snapshot = store.getSnapshot();

    if (drag.mode === "pan") {
      view.scrollX = Math.max(0, drag.scrollX - (x - drag.startX) / view.pxPerBeat);
      view.scrollY = Math.max(0, drag.scrollY - (y - drag.startY));
      view.touch();
      return;
    }

    if (drag.mode.startsWith("loop")) {
      const snap = event.altKey ? 1 / 32 : 4 / snapshot.grid;
      const rawBeat = (x - KEYS_WIDTH) / view.pxPerBeat + view.scrollX;
      const beat = Math.max(0, Math.min(snapshot.clipBeats, Math.round(rawBeat / snap) * snap));
      const min = 1 / 32;

      if (drag.mode === "loop-in") {
        store.liveLoop(Math.min(beat, snapshot.loopEnd - min), snapshot.loopEnd);
      } else if (drag.mode === "loop-out") {
        store.liveLoop(snapshot.loopStart, Math.max(beat, snapshot.loopStart + min));
      } else if (drag.mode === "loop-new") {
        const anchor = drag.startBeat ?? beat;
        const start = Math.min(anchor, beat);
        const end = Math.max(anchor, beat);
        store.liveLoop(start, Math.max(end, start + min));
      } else {
        const from = drag.loopStart ?? snapshot.loopStart;
        const to = drag.loopEnd ?? snapshot.loopEnd;
        const length = Math.max(min, to - from);
        const delta = beat - (drag.startBeat ?? beat);
        const start = Math.max(0, Math.min(snapshot.clipBeats - length, from + delta));
        store.liveLoop(start, start + length);
      }
      return;
    }

    if (drag.mode === "draw" && drag.origin) {
      const snap = 4 / snapshot.grid;
      const rawBeat = (x - KEYS_WIDTH) / view.pxPerBeat + view.scrollX;
      const end = Math.max(
        drag.origin.start + snap,
        Math.min(snapshot.clipBeats, Math.round(rawBeat / snap) * snap),
      );
      const length = Math.max(snap, end - drag.origin.start);

      store.live([...drag.baseNotes, { ...drag.origin, length }]);
      return;
    }

    const snap = 4 / snapshot.grid;
    const selected = new Set(drag.selection);

    if (drag.mode === "move") {
      const deltaBeats = Math.round((x - drag.startX) / view.pxPerBeat / snap) * snap;
      const rowDelta = Math.round((y - drag.startY) / view.rowHeight);

      store.live(
        drag.baseNotes.map((note) =>
          selected.has(note.id)
            ? {
                ...note,
                start: Math.max(0, Math.min(snapshot.clipBeats - note.length, note.start + deltaBeats)),
                pitch: Math.max(MIN_PITCH, Math.min(MAX_PITCH, note.pitch - rowDelta)),
              }
            : note,
        ),
      );
      return;
    }

    if (drag.mode === "resize-l" || drag.mode === "resize-r") {
      const origin = drag.origin;
      if (!origin) return;

      const deltaBeats = Math.round((x - drag.startX) / view.pxPerBeat / snap) * snap;

      store.live(
        drag.baseNotes.map((note) => {
          if (note.id !== origin.id) return note;

          if (drag.mode === "resize-r") {
            return {
              ...note,
              length: Math.max(
                snap,
                Math.min(snapshot.clipBeats - note.start, origin.length + deltaBeats),
              ),
            };
          }

          const start = Math.max(0, Math.min(origin.start + origin.length - snap, origin.start + deltaBeats));
          return { ...note, start, length: origin.length + (origin.start - start) };
        }),
      );
      return;
    }

    if (drag.mode === "marquee" && drag.marquee) {
      const marquee = { x: drag.startX, y: drag.startY, w: x - drag.startX, h: y - drag.startY };
      drag.marquee = marquee;
      (window as unknown as { __marquee?: typeof marquee }).__marquee = marquee;

      const left = Math.min(marquee.x, marquee.x + marquee.w);
      const right = Math.max(marquee.x, marquee.x + marquee.w);
      const top = Math.min(marquee.y, marquee.y + marquee.h);
      const bottom = Math.max(marquee.y, marquee.y + marquee.h);

      const rows = visibleRows(snapshot);
      const lookup = rowMap(rows);

      const ids = snapshot.notes
        .filter((note) => {
          const rect = noteRect(note, lookup);
          return rect && rect.x < right && rect.x + rect.w > left && rect.y < bottom && rect.y + rect.h > top;
        })
        .map((note) => note.id);

      store.select(ids);
      view.touch();
    }
  };

  const onPointerUp = (event: React.PointerEvent<HTMLCanvasElement>) => {
    const drag = dragRef.current;
    if (!drag) return;

    if (drag.mode === "marquee") {
      (window as unknown as { __marquee?: unknown }).__marquee = undefined;
    }

    dragRef.current = null;
    store.commit();
    event.currentTarget.releasePointerCapture(event.pointerId);
  };

  const onDoubleClick = (event: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = event.currentTarget.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    if (x < KEYS_WIDTH || y <= LOOP_STRIP) return;

    const snapshot = store.getSnapshot();
    if (snapshot.tool === "draw") return;
    const rows = visibleRows(snapshot);
    const lookup = rowMap(rows);

    const hit = [...snapshot.notes].reverse().find((note) => {
      const r = noteRect(note, lookup);
      return r && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
    });

    if (hit) {
      store.edit((notes) => notes.filter((note) => note.id !== hit.id));
      return;
    }

    const snap = 4 / snapshot.grid;
    const rawBeat = Math.round(((x - KEYS_WIDTH) / view.pxPerBeat + view.scrollX) / snap) * snap;
    const beat = Math.max(0, Math.min(snapshot.clipBeats - snap, rawBeat));
    const row = Math.max(0, Math.min(rows.length - 1, Math.floor((y + view.scrollY) / view.rowHeight)));
    const pitch = rows[row];

    if (pitch === undefined) return;

    const note: Note = {
      id: `n${Date.now()}${Math.random().toString(16).slice(2)}`,
      pitch,
      start: beat,
      length: snap,
      velocity: 100,
    };

    store.edit((notes) => [...notes, note]);
    store.select([note.id]);
  };

  return (
    <div
      className="pianoroll"
      data-notes={state.notes.length}
      data-bars={state.bars}
      data-mode={state.lengthMode}
      data-loop-start={state.loopStart}
      data-loop-end={state.loopEnd}
    >
      <canvas
        ref={canvasRef}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
        onDoubleClick={onDoubleClick}
        onContextMenu={(event) => event.preventDefault()}
      />
      <canvas ref={overlayRef} className="overlay" />
    </div>
  );
}
