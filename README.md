# Midi Roll

FFGL effect untuk Resolume yang berfungsi sebagai **sequencer MIDI** sekaligus
**modulator envelope**. Lihat `MANIFESTO.md` untuk arah proyek.

Status: **Modul 2 selesai** — skeleton plugin, mesin envelope, output MIDI,
piano roll, dan jembatan dua arah plugin ↔ browser.

---

## Isi paket

| Path | Fungsi |
|---|---|
| `src/MidiRollPlugin.cpp` | Plugin FFGL: parameter, clock sequencer, push envelope ke host |
| `src/EnvelopeEngine.*` | Kurva decay eksponensial, 4 lane, bebas alokasi per frame |
| `src/MidiOut.*` | Output MIDI Win32 (winmm), multi-channel, note tracking |
| `src/BridgeServer.*` | Listener HTTP loopback: serve GUI + terima pola |
| `src/BrowserLauncher.*` | `ShellExecute` ke GUI, dengan fallback path |
| `src/PluginMain.cpp` | Metadata plugin (`MR01`) |
| `gui/` | hasil build editor React dari `web/`, disajikan plugin di `127.0.0.1:47823` |
| `tools/probe.cpp` | Smoke test: baca DLL, cetak parameter, tahan listener untuk tes |
| `vendor/ffgl` | Resolume FFGL SDK (submodule) |

## Clone

```powershell
git clone --recursive https://github.com/danartriyudistira/resolume-midi-roll.git
```

`vendor/ffgl` adalah submodule. Kalau sudah terlanjur clone tanpa `--recursive`:

```powershell
git submodule update --init --recursive
```

## Build

```powershell
.\build.ps1
```

Hasil build otomatis disalin ke folder plugin Resolume:

```
%MyDocuments%\Resolume Arena\Extra Effects\
    MidiRoll.dll
    gui\index.html
```

`MyDocuments` diambil dari Windows (`[Environment]::GetFolderPath`), bukan
`%USERPROFILE%\Documents`, karena folder itu sering dialihkan ke OneDrive.
Resolume membaca dari lokasi yang sama.

> Resolume hanya memindai plugin saat start. Tutup dan buka ulang Arena setelah
> build pertama.

Build manual:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Uji coba tanpa Resolume:

```powershell
.\test.ps1
```

Dua tes: metadata (semua parameter terdaftar) dan end-to-end (konteks OpenGL
nyata, plugin instantiate, render, envelope bergerak, note + CC tertangkap di
port loopMIDI).

## Pakai di Resolume

1. Jalankan **loopMIDI** dan buat satu port virtual.
2. Buka Resolume → drag **Midi Roll** ke layer/clip.
3. Set **Midi Port** ke port loopMIDI, **Midi Channel** sesuai hardware.
4. Tekan **Play** — plugin mengirim note dan menggerakkan **Env 1..4**.
5. Tekan **Edit Mode** — piano roll terbuka di browser.

## Arsitektur jembatan

Plugin membuka listener HTTP di `127.0.0.1:47823` selama minimal satu instance
hidup. Halaman piano roll **disajikan oleh plugin itu sendiri**, jadi:

- tidak ada CORS, tidak ada preflight;
- `http://127.0.0.1` adalah secure context, jadi Web MIDI aktif;
- tidak ada file yang perlu dibuka manual.

| Endpoint | Fungsi |
|---|---|
| `GET /` | halaman piano roll (dibaca dari `gui/index.html` di sebelah DLL) |
| `GET /status` | transport + loop: `{"ok":true,"playing":true,"bpm":128,"step":42,"bars":3,"loopStart":0,"loopEnd":96}` |
| `GET /pattern?slot=a` | pola 64 karakter milik slot |
| `POST /pattern?slot=a` | kirim pola baru, body `text/plain` |

Alur sinkronisasi:

```
browser edit  ──POST──►  BridgeServer  ──Consume──►  pattern_  ──►  sequencer
                                  ▲                      │
                                  └──── Publish ◄────────┘  (saat Connect / ganti slot)
```

Pola juga ditulis ke parameter teks **Pattern** dan di-*raise* ke host, jadi
nilai terbaru ikut tersimpan di composition dan preset. Saat preset dimuat,
`SetTextParameter` mengembalikan pola ke sequencer.

