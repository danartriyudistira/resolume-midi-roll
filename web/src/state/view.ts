export interface ViewState {
  pxPerBeat: number;
  rowHeight: number;
  scrollX: number; // beats
  scrollY: number; // pixels
}

type Listener = () => void;

const listeners = new Set<Listener>();

export const view: ViewState & {
  touch: () => void;
  subscribe: (fn: Listener) => () => void;
} = {
  pxPerBeat: 96,
  rowHeight: 22,
  scrollX: 0,
  scrollY: 0,
  touch() {
    listeners.forEach((fn) => fn());
  },
  subscribe(fn: Listener) {
    listeners.add(fn);
    return () => {
      listeners.delete(fn);
    };
  },
};
