import { useSyncExternalStore } from "react";
import {
  ClipInfo,
  CompositionInfo,
  clearThumbnailFailures,
  divisionOf,
  fetchComposition,
  notesFromParam,
  notesToParam,
  postAction,
  putParam,
} from "../lib/rest";
import { store as editorStore, stretchNotes } from "./store";

export type SaveState = "idle" | "saving" | "saved" | "failed";

interface ClipState {
  online: boolean;
  composition: CompositionInfo | null;
  currentKey: string;
  hasFx: boolean;
  follow: boolean;
  saveState: SaveState;
}

type Listener = () => void;

const listeners = new Set<Listener>();

let state: ClipState = {
  online: false,
  composition: null,
  currentKey: "",
  hasFx: false,
  follow: true,
  saveState: "idle",
};

function set(patch: Partial<ClipState>) {
  state = { ...state, ...patch };
  listeners.forEach((fn) => fn());
}

function subscribe(fn: Listener) {
  listeners.add(fn);
  return () => {
    listeners.delete(fn);
  };
}

function getSnapshot() {
  return state;
}

export function useClips(): ClipState {
  return useSyncExternalStore(subscribe, getSnapshot);
}

export function allClips(composition: CompositionInfo | null): ClipInfo[] {
  if (!composition) return [];
  return composition.layers.flatMap((layer) => layer.clips);
}

export function findClip(key: string): ClipInfo | null {
  return allClips(state.composition).find((clip) => clip.key === key) ?? null;
}

let lastPushed = "";
let loadedRaw = "";
let loadedNotesId: number | null = null;
let pushTimer: number | null = null;
let lastPushAt = 0;
let lastConnected = new Set<string>();

function schedulePush(notesId: number, json: string) {
  if (pushTimer !== null) window.clearTimeout(pushTimer);

  pushTimer = window.setTimeout(async () => {
    pushTimer = null;
    loadedRaw = json;
    set({ saveState: "saving" });

    const ok = await putParam(notesId, json);
    lastPushAt = Date.now();
    set({ saveState: ok ? "saved" : "failed" });
  }, 250);
}

export function selectClip(key: string, force = false) {
  const clip = findClip(key);
  if (!clip) return;

  // Re-selecting the same clip must not throw away undo history or reload over
  // edits that are still waiting to be pushed.
  if (!force && key === state.currentKey) return;

  set({ currentKey: key, hasFx: clip.hasFx, saveState: "idle" });
  loadedNotesId = clip.notesId;
  loadedRaw = clip.notesValue;

  if (!clip.hasFx) {
    lastPushed = notesToParam([], clip.bars, 0, clip.bars * 4, "auto");
    editorStore.load([], clip.bars);
    return;
  }

  const parsed = notesFromParam(clip.notesValue, divisionOf(clip.lengthValue)) ?? {
    notes: [],
    bars: clip.bars,
    loopStart: 0,
    loopEnd: clip.bars * 4,
    lengthMode: "auto" as const,
  };

  // Resolume owns the length unless the user pinned a manual one in the editor.
  const manual = parsed.lengthMode === "manual";
  const targetBars = manual ? parsed.bars : clip.bars;

  // Legacy patterns without a bars field keep their original timing; patterns
  // written for another length are time-stretched when following the host.
  const storedBars = /"bars"\s*:/.test(clip.notesValue) ? parsed.bars : clip.bars;
  const notes =
    !manual && storedBars !== targetBars
      ? stretchNotes(parsed.notes, storedBars, targetBars)
      : parsed.notes;

  const corrected = notesToParam(
    notes,
    targetBars,
    parsed.loopStart,
    parsed.loopEnd,
    parsed.lengthMode,
  );
  lastPushed = corrected;
  editorStore.load(notes, targetBars, parsed.loopStart, parsed.loopEnd, parsed.lengthMode);

  // Keep the plugin's pattern length aligned with the transport, even when the
  // stored note list predates the bars field. An empty clip still needs the
  // length so the playhead wraps at the right bar.
  const needsBars =
    !manual && (storedBars !== targetBars || (!clip.notesValue.trim() && targetBars > 1));

  if (clip.notesId && needsBars) {
    loadedRaw = corrected;
    set({ saveState: "saving" });
    putParam(clip.notesId, corrected).then((ok) => {
      lastPushAt = Date.now();
      set({ saveState: ok ? "saved" : "failed" });
    });
  }
}