Encoding: 128 karakter, 4 lane × 32 langkah, satu digit per kotak.
`0` = mati, `1` = lembut (0.4), `2` = sedang (0.7), `3` = penuh (1.0).
Panjang tetap supaya preset tidak berubah saat `Length` diganti.

## Editor piano roll

Clip mengikuti grid Resolume, bukan slot abstrak. Alamatnya
`deck/layer/kolom` — `1/2/2` berarti deck 1, layer 2, kolom 2. Deck ikut menjadi
bagian alamat supaya clip yang sama di deck lain tidak tertukar. Gridnya ditata
seperti Resolume: header kolom dengan tombol trigger, header layer di kiri, dan
thumbnail clip di dalam sel.

- **Deck / page** — tombol ◀ ▶ di atas berpindah deck lewat
  `POST /composition/decks/{n}/select`, lengkap dengan nama deck dan indeks.
- **Trigger kolom** — tombol ▶ di header kolom memanggil
  `POST /composition/columns/{n}/connect`.
- **Trigger clip** — klik dua kali pada sel clip.
- **Tempo** dibaca dari komposisi dan ditampilkan read-only. Playhead memakai
  tempo itu untuk interpolasi antar-poll, jadi tidak ada dua sumber BPM.
- **Panjang clip** mengikuti transport clip di Resolume. Mengubah beat
  (mis. 16 → 8) langsung memendekkan/memanjangkan piano roll dan
  men-time-stretch note secara proporsional.
- **Panjang manual** — field **BARS** + tombol **Auto**. Saat manual, panjang
  dikunci user dan tidak lagi ditimpa Resolume. Note **tidak** di-stretch:
  posisinya tetap, note di luar panjang baru disimpan tapi tidak diputar.
  Tombol **Auto** mengembalikan panjang ke transport clip.
- **Loop in/out** — grup **LOOP** di toolbar atau bracket di ruler (drag handle,
  Alt = bebas 1/32). Area loop di-shade, note di luar loop diredupkan. Loop
  hanya memengaruhi playback MIDI, bukan loop video Resolume.
- **Playhead** digambar di canvas overlay 60 fps dari `step`/`bpm`/`loopStart`/
  `loopEnd` di `/status` plugin, jadi tetap halus dan memantul di loop out.
- **Follow** membuat editor pindah sendiri ke clip yang baru di-trigger.
- **Fallback**: kalau REST tidak terjangkau, editor dikunci dengan pesan
  "REST Resolume tidak terjangkau" supaya tidak ada edit yang hilang.

Rentang nada MIDI 0–127 (C-1 sampai G9), scroll vertikal, keyboard dan ruler
menempel.

| Aksi | Hasil |
|---|---|
| Klik sel kosong | buat note 1 langkah, velocity 100 |
| Drag di area kosong | seleksi kotak (marquee) |
| **Draw mode** (tombol atau `B`) | drag menggambar note sepanjang drag; klik note = hapus |
| Shift + klik note | tambah / lepas dari seleksi |
| Drag badan note | pindah semua note terpilih |
| Drag tepi kanan | ubah panjang semua note terpilih (delta sama) |
| **Alt + drag** | duplikat seleksi lalu geser klonnya |
| **Ctrl + D** | duplikat seleksi ke kanan sepanjang selection |
| **Ctrl + C / V** | copy / paste di posisi playhead |
| Panah kiri/kanan | geser 1 langkah grid |
| Panah atas/bawah | geser 1 semitone |
| Shift + panah | 1 beat (kiri/kanan) atau 1 oktaf (atas/bawah) |
| **Ctrl + Z / Ctrl + Shift + Z** | undo / redo (200 langkah) |
| Ctrl/Cmd + A | pilih semua note |
| Esc | lepas seleksi |
| **Velocity lane** | drag bar di bawah grid untuk ubah velocity |

Edit dikirim ke parameter `Notes` efek Midi Roll lewat REST
(`PUT /api/v1/parameter/by-id/{id}`), dengan indikator **SAVING / SAVED / SAVE
FAILED** di header. Komposisi Resolume adalah satu-satunya sumber kebenaran —
tidak ada draft localStorage. Kalau clip yang dipilih tidak punya efek Midi
Roll, editor dikunci dan menampilkan peringatan supaya edit tidak hilang diam-diam.

