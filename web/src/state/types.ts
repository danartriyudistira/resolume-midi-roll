export interface Note {
  id: string;
  pitch: number; // 0..127
  start: number; // beats
  length: number; // beats
  velocity: number; // 1..127
}

export type Tool = "select" | "eraser" | "draw";

export type LengthMode = "auto" | "manual";

export interface EditorState {
  notes: Note[];
  selection: string[];
  grid: number; // 4 | 8 | 16 | 32
  fold: boolean;
  scaleRoot: number | null;
  scaleType: string;
  tool: Tool;
  bars: number;
  clipBeats: number;
  loopStart: number; // beats
  loopEnd: number; // beats, exclusive
  lengthMode: LengthMode;
}
