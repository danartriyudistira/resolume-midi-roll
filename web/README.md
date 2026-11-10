# Midi Roll — Web Editor

Piano roll berbasis React + TypeScript dengan renderer Canvas. Berdiri sendiri
(belum disajikan oleh plugin), supaya editor lama di `../gui` tetap jalan.

## Menjalankan

```bash
cd web
npm install
npm run dev      # http://localhost:5173
npm run build    # hasil di web/dist
npm run preview  # cek hasil build
```

## Struktur

```
web/
  src/
    state/
      types.ts     model Note + EditorState
      store.ts     state immutable + undo/redo + transaksi drag
      view.ts      zoom/scroll (di luar React, agar pan 60fps)
    lib/
      theory.ts    nama nada, skala, black key
    components/
      PianoRoll.tsx   canvas: grid, keyboard, note, interaksi
      VelocityLane.tsx
      Minimap.tsx
      Toolbar.tsx
      Sidebar.tsx
    App.tsx
    styles.css
```

## Kontrol

| Aksi | Hasil |
|---|---|
| Double-click grid | buat note (panjang = grid) |
| Double-click note | hapus |
| Drag badan note | pindah (snap) |
| Drag tepi kiri/kanan | ubah panjang |
| Shift + klik | tambah/lepas seleksi |
| Drag kosong | marquee |
| Ctrl + wheel | zoom horizontal di posisi kursor |
| Shift + wheel | zoom vertikal |
| Wheel | scroll |
| Space + drag / tombol tengah | pan |
| Ctrl+Z / Ctrl+Shift+Z | undo / redo |
| Ctrl+A | pilih semua |
| Delete | hapus seleksi |
| Panah | geser 1 langkah / 1 semitone |
| Shift + panah | 1 beat / 1 oktaf |
| Eraser tool | klik note untuk hapus |

## Catatan teknis

- Canvas digambar lewat `requestAnimationFrame` dengan flag `dirty`; zoom/pan
  mengubah objek `view` di luar React sehingga tidak memicu re-render.
- Store memakai `useSyncExternalStore`. Drag memakai transaksi: satu entri undo
  per gestur, bukan per frame.
- `devicePixelRatio` diperhitungkan; canvas mengikuti ukuran CSS 100%.
- Belum tersambung ke plugin/Resolume — langkah berikutnya: build ke `gui/`,
  serve aset statis dari BridgeServer, dan sinkronkan lewat REST.