## Memetakan envelope ke parameter lain

Plugin mengirim envelope sebagai **CC 21, 22, 23, 24** di channel yang dipilih.

- Resolume → Preferences → MIDI → aktifkan port loopMIDI sebagai input.
- Klik kanan parameter target → *MIDI map* → gerakkan envelope (CC 21–24).

Ini jalur modulasi sebenarnya: MIDI keluar dari plugin, masuk lagi ke Resolume
sebagai input, lalu dipetakan ke slider mana pun.

## Transport

Tidak ada tombol Play dan tidak ada slider BPM. Sequencer mengambil posisi dan
tempo dari host lewat `SetBeatInfo(bpm, barPhase)`:

- **Play/pause ikut Resolume.** `barPhase` berhenti bergerak saat komposisi
  di-pause, jadi step juga berhenti. Tidak ada note yang terlewat atau dobel.
- **Tempo ikut Resolume.** Tidak ada drift, karena posisi dihitung dari phase
  host, bukan dari akumulasi timer sendiri.
- **Fallback.** Kalau host tidak mengirim beat info, plugin berjalan bebas di
  tempo terakhir yang diketahui (default 120 BPM) dan menulis peringatan ke log
  Resolume. Ini hanya jaring pengaman, bukan mode normal.

Kalau log Resolume memuat `Host beat info: bpm ...` berarti sinkronisasi aktif.
Kalau memuat `No host beat info received`, berarti host tidak mengirimnya.

### Bukti dari sesi nyata

Pola 22 note per bar, host 128 BPM (1 bar = 1,875 s) → seharusnya 11,73 note/s.
Yang tertangkap di port loopMIDI: **11,62 note/s**. Selisih 1%.

Harness juga menguji pause: `play 13, pause 0, play 13` note on. Nol note saat
host berhenti.

### Loop dan panjang manual

Loop dihitung dari posisi absolut host:

```
pos = loopStartBeat + (hostBeats mod (loopEndBeat - loopStartBeat))
```

Jadi loop tidak harus kelipatan bar. Contoh hasil harness (2 bar, note di step
0/16/32/48):

| Loop (step) | Note yang berbunyi | Env aktif |
|---|---|---|
| `0..16` | hanya step 0 | lane 1 |
| `16..48` | step 16, 32 | lane 1, 3 |
| `8..24` | step 8, 16 | lane 1, 3 |

Harness menerima `--notes-file <json>` untuk menghindari escaping kutip di
shell, serta `--expect-lanes N` / `--min-notes N` saat loop sengaja mengecualikan
sebagian lane. `--genre N` (0–4) dan `--template N` (0–4) menguji template
bawaan: genre 0 Techno, 1 House, 2 Trance, 3 Drum & Bass, 4 Hip-Hop; template
0 Empty, 1 Drums, 2 Bassline, 3 Melody, 4 Full Groove. `--root N` (0–11),
`--scale N` (0–5), `--bars N` (1–8), `--progression "A D A G"`, dan
`--density N` (0–1) menguji nada dasar, scale, panjang, progresi, dan kepadatan.
Tambahkan `--random` untuk variasi acak terkendali, `--previous` untuk menguji
tombol Previous, dan `--env1-osc "<alamat>"` untuk menguji alamat OSC Env 1.
Harness mencetak `Root/Scale`, `Bars/Density`, `Progression`, dan `Notes`.

## Peta parameter

