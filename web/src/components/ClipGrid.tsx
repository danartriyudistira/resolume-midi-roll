import { Fragment, useEffect, useState } from "react";
import { ClipInfo, getThumbnail } from "../lib/rest";
import {
  selectClip,
  selectDeck,
  setFollow,
  triggerClip,
  triggerColumn,
  useClips,
} from "../state/clipStore";

const CLIP_COLORS: Record<number, string> = {
  1: "#2f6f9f",
  2: "#3f9f4f",
  3: "#b8a13a",
  4: "#b5563a",
  5: "#a33a5f",
  6: "#6b4fa3",
};

function ClipCell({ clip, selected }: { clip: ClipInfo; selected: boolean }) {
  const [thumb, setThumb] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    if (!clip.id || !clip.name) return;

    getThumbnail(clip.layer, clip.column, clip.id).then((url) => {
      if (!cancelled && url) setThumb(url);
    });

    return () => {
      cancelled = true;
    };
  }, [clip.id, clip.layer, clip.column, clip.name]);

  const empty = !clip.name && clip.state === "Empty";
  const connected = clip.state.startsWith("Connected");

  const className = [
    "clipcell",
    empty ? "empty" : "",
    clip.hasFx ? "hasfx" : "",
    connected ? "connected" : "",
    selected ? "selected" : "",
  ]
    .filter(Boolean)
    .join(" ");

  return (
    <div
      className={className}
      style={
        {
          backgroundImage: thumb ? `url(${thumb})` : undefined,
          "--clip-color": CLIP_COLORS[clip.color] ?? CLIP_COLORS[1],
        } as React.CSSProperties
      }
      title={`${clip.key}${clip.name ? ` · ${clip.name}` : ""}${clip.hasFx ? " · Midi Roll" : ""}`}
      onClick={() => selectClip(clip.key)}
      onDoubleClick={() => triggerClip(clip.layer, clip.column)}
    >
      {!empty && (
        <>
          <div className="colorbar" />
          <div className="label">{clip.name}</div>
        </>
      )}
    </div>
  );
}

export default function ClipGrid() {
  const { composition, currentKey, follow, online, saveState } = useClips();

  const columns = composition
    ? composition.layers.reduce((max, layer) => Math.max(max, layer.clips.length), 0)
    : 0;

  const deck = composition ? composition.decks[composition.deckIndex] : null;

  return (
    <section className="clipbar">
      <div className="clipbar-head">
        <div className="decknav">
          <button
            className="deckbtn"
            onClick={() => selectDeck((composition?.deckIndex ?? 0) - 1)}
            disabled={!composition || (composition.deckIndex ?? 0) <= 0}
          >
            ◀
          </button>
          <span className="deckname">{deck?.name ?? "—"}</span>
          <button
            className="deckbtn"
            onClick={() => selectDeck((composition?.deckIndex ?? 0) + 1)}
            disabled={
              !composition || (composition.deckIndex ?? 0) >= composition.decks.length - 1
            }
          >
            ▶
          </button>
          <span className="deckindex">
            {composition ? `${composition.deckIndex + 1}/${composition.decks.length}` : ""}
          </span>
        </div>

        <span className="clip-current">{currentKey || "—"}</span>

        <span className="clip-tempo">
          <b>{Math.round(composition?.tempo ?? 120)}</b> BPM · {composition?.playState ?? "—"}
        </span>

        <label className="follow">
          <input type="checkbox" checked={follow} onChange={(event) => setFollow(event.target.checked)} />
          Follow
        </label>

        <div className="spacer" />

        <span className={`save ${saveState}`}>
          {saveState === "saving"
            ? "SAVING"
            : saveState === "saved"
              ? "SAVED"
              : saveState === "failed"
                ? "SAVE FAILED"
                : ""}
        </span>

        <span className={`rest ${online ? "on" : "off"}`}>{online ? "REST 8080" : "REST off"}</span>
      </div>

      {composition ? (
        <div className="clipgrid" style={{ "--cols": columns } as React.CSSProperties}>
          <div className="colhead corner" />

          {Array.from({ length: columns }, (_, index) => (
            <div
              key={`col-${index}`}
              className={`colhead${composition.columns[index]?.connected ? " connected" : ""}`}
              onClick={() => triggerColumn(index + 1)}
              title={`Trigger kolom ${index + 1}`}
            >
              <span>{index + 1}</span>
              <span className="colplay">▶</span>
            </div>
          ))}

          {[...composition.layers].reverse().map((layer, reversedIndex) => {
            const layerIndex = composition.layers.length - 1 - reversedIndex;

            return (
              <Fragment key={`layer-${layerIndex}`}>
                <div className="layerhead">
                  <b>Layer {layerIndex + 1}</b>
                  <span>{layer.name !== "Layer #" ? layer.name : ""}</span>
                </div>

                {Array.from({ length: columns }, (_, index) => {
                  const clip = layer.clips[index];
                  return clip ? (
                    <ClipCell key={clip.key} clip={clip} selected={clip.key === currentKey} />
                  ) : (
                    <div key={`pad-${layerIndex}-${index}`} className="clipcell empty" />
                  );
                })}
              </Fragment>
            );
          })}
        </div>
      ) : (
        <div className="clip-offline">
          Resolume REST tidak terjangkau — piano roll tetap bisa dipakai.
        </div>
      )}
    </section>
  );
}
