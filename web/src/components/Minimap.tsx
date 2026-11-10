import { useEffect, useRef } from "react";
import { store } from "../state/store";
import { view } from "../state/view";

const KEYS_WIDTH = 64;

export default function Minimap() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const dragRef = useRef(false);

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

    const render = () => {
      const ctx = canvas.getContext("2d");
      if (!ctx) return;

      const width = canvas.clientWidth;
      const height = canvas.clientHeight;
      const state = store.getSnapshot();

      ctx.fillStyle = "#0b1020";
      ctx.fillRect(0, 0, width, height);

      const usable = width - KEYS_WIDTH;
      const clipBeats = Math.max(1, state.clipBeats);

      // Loop region highlight, so the minimap matches the piano roll.
      const loopStartX = KEYS_WIDTH + (state.loopStart / clipBeats) * usable;
      const loopEndX = KEYS_WIDTH + (state.loopEnd / clipBeats) * usable;
      ctx.fillStyle = "rgba(56, 214, 255, 0.12)";
      ctx.fillRect(loopStartX, 0, Math.max(0, loopEndX - loopStartX), height);
      ctx.strokeStyle = "rgba(56, 214, 255, 0.5)";
      ctx.lineWidth = 1;
      for (const lx of [loopStartX, loopEndX]) {
        ctx.beginPath();
        ctx.moveTo(Math.round(lx) + 0.5, 0);
        ctx.lineTo(Math.round(lx) + 0.5, height);
        ctx.stroke();
      }

      for (const note of state.notes) {
        const x = KEYS_WIDTH + (note.start / clipBeats) * usable;
        const w = Math.max(2, (note.length / clipBeats) * usable);
        const y = height - 4 - ((note.pitch - 24) / 96) * (height - 8);
        const inLoop =
          note.start < state.loopEnd - 1e-6 && note.start + note.length > state.loopStart + 1e-6;
        ctx.fillStyle = state.selection.includes(note.id)
          ? "#d9ff00"
          : inLoop
            ? "#5d6f9c"
            : "rgba(93, 111, 156, 0.3)";
        ctx.fillRect(x, Math.max(2, Math.min(height - 4, y)), w, 3);
      }

      const visibleBeats = usable / view.pxPerBeat;
      const x = KEYS_WIDTH + (view.scrollX / clipBeats) * usable;
      const w = (Math.min(visibleBeats, clipBeats) / clipBeats) * usable;

      ctx.strokeStyle = "#d9ff00";
      ctx.lineWidth = 1;
      ctx.strokeRect(x + 0.5, 0.5, Math.max(6, w), height - 1);
      ctx.fillStyle = "rgba(217, 255, 0, 0.08)";
      ctx.fillRect(x, 0, Math.max(6, w), height);
    };

    const loop = () => {
      raf = requestAnimationFrame(loop);
      if (!dirty) return;
      dirty = false;
      render();
    };
    raf = requestAnimationFrame(loop);

    return () => {
      cancelAnimationFrame(raf);
      ro.disconnect();
      unsubStore();
      unsubView();
    };
  }, []);

  const panTo = (clientX: number) => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const rect = canvas.getBoundingClientRect();
    const usable = rect.width - KEYS_WIDTH;
    const ratio = Math.max(0, Math.min(1, (clientX - rect.left - KEYS_WIDTH) / usable));
    const visibleBeats = usable / view.pxPerBeat;
    const clipBeats = Math.max(1, store.getSnapshot().clipBeats);
    const maxScroll = Math.max(0, clipBeats - visibleBeats);

    view.scrollX = Math.max(0, Math.min(maxScroll, ratio * clipBeats - visibleBeats / 2));
    view.touch();
  };

  return (
    <div className="minimap">
      <canvas
        ref={canvasRef}
        onPointerDown={(event) => {
          dragRef.current = true;
          event.currentTarget.setPointerCapture(event.pointerId);
          panTo(event.clientX);
        }}
        onPointerMove={(event) => {
          if (dragRef.current) panTo(event.clientX);
        }}
        onPointerUp={(event) => {
          dragRef.current = false;
          event.currentTarget.releasePointerCapture(event.pointerId);
        }}
      />
    </div>
  );
}