| # | Nama | Tipe | Catatan |
|---|---|---|---|
| 0 | Midi Port | option | dropdown `Off` + semua output MIDI saat plugin dimuat |
| 1 | Midi Channel | option | 1–16, per instance |
| 2 | Midi Offset | standard | kompensasi latency −80…+80 ms, 0.5 = netral |
| 3 | Edit Mode | event | buka piano roll di browser |
| 4 | Root | option | `C` … `B` — nada dasar untuk bass/melodi |
| 5 | Scale | option | `Major`, `Natural Minor`, `Dorian`, `Mixolydian`, `Pentatonic Minor`, `Pentatonic Major` |
| 6 | Genre | option | `Techno`, `House`, `Trance`, `Drum & Bass`, `Hip-Hop` |
| 7 | Template | option | `Empty`, `Drums`, `Bassline`, `Melody`, `Full Groove` |
| 8 | Bars | option | 1–8 bar untuk pola yang dihasilkan (default 1) |
| 9 | Progression | text | root/kualitas chord per bar, mis. `Am Dm A G` |
| 10 | Density | standard | 0–1, kepadatan Random (default 0.5) |
| 11 | Load | event | memuat template + menyimpan snapshot ke history |
| 12 | Random | event | variasi acak + menyimpan snapshot ke history |
| 13 | Previous | event | kembali ke pola sebelum Load/Random (riwayat 8 langkah) |
| 14 | Decay | standard | 0.05–3.0 detik |
| 15 | Env 1 | standard | keluaran; lane aktif mengikuti note yang di-fold |
| 16 | Env 1 OSC | text | alamat OSC, default `/composition/selectedclip/dashboard/link1` |
| 17 | OSC 1 Enable | boolean | centang untuk mengaktifkan OSC Env 1 |
| 18 | Env 2 | standard | keluaran |
| 19 | Env 2 OSC | text | default `/composition/selectedclip/dashboard/link2` |
| 20 | OSC 2 Enable | boolean | centang untuk mengaktifkan OSC Env 2 |
| 21 | Env 3 | standard | keluaran |
| 22 | Env 3 OSC | text | default `/composition/selectedclip/dashboard/link3` |
| 23 | OSC 3 Enable | boolean | centang untuk mengaktifkan OSC Env 3 |
| 24 | Env 4 | standard | keluaran |
| 25 | Env 4 OSC | text | default `/composition/selectedclip/dashboard/link4` |
| 26 | OSC 4 Enable | boolean | centang untuk mengaktifkan OSC Env 4 |
| 27 | Allow Overwrite | boolean | izinkan tombol OSC mengganti alamat yang sudah ada |
| 28 | OSC from Clip | event | isi alamat dari clip yang selected |
| 29 | OSC Composition | event | isi alamat composition dashboard |
| 30 | OSC Layer | event | isi alamat layer dari clip yang selected |
| 31 | Notes | text | JSON daftar note; dipakai web editor |

Legacy yang disembunyikan: `Grid` (32), `Pattern` (33), `Load Template` (34),
`Random Template` (35).

### Panel Resolume

**Grup atas — MIDI out & generator:**

`Midi Port`, `Midi Channel`, `Midi Offset`, `Edit Mode`, `Root`, `Scale`,
`Genre`, `Template`, `Bars`, `Progression`, `Density`, `Load`, `Random`,
`Previous`.

**Grup bawah — kontrol output Env:**

`Decay`, lalu untuk tiap Env: `Env 1` → `Env 1 OSC` → `OSC 1 Enable`,
`Env 2` → `Env 2 OSC` → `OSC 2 Enable`, dan seterusnya sampai Env 4.
Setelah `OSC 4 Enable`: `Allow Overwrite`, `OSC from Clip`,
`OSC Composition`, `OSC Layer`.

`Notes` ada di paling bawah.

### Alamat OSC otomatis

Tombol di panel:

| Tombol | Hasil |
|---|---|
| `OSC from Clip` | `/composition/layers/{L}/clips/{C}/dashboard/linkN` |
| `OSC Composition` | `/composition/dashboard/linkN` |
| `OSC Layer` | `/composition/layers/{L}/dashboard/linkN` |

`{L}` dan `{C}` diambil dari **clip yang selected** di Resolume. Clip harus
mengandung effect **Midi Roll**; kalau tidak, alamat tidak diubah dan plugin
menulis log.

Kalau alamat sudah pernah diisi (bukan default `selectedclip`), tombol menolak
mengganti. Centang **Allow Overwrite** dulu, lalu tekan tombol. Setelah berhasil,
centang otomatis dilepas lagi.

Butuh **Webserver Resolume** aktif di `127.0.0.1:8080` (sama seperti web editor).
Tombol `OSC Layer` memakai layer dari clip yang selected, bukan layer yang
sedang dipilih di UI.

