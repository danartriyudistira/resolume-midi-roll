import { useEffect, useRef } from "react";
import { store, useEditor } from "../state/store";
import { view } from "../state/view";
import { EditorState, Note } from "../state/types";

const KEYS_WIDTH = 64;

function draw(canvas: HTMLCanvasElement, state: EditorState) {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;

  const width = canvas.clientWidth;
  const height = canvas.clientHeight;

  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#0b1020";
  ctx.fillRect(0, 0, width, height);

  ctx.fillStyle = "rgba(255, 255, 255, 0.05)";
  for (let line = 1; line < 4; line++) {
    const y = (height / 4) * line;
    ctx.fillRect(KEYS_WIDTH, y, width - KEYS_WIDTH, 1);
  }

  const selected = new Set(state.selection);

  for (const note of state.notes) {
    const x = KEYS_WIDTH + (note.start - view.scrollX) * view.pxPerBeat;
    if (x < KEYS_WIDTH - 4 || x > width) continue;

    const barHeight = Math.max(3, (note.velocity / 127) * (height - 14));
    ctx.fillStyle = selected.has(note.id) ? "#d9ff00" : "#7f8fb8";
    ctx.fillRect(Math.round(x), height - barHeight - 4, 5, barHeight);
  }

  ctx.fillStyle = "#2b3752";
  ctx.fillRect(KEYS_WIDTH, 0, 1, height);
}

export default function VelocityLane() {
  const state = useEditor();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const dragRef = useRef<{ id: string; startY: number; startVelocity: number; base: Note[] } | null>(null);

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
      mark();
    };

    const ro = new ResizeObserver(resize);
    ro.observe(canvas);
    resize();

    const loop = () => {
      raf = requestAnimationFrame(loop);
      if (!dirty) return;
      dirty = false;
      draw(canvas, store.getSnapshot());
    };
    raf = requestAnimationFrame(loop);

    return () => {
      cancelAnimationFrame(raf);
      ro.disconnect();
      unsubStore();
      unsubView();
    };
  }, []);

  const onPointerDown = (event: React.PointerEvent<HTMLCanvasElement>) => {
    const rect = event.currentTarget.getBoundingClientRect();
    const x = event.clientX - rect.left;

    if (x < KEYS_WIDTH) return;

    const beat = (x - KEYS_WIDTH) / view.pxPerBeat + view.scrollX;
    const state = store.getSnapshot();

    let closest: Note | null = null;
    let distance = Infinity;

    for (const note of state.notes) {
      const d = Math.abs(note.start - beat);
      if (d < distance) {
        distance = d;
        closest = note;
      }
    }

    if (!closest || distance * view.pxPerBeat > 10) return;

    store.select([closest.id]);
    store.begin();
    dragRef.current = {
      id: closest.id,
      startY: event.clientY,
      startVelocity: closest.velocity,
      base: state.notes,
    };

    event.currentTarget.setPointerCapture(event.pointerId);
  };

  const onPointerMove = (event: React.PointerEvent<HTMLCanvasElement>) => {
    const drag = dragRef.current;
    if (!drag) return;

    const delta = drag.startY - event.clientY;
    const velocity = Math.max(1, Math.min(127, Math.round(drag.startVelocity + delta * 1.5)));

    store.live(
      drag.base.map((note) => (note.id === drag.id ? { ...note, velocity } : note)),
    );
  };

  const onPointerUp = (event: React.PointerEvent<HTMLCanvasElement>) => {
    if (!dragRef.current) return;
    dragRef.current = null;
    store.commit();
    event.currentTarget.releasePointerCapture(event.pointerId);
  };

  return (
    <div className="velocity">
      <canvas
        ref={canvasRef}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
      />
      <span className="velocity-label">{state.selection.length > 0 ? "Velocity" : "Velocity lane"}</span>
    </div>
  );
}
