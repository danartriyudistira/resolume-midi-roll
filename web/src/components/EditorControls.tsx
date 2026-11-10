import { store, useEditor } from "../state/store";
import { findClip, useClips } from "../state/clipStore";
import { NOTE_NAMES, SCALES } from "../lib/theory";
import { view } from "../state/view";

export default function EditorControls() {
  const state = useEditor();
  const clips = useClips();
  const clip = findClip(clips.currentKey);
  const autoBars = clip?.bars ?? state.bars;
  const locked = !clips.currentKey || !clips.hasFx;

  const zoom = (factor: number) => {
    view.pxPerBeat = Math.max(24, Math.min(480, view.pxPerBeat * factor));
    view.touch();
  };

  return (
    <div className={`toolbar editor-controls${locked ? " locked" : ""}`}>
      <label className="field">
        Grid
        <select value={state.grid} onChange={(event) => store.setGrid(Number(event.target.value))}>
          <option value={4}>1/4</option>
          <option value={8}>1/8</option>
          <option value={16}>1/16</option>
          <option value={32}>1/32</option>
        </select>
      </label>

      <div className="group">
        <label className="field">
          Bars
          <input
            type="number"
            min={1}
            max={16}
            value={state.bars}
            title="Clip length in bars (manual override)"
            onChange={(event) => store.setBarsManual(Number(event.target.value))}
          />
        </label>
        <button
          className={state.lengthMode === "auto" ? "on" : ""}
          onClick={() => store.setAutoLength(autoBars)}
          title="Follow the clip length from Resolume"
        >
          Auto
        </button>
      </div>

      <div className="group">
        <label className="field">
          Loop In
          <input
            type="number"
            step={0.25}
            min={0}
            max={state.clipBeats}
            value={Math.round(state.loopStart * 100) / 100}
            title="Loop start in beats (drag the bracket on the ruler)"
            onChange={(event) => store.setLoop(Number(event.target.value), state.loopEnd)}
          />
        </label>
        <label className="field">
          Out
          <input
            type="number"
            step={0.25}
            min={0}
            max={state.clipBeats}
            value={Math.round(state.loopEnd * 100) / 100}
            title="Loop end in beats"
            onChange={(event) => store.setLoop(state.loopStart, Number(event.target.value))}
          />
        </label>
        <button onClick={() => store.resetLoop()} title="Loop the whole clip">
          Full
        </button>
      </div>

      <div className="group">
        <button
          className={state.tool === "select" ? "on" : ""}
          onClick={() => store.setTool("select")}
          title="Select and move notes"
        >
          Select
        </button>
        <button
          className={state.tool === "eraser" ? "on" : ""}
          onClick={() => store.setTool("eraser")}
          title="Click a note to delete it"
        >
          Eraser
        </button>
        <button
          className={state.tool === "draw" ? "on" : ""}
          onClick={() => store.setTool("draw")}
          title="Drag to draw notes, click a note to erase (B)"
        >
          Draw
        </button>
      </div>

      <button className={state.fold ? "on" : ""} onClick={() => store.setFold(!state.fold)} title="Hide empty rows">
        Fold
      </button>

      <label className="field">
        Scale
        <select
          value={state.scaleRoot === null ? "off" : String(state.scaleRoot)}
          onChange={(event) =>
            store.setScale(event.target.value === "off" ? null : Number(event.target.value))
          }
        >
          <option value="off">Off</option>
          {NOTE_NAMES.map((name, index) => (
            <option key={name} value={index}>
              {name}
            </option>
          ))}
        </select>
      </label>

      <select
        value={state.scaleType}
        onChange={(event) => store.setScaleType(event.target.value)}
        disabled={state.scaleRoot === null}
      >
        {Object.keys(SCALES).map((name) => (
          <option key={name} value={name}>
            {name}
          </option>
        ))}
      </select>

      <div className="spacer" />

      <div className="group">
        <button onClick={() => store.undo()} title="Ctrl+Z">
          Undo
        </button>
        <button onClick={() => store.redo()} title="Ctrl+Shift+Z">
          Redo
        </button>
      </div>

      <div className="group">
        <button onClick={() => zoom(1 / 1.25)} title="Zoom out">
          −
        </button>
        <button onClick={() => zoom(1.25)} title="Zoom in">
          +
        </button>
      </div>
    </div>
  );
}