Midi Port dirender sebagai dropdown karena daftar elemennya diberi pemisah
(`AddElementSeparator`): `Off` di atas, lalu daftar device MIDI. Tanpa pemisah,
Resolume menampilkan opsi sebagai deretan tombol.

Grid tetap disembunyikan dari panel (disimpan untuk kompatibilitas preset).

**Default kosong.** Effect baru tidak memainkan note apa pun: pola legacy di
`Pattern` seluruhnya nol dan `Notes` kosong. Pilih **Root**, **Scale**, **Genre**,
**Template**, **Bars**, dan **Progression**, lalu tekan `Load` atau
`Random`.

**Bars** default 1 (sama seperti sekarang), maksimal 8. Bars hanya dipakai saat
Load/Random; setelah itu `Notes` menyimpan panjangnya dan web menampilkan pola
multi-bar.

**Progression** menentukan root dan kualitas chord tiap bar:

```
Bars = 4, Progression = Am Dm A G
bar 1 = A minor, bar 2 = D minor, bar 3 = A minor, bar 4 = G mayor
```

- Format: `A D A G`, `C# F# B E`, `Am Dm A G`, `C7 F G`, `Bdim`.
- Kualitas chord: tanpa suffix = **mayor**, `m` = minor, `7` = dominant 7,
  `dim` = diminished, `*` = ikut Scale global.
- Kosong → semua bar memakai Root + Scale global.
- Entri lebih sedikit dari Bars → diulang.
- Kualitas chord hanya mengubah interval melodi; bass tetap root + fifth +
  oktaf, dan drum tidak ditranspose.

| Token | Arti | Contoh melodi (root C) |
|---|---|---|
| `C` | C mayor | C4, E4, G4, B4 |
| `Cm` | C minor | C4, D#4, G4, A#4 |
| `C7` | C dominant 7 | C4, E4, G4, A#4 |
| `Cdim` | C diminished | C4, D#4, F#4, A4 |
| `C*` | ikut Scale global | tergantung Scale |

**Nada dasar dan scale.** Root menentukan transposisi; scale menentukan interval
yang dipakai bass dan melodi. Drum tidak terpengaruh. Contoh:

| Root + Scale | Melody (Template Melody, Techno) |
|---|---|
| C + Natural Minor | C4, D#4, G4, A#4 |
| D + Natural Minor | D4, F4, A4, C5 |
| C + Major | C4, E4, G4, B4 |
| A + Natural Minor | A4, C5, E5, A5 |

Bass memakai **root bar + aksen fifth/oktaf** dua oktaf lebih rendah, bukan
random walk melodi. Random juga memakai
scale yang dipilih, jadi variasi tetap di dalam nada dasar dan scale.

Genre tersedia: **Techno**, **House**, **Trance**, **Drum & Bass**, **Hip-Hop**.
Template adalah bagian yang dimuat:

| Template | Isi |
|---|---|
| `Empty` | menghapus note (1 bar, loop penuh) |
| `Drums` | pola drum sesuai genre |
| `Bassline` | pola bass sesuai genre |
| `Melody` | pola melodi sesuai genre |
| `Full Groove` | drum + bass + melodi sesuai genre |

Contoh isi pola:

| Genre | Drums | Bass/Melody |
|---|---|---|
| Techno | kick 4-on-floor, snare 2 & 4, hat off-beat | bass root, aksen fifth; melodi derajat 0-2-4-6 |
| House | kick 4-on-floor, clap 2 & 4, open hat | bass root/fifth/oktaf; melodi derajat 0-2-4-7 |
| Trance | kick 4-on-floor, hat 1/8, open hat | bass rolling root + fifth; melodi derajat 0-4-5-7 |
| Drum & Bass | kick broken, snare 2 & 4, hat cepat | sub root + low fifth; melodi derajat 0-2-3-4 |
| Hip-Hop | kick boom-bap, snare 2 & 4, hat swung | bass root/fifth; melodi derajat 0-1-2-4 |

Template ditulis ke `Notes` (bukan `Pattern`) dan dipublikasikan ke host, jadi
editor web langsung menampilkan note yang sama. Template hanya diterapkan saat
`Load` ditekan — bukan saat effect/preset dimuat — supaya preset yang
sudah berisi note hasil edit tidak tertimpa.

