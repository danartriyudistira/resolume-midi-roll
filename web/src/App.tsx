import { useEffect } from "react";
import ClipGrid from "./components/ClipGrid";
import EditorControls from "./components/EditorControls";
import Minimap from "./components/Minimap";
import PianoRoll from "./components/PianoRoll";
import Toolbar from "./components/Toolbar";
import VelocityLane from "./components/VelocityLane";
import { refresh, useClips } from "./state/clipStore";
import { startTransportPolling } from "./state/transport";

export default function App() {
  const { currentKey, hasFx, online } = useClips();

  useEffect(() => {
    refresh();
    const clipTimer = window.setInterval(refresh, 1500);
    const transportTimer = startTransportPolling();

    return () => {
      window.clearInterval(clipTimer);
      window.clearInterval(transportTimer);
    };
  }, []);

  return (
    <div className="app">
      <Toolbar />
      <ClipGrid />
      <EditorControls />
      <div className="body">
        <main className="stage">
          <Minimap />
          <PianoRoll />
          <VelocityLane />
          {(!currentKey || !hasFx) && (
            <div className="editor-guard">
              <strong>
                {!online
                  ? "REST Resolume tidak terjangkau"
                  : currentKey
                    ? "Clip ini tidak punya efek Midi Roll"
                    : "Belum ada clip Midi Roll yang dipilih"}
              </strong>
              <span>
                {!online
                  ? "Perubahan tidak bisa disimpan. Cek webserver Resolume di port 8080."
                  : currentKey
                    ? "Tambahkan efek Midi Roll ke clip ini supaya note bisa disimpan."
                    : "Klik sel clip dengan titik kuning di grid atas."}
              </span>
            </div>
          )}
        </main>
      </div>
    </div>
  );
}
