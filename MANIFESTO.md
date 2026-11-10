# MANIFESTO — Resolume Midi Roll

> Satu plugin. Satu jendela browser. Satu kurva decay.
> Tidak ada lagi klik-klik untuk memasukkan note.

---

## 01 — Masalahnya

Resolume punya MIDI mapping yang kuat, tapi tidak punya **sequencer**.
Kamu bisa memetakan slider ke CC, tapi kamu tidak bisa menulis *pola*.
Kamu bisa memakai Ableton di belakang, tapi itu berarti satu DAW lagi,
satu window lagi, satu render loop lagi yang harus dijaga tetap sinkron.

Yang sebenarnya dibutuhkan VJ cuma ini:

**Gambar pola. Tekan play. Slider bergerak sendiri.**

---

## 02 — Tiga Pilar

### A. Plugin sebagai Kreator MIDI
Setiap instance punya **channel MIDI sendiri**.
Delapan instance = delapan device berbeda = delapan jalur hardware.
Plugin tidak menunggu DAW. Plugin *adalah* sumbernya.

### B. Edit Mode sebagai Jembatan
Satu tombol. `ShellExecute`. Browser terbuka.
Piano roll penuh layar, kontras tinggi, langsung bisa dimainkan.
Tutup browser — state kembali ke plugin. Tidak ada server, tidak ada install.

### C. Envelope sebagai Keluaran
Note On bukan tujuan akhir — note On adalah **pemicu**.
Setiap trigger melahirkan kurva decay eksponensial yang diekspos
sebagai parameter float FFGL. Parameter itu yang menggerakkan opacity,
blur, scale, apa pun. MIDI masuk, gerak keluar.

---

## 03 — Hukum yang Tidak Bisa Dilanggar

1. **GL thread tidak pernah menunggu.**
   Tidak ada `Sleep`, tidak ada blocking read, tidak ada `ShellExecute` per frame.
   Proses peluncuran browser terjadi tepat sekali, saat state berubah.

2. **Tidak ada alokasi di dalam `ProcessOpenGL`.**
   Buffer envelope berukuran tetap. Dibangun sekali, dipakai selamanya.

3. **Note-off dilacak per nomor note, bukan per waktu.**
   Trigger yang tumpang tindih tidak boleh meninggalkan envelope menggantung.

4. **Plugin tetap hidup tanpa browser.**
   Kalau GUI tidak ada, plugin tetap memutar pola terakhir yang tersimpan.
   Web hanyalah editor, bukan otak.

5. **Tidak ada dependensi yang tidak bisa dijelaskan dalam satu kalimat.**
   FFGL SDK. Win32. Itu saja.

---

## 04 — Estetika

Hitam. Putih. Satu warna aksen yang menyala.
Grid yang terlihat. Note yang tebal. Tidak ada gradien dekoratif.
Antarmuka harus bisa dibaca dari jarak tiga meter di dalam club yang gelap.

Kontras bukan gaya — kontras adalah **keterbacaan di bawah tekanan**.

---

## 05 — Definisi Selesai

- [ ] Plugin load di Resolume, muncul di daftar efek.
- [ ] Channel MIDI bisa diubah per instance dan tersimpan di preset.
- [ ] Tombol Edit Mode membuka piano roll di browser default.
- [ ] Piano roll mengirim Note On/Off ke port virtual (loopMIDI / IAC).
- [ ] Envelope decay menggerakkan parameter float lain secara mulus.
- [ ] Tidak ada frame drop saat 16 note berbunyi bersamaan.
- [ ] Semua state bertahan setelah Resolume ditutup dan dibuka lagi.

---

## 06 — Urutan Pembangunan

1. Skeleton FFGL + mesin envelope.
2. Kontrak data (format pertukaran plugin ↔ web).
3. Piano roll.
4. Peluncur browser + deteksi edit mode.
5. Multi-channel + persistensi preset.
6. Poles.

Satu langkah. Lihat hasilnya. Baru langkah berikutnya.

---

*Dibangun cepat, diuji dengan mata, tidak pernah ditebak.*