### Random

Tombol `Random` ada di bawah `Density` (setelah `Load`) dan langsung memuat variasi dari
kombinasi **Root + Scale + Genre + Template + Bars + Progression** yang sedang
dipilih. Random tidak liar:

- mengikuti **Bars** (1–8 bar), `manual`, loop penuh;
- pitch tetap di dalam nada dasar/scale, dan root tiap bar mengikuti
  **Progression**;
- drum memakai pola kick/snare khas genre, hanya hat/velocity yang divariasikan;
- melodi memakai random walk kecil di dalam 4 degree genre, dengan occasional
  jump dan resolusi ke root di akhir bar;
- bass tetap di root bar dengan aksen fifth/oktaf (tidak ikut random walk
  melodi);
- **Density** mengatur seberapa padat ketukan yang dihasilkan;
- `Template = Empty` + `Random` tetap kosong.

### Density

Slider **Density** 0–1 (default 0.5, tampil sebagai persentase) hanya
memengaruhi `Random`; `Load` tetap memakai pola referensi.

| Bagian | Density rendah | Density tinggi |
|---|---|---|
| Drum | kick/snare inti tetap, hat jarang | hat hampir semua posisi, open hat lebih sering, ghost kick/snare |
| Bass | banyak rest, root beat 1 selalu ada | onset rapat, sedikit rest |
| Melody | beberapa note saja | hampir semua 8 note terisi |

Probabilitas:

- hat: `20% + density × 75%` → 20–95%
- open hat: `density × 50%` → 0–50%
- onset bass/melodi: `30% + density × 70%` → 30–100%
- ghost note drum: 0–30% (kick) dan 0–25% (snare), hanya di density tinggi

Pitch tetap di dalam Root/Scale; Density hanya mengubah jumlah/kerapatan
ketukan, bukan nada.

Contoh hasil: Techno Drums random tetap 4-on-floor dengan hat yang berpindah;
Hip-Hop Melody random tetap di set degree genre, ditransposisikan ke Root/Scale.

### Previous

Setiap `Load` atau `Random` menyimpan snapshot `Notes` (pitch, step, length,
velocity, bars, loop) ke riwayat **8 langkah** sebelum pola diganti. `Previous`
mengembalikan snapshot terakhir dan mempublikasikannya ke web, jadi pola lama
langsung kembali terlihat di piano roll.

- Tekan `Previous` berulang kali untuk mundur beberapa langkah.
- Riwayat hanya di memori instance; tidak tersimpan di composition/preset.
- Riwayat kosong (mis. effect baru) → `Previous` tidak melakukan apa pun.
- Edit manual di web tidak masuk riwayat; hanya Load/Random.

### Dashboard Resolume

Live performance dilakukan di Resolume, jadi kontrol besar ditaruh di
**Dashboard clip** (bukan layer). Dashboard clip mengikuti clip dan tetap
tersimpan saat pindah deck. Clip `Metaballs` di deck **Generators** sudah diisi:

| Dial | Nama | Parameter |
|---|---|---|
| 2 | Genre | Genre |
| 3 | Template | Template |
| 4 | Root | Root |
| 5 | Scale | Scale |
| 6 | Bars | Bars |
| 7 | Density | Density |
| 8 | Decay | Decay |

Dial **1 tidak bisa dipakai** untuk clip effect ini (Resolume menolak assignment
ke dial 1 lewat API); biarkan kosong.

Catatan penting:

- **Load**, **Random**, dan **Previous** adalah *event*, bukan range/choice.
  Dashboard hanya berisi dial, jadi ketiganya tidak bisa dijadikan dial. Tekan
  dari panel effect, atau map ke MIDI/keyboard shortcut Resolume.
- **Progression** bertipe text, juga tidak bisa dijadikan dial; atur dari panel
  atau web.
- Assignment dashboard bersifat **per clip**. Kalau ingin pola baru dengan
  dashboard yang sama, **duplicate clip** tersebut.
- **Midi Offset** tetap di panel untuk trim latency.