export async function refresh() {
  const composition = await fetchComposition();

  if (!composition) {
    if (state.online) set({ online: false });
    return;
  }

  const wasOnline = state.online;
  set({ online: true, composition });

  if (!wasOnline && !state.currentKey) {
    const first = allClips(composition).find((clip) => clip.hasFx);
    if (first) selectClip(first.key);
  }

  if (state.follow) {
    const connected = allClips(composition).filter((clip) => clip.state.startsWith("Connected"));
    const fresh = connected.filter((clip) => !lastConnected.has(clip.key));
    const target = fresh.find((clip) => clip.hasFx) ?? fresh[0];

    if (target && target.key !== state.currentKey) {
      selectClip(target.key);
    }

    lastConnected = new Set(connected.map((clip) => clip.key));
  }

  const current = findClip(state.currentKey);

  // The deck changed (or the clip disappeared). Clip keys include the deck, so
  // the old key no longer resolves — pick a clip from the new deck instead of
  // leaving stale notes on screen.
  if (!current) {
    const fallback =
      allClips(composition).find((clip) => clip.hasFx) ??
      allClips(composition).find((clip) => clip.name);

    if (fallback) {
      selectClip(fallback.key);
    } else if (state.currentKey) {
      set({ currentKey: "", hasFx: false });
      loadedNotesId = null;
      loadedRaw = "";
      editorStore.load([], 1);
    }
    return;
  }

  // The clip under the same address was replaced (deck switch, preset recall).
  if (current.notesId !== loadedNotesId) {
    selectClip(current.key, true);
    return;
  }

  // A preset or another editor rewrote the Notes parameter. Reload only when
  // nothing local is waiting to be saved, so an in-flight edit is never lost.
  const snapshot = editorStore.getSnapshot();
  const localJson = notesToParam(
    snapshot.notes,
    snapshot.bars,
    snapshot.loopStart,
    snapshot.loopEnd,
    snapshot.lengthMode,
  );
  const pending = pushTimer !== null || Date.now() - lastPushAt < 2500;

  if (
    !pending &&
    state.saveState !== "failed" &&
    current.notesValue !== loadedRaw &&
    localJson === lastPushed
  ) {
    selectClip(current.key, true);
    return;
  }

  // Resolume owns the clip length in auto mode. When the beat setting changes
  // while the editor is open, time-stretch the pattern to the new length so the
  // piano roll follows instead of staying stale.
  if (current.hasFx && snapshot.lengthMode === "auto" && current.bars !== snapshot.bars) {
    editorStore.resizeClip(current.bars);
  }
}

export function setFollow(follow: boolean) {
  set({ follow });
}

export async function selectDeck(index: number) {
  const composition = state.composition;
  if (!composition || composition.decks.length === 0) return;

  const next = Math.max(0, Math.min(composition.decks.length - 1, index));
  await postAction(`/composition/decks/${next + 1}/select`);
  clearThumbnailFailures();
  await refresh();
}

export async function triggerColumn(column: number) {
  await postAction(`/composition/columns/${column}/connect`);
  await refresh();
}

export async function triggerClip(layer: number, column: number) {
  await postAction(`/composition/layers/${layer}/clips/${column}/connect`);
  await refresh();
}

// Edits are pushed to the clip that owns them, debounced. A clip switch flushes
// the previous clip because its id was captured when the timer was set.
editorStore.subscribe(() => {
  const clip = findClip(state.currentKey);
  if (!clip || !clip.notesId) return;

  const snapshot = editorStore.getSnapshot();
  const json = notesToParam(
    snapshot.notes,
    snapshot.bars,
    snapshot.loopStart,
    snapshot.loopEnd,
    snapshot.lengthMode,
  );
  if (json === lastPushed) return;

  lastPushed = json;
  schedulePush(clip.notesId, json);
});
