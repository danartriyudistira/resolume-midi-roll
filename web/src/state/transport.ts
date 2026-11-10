// Live transport from the plugin's bridge. Kept outside React so the playhead
// can animate at 60 fps without re-rendering the tree.
const BRIDGE = location.port === "47823" ? "" : "http://127.0.0.1:47823";

export interface TransportState {
  step: number;
  bars: number;
  loopStart: number; // 1/32 steps
  loopEnd: number; // 1/32 steps, exclusive
  playing: boolean;
  bpm: number;
  at: number;
}

export const transport: TransportState = {
  step: 0,
  bars: 1,
  loopStart: 0,
  loopEnd: 32,
  playing: false,
  bpm: 120,
  at: 0,
};

export async function pollTransport(): Promise<void> {
  try {
    const response = await fetch(`${BRIDGE}/status`, { signal: AbortSignal.timeout(800) });
    if (!response.ok) return;

    const data = await response.json();

    const step = Number(data.step);
    const bars = Number(data.bars);
    const bpm = Number(data.bpm);
    const loopStart = Number(data.loopStart);
    const loopEnd = Number(data.loopEnd);

    transport.step = Number.isFinite(step) ? Math.max(0, step) : 0;
    transport.bars = Number.isFinite(bars) ? Math.max(1, Math.min(16, bars)) : 1;
    transport.playing = Boolean(data.playing);
    transport.bpm = Number.isFinite(bpm) && bpm > 0 ? bpm : 120;

    // Older plugins do not report loop points; fall back to the whole clip.
    const total = transport.bars * 32;
    const start = Number.isFinite(loopStart) ? Math.max(0, loopStart) : 0;
    const end = Number.isFinite(loopEnd) ? Math.max(start + 1, loopEnd) : total;
    transport.loopStart = Math.min(start, Math.max(0, total - 1));
    transport.loopEnd = Math.max(transport.loopStart + 1, Math.min(total, end));

    transport.at = performance.now();
  } catch {
    transport.playing = false;
  }
}

export function startTransportPolling(): number {
  pollTransport();
  return window.setInterval(pollTransport, 100);
}