Alur live: putar Genre/Template/Root/Scale/Bars dan Density di Dashboard, tekan
**Random** di panel sampai pola cocok, **Previous** kalau meleset. Detail note
diedit di web lewat **Edit Mode**.

### Env 1 → Dashboard (OSC)

`Env 1 OSC` berada tepat di bawah **Env 1** dan di atas **Env 2**. Isinya adalah
**alamat OSC manual** yang digerakkan Env 1, bukan MIDI.

Default:

```
/composition/selectedclip/dashboard/link1
```

Alamat lain yang bisa dipakai:

```
/composition/dashboard/link1
/composition/layers/1/dashboard/link1
/composition/layers/1/clips/1/dashboard/link1
```

1. Resolume → **Preferences → OSC** → aktifkan **OSC Input** (port **7000**).
2. Di Midi Roll, isi `Env 1 OSC` dengan alamat tujuan.
3. Centang **OSC Enable** untuk mengaktifkan pengiriman.
4. Buka dashboard yang sesuai dan link parameter warna ke rotary tersebut.
5. Play clip. Env 1 menggerakkan rotary di alamat itu.

Plugin mengirim nilai Env 1 ke alamat tersebut setiap nilainya berubah.
Hilangkan centang **OSC Enable** untuk mematikan OSC tanpa menghapus alamatnya.
Kosongkan field alamat untuk mematikan juga. Tidak ada MIDI learn atau Shortcut
Preset untuk jalur ini.

**Penting:** jangan drag parameter **Env 1** ke dashboard. Itu membuat dial
mengontrol Env 1 (arah terbalik). Dial harus digerakkan oleh OSC dari plugin,
lalu parameter warna di-link ke dial.

Untuk Env 2–4, gunakan jalur MIDI CC di bawah.

### Env sebagai penggerak Dashboard (CC)

Env 1–4 dipancarkan sebagai **MIDI CC 21–24**. Resolume bisa memetakan CC itu ke
dial Dashboard, lalu parameter lain (mis. warna) di-link ke dial tersebut. Jadi
Env tidak menjadi dial itu sendiri — Env **menggerakkan** dial lewat MIDI.

```
Env 1..4 → CC 21..24 (loopMIDI) → MIDI shortcut → Dashboard dial → parameter warna
```

- CC selalu dikirim di channel yang sama dengan note.
- CC hanya dikirim saat nilainya berubah; setelah port/channel diganti, plugin
  mengirim ulang nilai saat ini.

Langkah mapping (ulangi untuk tiap Env):

1. Aktifkan `loopMIDI Port` sebagai MIDI Input di Preferences Resolume.
2. Di Midi Roll: `Midi Port = loopMIDI Port` dan `Midi Channel` untuk note
   sekaligus CC (CC mengikuti channel note).
3. Play clip supaya **Env 1** bergerak.
4. Masuk MIDI shortcut mode, klik rotary Dashboard 1. Resolume menangkap
   **CC 21**.
5. Set mode **Absolute**, lalu keluar dari map mode.
6. Ulangi untuk **Env 2 → rotary 2 (CC 22)**, **Env 3 → rotary 3 (CC 23)**,
   **Env 4 → rotary 4 (CC 24)**. Nomor CC bisa diubah di panel Shortcuts.
7. Drag parameter warna ke tiap rotary: `Hue Rotate`, `Colorize` → `Hue`,
   atau `Add/Subtract` → `R`/`G`/`B`.
8. Atur **In/Out**, **Invert**, dan **Dial Range** per parameter.
9. Simpan **Shortcut Preset** — mapping MIDI tidak ikut tersimpan di `.avc`.

Dengan begitu Env 1–4 menggerakkan empat rotary Dashboard secara independen.

Satu CC yang sama boleh di-map ke beberapa dial sekaligus: dial clip, dial layer,
dan dial composition. Dengan begitu satu Env bisa memodulasi warna di beberapa
scope tanpa duplikasi.

**Env mengikuti Fold.** Lane envelope dipetakan dari daftar pitch unik pada
`Notes`, diurutkan dari nada tertinggi (sama seperti baris Fold di piano roll):
pitch tertinggi → Env 1, berikutnya → Env 2, dan seterusnya. Kalau hanya ada 2
pitch, hanya Env 1–2 yang aktif; Env 3–4 tetap 0.

Catatan teknis: Resolume menyusun daftar parameter saat effect dibuat dan
mengabaikan perubahan visibilitas parameter setelahnya, jadi slot Env 3–4 tetap
terlihat di panel meskipun tidak dipakai. Yang mengikuti jumlah note adalah
**lane yang aktif**, bukan jumlah baris parameter. Kalau suatu saat butuh jumlah
Env yang benar-benar dinamis, perlu plugin/instance terpisah atau jumlah Env
tetap yang dipilih sebelum effect dimuat.

`Notes` berisi:

```json
{
  "res": 32,
  "bars": 4,
  "lengthMode": "auto",
  "loopStart": 0,
  "loopEnd": 128,
  "notes": [[pitch, step, panjang, velocity], ...]
}
```

Maksimum 128 note. `bars` adalah panjang clip dalam bar, `loopStart`/`loopEnd`
adalah titik loop dalam 1/32 step (end eksklusif), dan `lengthMode` menandai
apakah panjang mengikuti Resolume (`auto`) atau dikunci user (`manual`).
Kalau `Notes` kosong, sequencer kembali ke pola 4 lane di `Pattern`. Envelope
tetap 4 lane: lane = `pitch % 4`, jadi note 36–39 memetakan ke Env 1–4 seperti
sebelumnya.

Panjang note menentukan gate: `durasi step × panjang × 0.9`.

`Length` menentukan kerapatan grid, bukan durasi loop. Panjang loop mengikuti
setingan transport clip di Resolume: **BPM Sync** memakai jumlah beat
(`bars = ceil(beat / 4)`), **Timeline** memakai durasi detik
(`beat = detik × BPM / 60`). Saat beat diubah di Resolume, editor langsung
menyesuaikan lebar clip dan men-time-stretch semua note secara proporsional,
lalu menyimpannya ke `Notes` — tidak ada note yang hilang. Kalau panjang diisi
manual lewat field **BARS**, posisi note tidak diubah dan perubahan beat
Resolume diabaikan sampai tombol **Auto** ditekan.

| Length | Kotak per bar | Satu kotak | Aksen beat |
|---|---|---|---|
| 1/4 | 4 | 1 beat | tiap 1 |
| 1/8 | 8 | 1/2 beat | tiap 2 |
| 1/16 | 16 | 1/4 beat | tiap 4 |
| 1/32 | 32 | 1/8 beat | tiap 8 |

Ganti Length tidak menggeser note. Kotak yang tidak terlihat (misalnya kolom
17–32 saat pindah ke 1/16) tetap tersimpan dan muncul lagi saat kembali ke
1/32. Piano roll membaca `steps` dari `/status` dan membangun ulang grid sesuai
jumlah kotak, jadi yang terlihat di editor sama dengan yang berbunyi.

### Pemetaan note

```
lane 0 → note 36
lane 1 → note 37
lane 2 → note 38
lane 3 → note 39
```

## Catatan teknis

- **Tidak ada alokasi di `ProcessOpenGL`.** Semua buffer berukuran tetap.
- **Delta waktu dari `steady_clock`**, dibatasi 0.1 s agar frame tersendat tidak
  memicu ledakan note.
- **Note-off dilacak per note**, antrean tetap 64 slot.
- **Envelope memakai `exp(-dt/tau)`**, jadi kecepatan decay tidak bergantung
  frame rate.
- **`RaiseParamEvent` di-throttle** pada selisih 0.002.
- **Bridge dipoll 6–7× per detik** dari `ProcessOpenGL`, dengan mutex; tidak ada
  thread yang menyentuh state plugin secara langsung.
- **Satu listener per proses**, ref-counted: instance pertama menyalakan,
  instance terakhir mematikan.
- **GLEW statis**, dependensi runtime hanya OpenGL32 / WINMM / SHELL32 / WS2_32.

## Berikutnya

1. Velocity dan panjang note per sel (bukan 4 level).
2. Piano roll multi-bar + panjang pola (16/32/64 step).
3. Sinkronisasi BPM ke transport host (`SetBeatInfo`).
4. Indikator playhead dari plugin ke browser (SSE atau polling ringan).

