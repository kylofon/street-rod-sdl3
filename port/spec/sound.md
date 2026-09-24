# Sound — porting spec (PC speaker: `0000:0d99`–`0dd8`, `0000:1a05`–`1b27`, sound parts of the ISRs `0000:1fe2` / `0000:238c`, `0f38:6f0a`–`7999`, `2196:0002`)

Addresses as in `port/RE_GUIDE.md` (`SSSS:OOOO` file segments, `DS:xxxx` = DGROUP `3E96`). Confidence:
**verified** = read in the disassembly and, where noted, reproduced by `tools/srsnd.py`; **likely**;
**guess**. Symbols: `port/spec/sound_symbols.csv`. Decoder / renderer: `python tools/srsnd.py
work/SR_unp.exe work/snd` (event lists of every song in `songs.txt`, square-wave WAVs of the songs and
effects, `engine.csv` = RPM → speaker frequency).

Street Rod has **only a PC-speaker driver** (PIT channel 2 + port 61h). There is no AdLib/Tandy/CMS
code: PLAN.md's "probably Tandy / AdLib" is answered *no*. The `-6` Tandy driver draws only; its sound
would be the same speaker code.

---

## 1. Overview

Three independent sound sources share the one speaker:

1. **Music sequencer** (tick-driven, `0000:1fe2` = the normal INT 8 handler, 72.82 Hz). Plays a
   *song structure* (`SongState`, section 4.1) selected by `DS:58D2`: either the **background tune**
   (`DS:79AA`, song data `2e6b:0000`, ≈ 5 min 28 s, loops forever — the jukebox music toggled by the
   *"Catch some tunes" / "Squelch it !"* button and the `M` key) or one of six short **looping sound
   loops** (`DS:79C6`, data in `2f93:0000`–`00fb`: paint hiss, wrench, idle putt-putt, …), which
   temporarily replace the background tune. Notes are PIT divisors with durations in ticks, organised
   in repeatable sections, with a per-section table-driven **vibrato**.
2. **Driving sounds** (tick-driven, `0000:238c` = the INT 8 handler installed while driving, same 72.82
   Hz). The **engine note** (divisor computed from the RPM `DS:78EE`, updated every 4th tick), the
   **police siren** (two-tone, `DS:58DC`) and the **tyre squeal / off-road buzz** (8 kHz blips,
   `DS:58DA` / `DS:5E0E`). The music sequencer does not run while `238c` is installed.
3. **Busy-loop effects** (`0f38:7122`–`7998`): synchronous routines that block the game for a few ms
   to ~0.3 s: frequency warbles, sweeps, random tone bursts (`PIT ch 2 + gate`) and **bit-banged
   noise** (port 61h bit 1 toggled with random software delays, gate off). The delay unit is
   calibrated at start-up (`0f38:703e`). The ISR keeps running during them and can change the speaker
   in the middle of an effect (faithful port must interleave ticks, section 6).

Plus one special case: the **tune-up screen** `0000:7fb1` holds a constant low tone (divisor
`7530h + 400h·|setting|`, 39.8 → 31.2 Hz) while the carburettor/idle setting is adjusted.

Global switches: `DS:47CE` **sound on/off** (Ctrl key, toggled inside the keyboard ISR `0000:2c42`;
initial 1) gates everything; `DS:58D0` **music on/off** (initial 1; `M` key, the jukebox button,
`0f38:7001` mute while driving). `DS:47CF` mirrors the gate bits last written to port 61h.

There is **no digitised sound and no high-rate PIT reprogramming**: channel 0 stays at divisor 4000h
(72.82 Hz) in both ISRs (`1aad`/`1b27` only swap the INT 8 vector). The only PWM code (`0f38:7292`,
`7306`, `73ed`, from `0f38:775a`/`778a`) and a 3-voice pulse-train player (`2196:001d`) are **dead
code** (no callers, no data) — listed, not needed.

### Call graph

```
main 0000:066f
 ├ snd_init 0f38:7101 ── snd_calibrate_delay 0f38:703e ── delay 2196:0002
 │                    └─ spk_set_hz 0f38:70d5 (25000 Hz)
 ├ timer_install 0000:1111 (platform) : PIT0 = 4000h, INT 8 = 0000:1fe2, 43h = B6h
 └ game loop 0000:503f
    ├ game_start 0000:39c0 ── music_start 0000:1a05(2, 10)            (background tune begins)
    ├ menu button -40 / 'M' key (0000:503f, 0000:1417, 0f38:0b51)
    │     └ music_toggle: music_stop 0000:0d99 | music_resume 0000:0dd8
    ├ music_mute 0f38:7001(on/off) ─ music_stop / music_resume
    ├ fx loops 0f38:7548 7573 7613 7661 76da (75c3 76af dead)
    │     ├ song_select 0f38:6f0a(ptr, looptbl) ── music_start 0000:1a05(2, 0)
    │     └ vib_set 0f38:6fbd(...)
    ├ busy effects 0f38:77ba 77e1 7817 7841 7863 790f 7973  (775a 778a dead)
    │     └ spk_on 7122, spk_off 713d, tone 714c, burst 7169, sweep 71b2, warble 71e4,
    │       noise 7464, noise_down 74b0, noise_up 74fc, delay 2196:0002, rng 0f38:5eb6
    ├ tune-up screen 0000:7fb1 (garage)
    └ driving 0000:8ea8 / animation player 0f38:8e48
          ├ drive_isr_install 0000:1aad  (INT 8 = 0000:238c)
          ├ siren_set 0f38:7728, squeal_set 0f38:7741
          └ drive_isr_remove 0000:1b27  (INT 8 = 0000:1fe2)
INT 8: 0000:1fe2 ─ snd_gate_check + music_tick (section 4.3)
       0000:238c ─ drive_sound_tick (section 4.6)
exit 0000:0fea: speaker off
```

---

## 2. Function table

| address | proposed name | signature | purpose | confidence |
|---|---|---|---|---|
| `0000:0d78` | ui_request_redraw | `void far ()` | sets `6C4A`=1, `8B92`=-1, `8BAA`=7D00h, clears `4736/4737`, flushes a key (`2db6`) — see game_flow | verified |
| `0000:0d99` | music_stop | `void far ()` | if the current song plays: stop, speaker gate off, relabel the jukebox button "Catch some tunes" | verified |
| `0000:0dd8` | music_resume | `void far ()` | if music enabled, not playing and started once: play on from where it stopped, relabel "Squelch it !" | verified |
| `0000:0fea` | exit_game | `void far ()` | exit path (INT 24h, quit, fatal): restores timer (`0fb7`), video, **speaker off**, `exit(5)` — see platform | verified |
| `0000:0fb7` | timer_restore | `void far ()` | PIT0 divisor 0 (18.2 Hz), old INT 8 — see platform | verified |
| `0000:1111` | timer_install | `void far ()` | PIT0 = 4000h, INT 8 = `1fe2`, `43h`=B6h (ch 2 mode 3), song structure init — see platform | verified |
| `0000:1a05` | music_start | `void far (int word_off, int delay)` | (re)start the current song at `base + 2·word_off` after `delay+1` ticks | verified |
| `0000:1a7c` | music_rewind | `void far ()` | stop + rewind to base; only caller is dead `0f38:6fec` | verified |
| `0000:1aad` | drive_isr_install | `void far ()` | INT 8 = `238c`, ch 2 divisor 1Eh, gate on if sound on, clears input/steer state | verified |
| `0000:1b27` | drive_isr_remove | `void far ()` | INT 8 = `1fe2`, clears siren/squeal/off-road flags, gate off | verified |
| `0000:1fe2` | isr_timer (sound part) | ISR | per tick: gate off if sound off, then `music_tick` (4.3); rest see platform | verified |
| `0000:238c` | isr_drive (sound part) | ISR | per tick: gate on/off, engine / siren / squeal divisors (4.6); physics see race | verified |
| `0000:7fb1` | tune_screen (sound part) | `void near (Car *car)` | constant tone `7530h + 400h·|car[20h]|` while adjusting — rest see garage | verified |
| `0f38:6f0a` | song_select | `void far (u16 off, u16 seg, u16 loop_tbl)` | `seg:off == 2e6b:0000` → back to the background tune; else start a loop sound on `DS:79C6` | verified |
| `0f38:6fbd` | vib_set | `void far (int on, int row, int period, int depth, int step)` | writes vibrato record 0 (`DS:5826`), `vib_enabled = on` | verified |
| `0f38:6fec` | music_off_rewind | `void far ()` | vibrato off, gate off (no `47CF` update), `1a7c` — **no callers** | verified |
| `0f38:7001` | music_mute | `void far (int mute)` | 1: stop music and remember; 0: resume if muted by us | verified |
| `0f38:703e` | snd_calibrate_delay | `int far (int n)` | counts `delay(n)` calls in one BIOS tick (0:046C) → delay scale | verified |
| `0f38:70d5` | spk_set_hz | `void far (long hz)` | ch 2 divisor = `1234DCh / hz` (`_aFldiv`, low word) | verified |
| `0f38:7101` | snd_init | `void far ()` | `58E2 = calibrate(10)`, `43h`=B6h, 25000 Hz | verified |
| `0f38:7122` | spk_on | `void far ()` | 25000 Hz, port 61h \|= 3, `47CF`=1 | verified |
| `0f38:713d` | spk_off | `void far ()` | port 61h &= FCh, `47CF`=0 | verified |
| `0f38:714c` | fx_tone | `void near (long hz, int d)` | set frequency, `delay(d)` | verified |
| `0f38:7169` | fx_burst | `void near (int lo, int hi, int d, int n)` | n random tones `lo + rng(hi-lo)` of `d` units | verified |
| `0f38:71b2` | fx_sweep_down | `void near (int from, int to, int step, int d)` | tones `from, from-step, … ≥ to` | verified |
| `0f38:71e4` | fx_warble | `void near (int c, int dev, int step, int d, int n)` | n × (up `c-dev → c+dev-step`, down `c+dev → c-dev+step`) | verified |
| `0f38:7292` | pwm_up | `void near (int period, int *duty, int n, int reps)` | software PWM on bit 1 — dead | verified |
| `0f38:7306` | pwm_down | same, table walked backwards — dead | | verified |
| `0f38:737a` | ramp_fill | `int near (int *buf, int from, int to, int step)` | fills a ramp — dead | verified |
| `0f38:73ed` | pwm_swell | `void near (long hz, int period, int to, int step, int reps, int n)` | carrier + PWM swell — only from dead `775a`/`778a` | verified |
| `0f38:7464` | fx_noise | `void near (int range, int ormask, int n)` | gate off; n × (toggle bit 1, `delay(rng(range) \| ormask)`) | verified |
| `0f38:74b0` | fx_noise_down | `void near (int a, int b, int sub, int step, int n)` | noise blocks with range `a` falling to `b` | verified |
| `0f38:74fc` | fx_noise_up | `void near (int a, int b, int sub, int step, int n)` | rising version | verified |
| `0f38:7548` | loop_idle_putt | `void far (int on)` | on: song `2f93:00C0` (32–39 Hz pulses every 9 ticks); off: background | verified (name guess) |
| `0f38:7573` | loop_spray | `void far (int on)` | on: `2f93:00A0` (4811 Hz, sine vibrato) + `vib_set(1,0,8,10,1)` | verified (name guess) |
| `0f38:75c3` | loop_hum100 | `void far (int on)` | `2f93:00B0` (100 Hz) + `vib_set(1,0,8,5,1)` — **no callers** | verified |
| `0f38:7613` | loop_tick | `void far (int on)` | `2f93:0000` (131–156 Hz blips) + `vib_set(1,1,8,1Eh,1)` | verified (name guess) |
| `0f38:7661` | loop_crank | `void far (int on)` | `2f93:002C` (33–49 Hz) + `vib_set(1,1,8,14h,1)` | verified (name guess) |
| `0f38:76af` | loop_rumble | `void far (int on)` | `2f93:0050` (18–19 Hz) — **no callers** | verified |
| `0f38:76da` | loop_wrench | `void far (int on)` | `2f93:007C` (7 kHz / 30–90 Hz alternation) + `vib_set(1,1,8,0Eh,1)` | verified (name guess) |
| `0f38:7728` | siren_set | `void far (int on)` | `DS:58DC = on != 0` | verified |
| `0f38:7741` | squeal_set | `void far (int on)` | `DS:58DA = on != 0` | verified |
| `0f38:775a` | fx_pwm_a | `void far ()` | `pwm_swell(30000, 300, 100, 6, 1, 2)` — **no callers** | verified |
| `0f38:778a` | fx_pwm_b | `void far ()` | `pwm_swell(30000, 200, 70, 5, 1, 3)` — **no callers** | verified |
| `0f38:77ba` | fx_click | `void far ()` | warble(3000, 10, 10, 60, 9) — UI button press (`0f38:5910`) | verified |
| `0f38:77e1` | fx_chirp_hi | `void far ()` | warble(6200+rng(100), 40, 10, 1, 10) | verified |
| `0f38:7817` | fx_chirp_lo | `void far ()` | warble(5200, 40, 10, 1, 10) | verified |
| `0f38:7841` | fx_clank | `void far ()` | noise(200, 10, 60) | verified |
| `0f38:7863` | fx_crash | `void far ()` | 3 × (burst, falling noise, noise, rising noise) | verified |
| `0f38:790f` | fx_hit | `void far ()` | two bursts + noise (driving collisions) | verified |
| `0f38:7973` | fx_thud | `void far ()` | sweep_down(130, 90, 4, 1000) | verified |
| `2196:0002` | delay | `void far (u16 n)` | busy loop of `(n·DS:58E2) >> 8` `LOOP`s | verified |
| `2196:001d` | pulse3_play | `void far (u16 *list)` | 3-voice pulse-train player on bit 1 — **no callers, no data** | verified |
| `0f38:5eb6` | rng | `int far (int n)` | random 0..n-1 (n < 1 reseeds) — see platform | verified |

Callers of the entry points (for the other specs; screen names are guesses where marked):

| entry | call sites | context |
|---|---|---|
| `music_start 1a05` | `0000:39c0` (2, 10) | game start/title → background tune starts 11 ticks later (see game_flow) |
| `music_stop / resume` toggle | `0000:503f` case -40 (jukebox button), `0000:1417` button -40 (only if `58D4 == 1`), `0f38:0b51` key `M` | see game_flow |
| `music_mute 7001` | (1): `0000:8ea8` (starting to drive, only if `DS:51EC == 0`), `0f38:be93` @`c78b`; (0): `0000:503f`, `6b06`, `ab8b` ×4, `b8a1`, `be93` @`cc98` | see game_flow/race |
| `loop_idle_putt 7548` | `0f38:b82c` @`bb54`(1) `bb79`(0) `bd75`(1) `bd9a`(0) | dialog `be93` (guess: engine idling) |
| `loop_spray 7573` | `0000:7367` @`758f`(1) `764b`(0) | garage, paint job (guess) |
| `loop_tick 7613` | `0000:b8a1` @`bb2f`(0) `bc14`(1) `bda9`(0) `be68`(1); `0f38:1e93` @`1eb9`(1) `1f3c`(0) | countdown / waiting (guess) |
| `loop_crank 7661` | `0f38:be93` @`c8a2`(1) `c94b`(0) `cbe5`(1) `cc8d`(0) | animation in dialog `be93` (guess: starting the engine) |
| `loop_wrench 76da` | `0000:7aa4` @`7cc3`(1) `7cdd`(0); `0000:81ab` @`87aa`/`87c4`, `8a8b`/`8aae`, `8acb`/`8ae4` | garage, installing/removing parts (guess) |
| `fx_click 77ba` | `0f38:5910` @`5a90`, `5c15` | UI button pressed |
| `fx_chirp_hi 77e1` | `0000:7aa4` @`7d1a` `7dd7` `7ec4`; `0000:81ab` @`8a38` `8b98` `8d06` | garage (see garage) |
| `fx_chirp_lo 7817` | `0000:7aa4` @`7ce5`; `0000:81ab` @`87cc` `8ab6` | garage |
| `fx_clank 7841` | `0000:711a` @`730a` (body shop), `0000:c52c` @`c58b`, `0000:c5ac` @`c5f3` | garage / results (see race) |
| `fx_crash 7863` | `0000:c1d8` @`c1f8` | wreck sequence (see race) |
| `fx_hit 790f` | `0000:da25` @`dc13` `dc65` `dc6a` `dca4` | collisions while racing (see race) |
| `fx_thud 7973` | `0000:7aa4` @`7dfc` `7e95`; `0000:81ab` @`8bc3` `8cda`; `0f38:31a5` @`325b`; `0f38:37fc` @`3902` | garage (count down/up clicks) |
| `siren_set 7728` | `0000:8e2d` (0), `0000:c613` (0), `0000:da25` @`db47`(0) `db65`(1, police state 3) | see race |
| `squeal_set 7741` | `0000:8e2d` (0); `DS:58DA` also written directly by `0000:da25`/`d992` (hysteresis on \|`DS:78E4`\|: on > 11, off < 10) | see race |
| `drive_isr_install 1aad` | `0000:8ea8` (cruise and race start), `0f38:8e48` (animation player modes 1/2/4/6 unless `DS:5A24 == 2`) | see race / video |
| `drive_isr_remove 1b27` | `0000:8ea8`, `0000:d031`, `0f38:8e48` (unless `DS:5A24 == 1`) | see race / video |
| `tune_screen 7fb1` | `0000:81ab` | garage |

---

## 3. Globals table

| DS offset | proposed name | type | meaning | written by | read by |
|---|---|---|---|---|---|
| `47CE` | snd_enabled | u8 | master sound switch, init 1, Ctrl toggles (`2c42`/`2e93`, key-table value 1Dh) | kbd ISR | ISRs, all effects, `1aad`, `7fb1` |
| `47CF` | spk_gate | u8 | 1 after `61h \|= 3`, 0 after `61h &= FCh` (mirror, not a read of the port) | every gate write | ISRs, `7fb1`, `1417` |
| `58D0` | music_enabled | u16 | music switch, init 1 | toggles, `6f0a`, `7001` | `1a05`, `0dd8`, `6f0a`, `1417` |
| `58D2` | music_cur | u16 near ptr | current `SongState`, init `79AA` | `6f0a` | sequencer, `0d99`, `0dd8`, `1a05` |
| `58D4` | music_mode | u16 | 1 background tune, 2 loop sound (init 1) | `6f0a`, `7fb1` | `1417`, `7fb1` |
| `58D6` | music_enabled_saved | s16 | `58D0` saved while a loop sound plays, -1 none (init FFFFh) | `6f0a` | `6f0a` |
| `58D8` | music_muted | u16 | 1 = `7001(1)` stopped the music | `7001` | `7001` |
| `58DA` | squeal_on | u16 | tyre squeal (driving) | `7741`, `1b27`, `da25`, `d992` | `238c` |
| `58DC` | siren_on | u16 | police siren | `7728`, `1b27` | `238c` |
| `58DE` | pulse3_ptr | u16 | dead `2196:001d` | | |
| `58E0` | pulse3_delay | u16 | dead `2196:001d` (init 1) | | |
| `58E2` | delay_scale | u16 | `delay()` scale from calibration (init 1) | `7101` (`703e` sets 100h meanwhile) | `2196:0002` |
| `5826` | vib_rec[5] | 5 × 10 B | vibrato records `{s16 phase, u16 row, u16 depth, s16 period, u16 step}` | `6fbd` (rec 0), sequencer (phase) | sequencer |
| `57F6` | vib_wave[3][8] | s16 | row 0 `0 1 2 1 0 -1 -2 -1`, row 1 `0..7`, row 2 `0 1 0 0 0 0 0 0` | const | sequencer |
| `5858` | bg_loop_counts | s16[19] | extra plays per section of the background tune, -1 end: `2 1 0 2 2 0 2 1 0 1 0 0 2 2 0 2 1 0 -1` | const | sequencer |
| `5882` | bg_vib_index | u16[37] | vibrato record per section play (36 entries + FFFFh) | const | sequencer |
| `587E` | fx_loop_counts | s16[2] | `0, -1` (loop sounds: 1 section, repeat forever) | const | sequencer |
| `58CC` | fx_vib_index | u16[2] | `0, FFFFh` (record 0 = the one `6fbd` sets) | const | sequencer |
| `72EC` | vib_enabled | u16 | vibrato on | `1a05`, `6f0a`, `6fbd` | sequencer |
| `82C4` | vib_rec_ptr | u16 | `5826 + 10·index` of the current section | `1a05`, `6f0a`, sequencer | sequencer |
| `06E2` | vib_countdown | u16 | ticks to the next vibrato step | sequencer | sequencer |
| `6C52` | vib_amount | u16 | `((depth·note) & FFFFh) / 1000` for the current note | sequencer | sequencer |
| `6C54` | vib_last_div | u16 | last vibrato divisor (write-only) | sequencer | — |
| `79AA` | bg_song | SongState (1Ch) | background tune (init by `1111`) | `1111`, sequencer | sequencer |
| `79C6` | fx_song | SongState (1Ch) | loop sounds | `6f0a`, sequencer | sequencer |
| `2BA9` | lbl_squelch | u16 | 0336h: label offset of "Squelch it !" (`DS:239E + 336h = DS:26D4`) | const | `0dd8` |
| `2BAB` | lbl_tunes | u16 | 0325h: "Catch some tunes" (`DS:26C3`) | const | `0d99` |
| `06E4` | siren_count | u16 | siren tone counter 0..20 | `238c` | `238c` |
| `06EA` | squeal_phase | u8 | toggles on the siren/squeal ticks | `238c` | `238c` |
| `0608` | tick_div4 | u8 | 3,2,1,0 tick counter (BIOS chained at 0) — platform | ISRs | ISRs |
| `0286` | anim_mode | u16 | 1 while the animation player `0f38:8e48` runs (alternative engine formula) — see video | `8e48`, `1abd` | `238c` |
| `5A0E` | anim_engine_snd | u16 | 1 while `8e48` runs | `8e48` | `238c` |
| `825E`, `8B7C` | anim values | s16 | their difference drives the animation engine note — see video | video | `238c` |
| `78EE` | engine_rpm | s16 | RPM (idle 5DCh=1500, limiter 1644h=5700) — see race | race / `238c` physics | `238c` sound |
| `5E0E` | off_road | u16 | car on the verge (`2645:206b`) → buzz — see race | `2645:206b`, `1b27` | `238c` |

`SongState` (28 bytes):

| off | field | meaning |
|---|---|---|
| +00 | u16 note | PIT divisor of the sounding note (0 = rest) |
| +02 | u16 dur | ticks left of the current event |
| +04 | s16 rep | plays of the current section so far |
| +06 | u8 playing | |
| +07 | u8 regate | 1 = the gate must be switched on at the next note |
| +08 | far base | song start; events start at `base+4` (4-byte header `0,0` never read) |
| +0C | far sec_start | start of the current section |
| +10 | far cur | read pointer |
| +14 | u16 loop_tbl | near ptr: s16 extra plays per section, negative = end of song → restart |
| +16 | u16 loop_ptr | current entry |
| +18 | u16 vib_tbl | near ptr: u16 vibrato record index per section *play* |
| +1A | u16 vib_ptr | current entry |

Initial values (`0000:1111`): `bg_song.base = sec_start = cur = 2e6b:0000`, `loop_tbl = loop_ptr =
5858h`, `vib_tbl = vib_ptr = 5882h`; `fx_song.playing = 0`, `fx_song.vib_tbl = vib_ptr = 58CCh`. The
rest of both structures is 0 in the file.

---

## 4. Pseudocode

Helpers used below (port: section 6):

```c
#define SPK_ON()   do { port61 |= 3;    DSB(0x47CF) = 1; } while (0)   /* in al,61h / or al,3 / out */
#define SPK_OFF()  do { port61 &= 0xFC; DSB(0x47CF) = 0; } while (0)   /* and ax,00FCh */
static void pit2_div(u16 d);            /* out 42h,lo ; out 42h,hi (mode 3 set once: 43h = B6h) */
```

### 4.1 Start / stop / resume / mute

```c
void music_start(int word_off, int delay)            /* 0000:1a05 */
{
    SongState *s = (SongState *)DSW(0x58D2);
    if (DSW(0x58D0) == 0) { s->playing = 0; return; }
    s->playing = 0;
    SPK_OFF();
    s->rep = 0;
    s->regate = 1;
    s->cur = s->sec_start = far_add(s->base, word_off * 2);     /* offset += 2*word_off, seg kept */
    s->dur = delay + 1;
    s->vib_ptr = s->vib_tbl;
    DSW(0x82C4) = 0x5826 + 10 * DSW(s->vib_ptr);                 /* imul, low word */
    DSW(0x72EC) = 1;
    s->playing = 1;
    /* NOTE: loop_ptr (+16) is NOT reset (quirk: a restart of the background tune keeps the section
       counter table position; only 6f0a sets it for loop sounds) */
}

void music_stop(void)                                 /* 0000:0d99 */
{
    SongState *s = (SongState *)DSW(0x58D2);
    if (!s->playing) return;
    s->playing = 0;
    SPK_OFF();
    s->regate = 1;
    int slot = ui_find_button(-40);                   /* 0000:6827: slot 74h..85h, or -40 if absent */
    DSW(0x09F8 + slot * 0x12) = DSW(0x2BAB);          /* label "Catch some tunes" */
    /* quirk: if button -40 is not on screen, slot = -40 and the write hits DS:0728 */
    ui_request_redraw();                              /* 0000:0d78 */
}

void music_resume(void)                               /* 0000:0dd8 */
{
    SongState *s = (SongState *)DSW(0x58D2);
    if (DSW(0x58D0) == 0 || s->playing) return;
    if (s->sec_start == s->base) return;              /* never started (or ended): nothing to resume */
    s->playing = 1;
    s->regate = 1;
    int slot = ui_find_button(-40);
    DSW(0x09F8 + slot * 0x12) = DSW(0x2BA9);          /* label "Squelch it !" */
    ui_request_redraw();
}

void music_toggle(void)            /* inline in 0000:503f (button -40), 0000:1417 (button -40, only if
                                      DSW(0x58D4) == 1), 0f38:0b51 (key 'M') */
{
    if (DSW(0x58D0) == 0) { DSW(0x58D0) = 1; music_resume(); }
    else                  { music_stop();    DSW(0x58D0) = 0; }
}

void music_mute(int mute)                             /* 0f38:7001 */
{
    if (mute) {
        if (DSW(0x58D0) == 0) { DSW(0x58D8) = 0; return; }
        music_stop(); DSW(0x58D0) = 0; DSW(0x58D8) = 1;
    } else {
        if (DSW(0x58D8) == 0) return;
        DSW(0x58D0) = 1; music_resume(); DSW(0x58D8) = 0;
    }
}

void music_rewind(void)            /* 0000:1a7c (only from dead 0f38:6fec) */
{
    SongState *s = (SongState *)DSW(0x58D2);
    s->playing = 0; SPK_OFF(); s->regate = 1;
    s->cur = s->sec_start = s->base;
}
```

### 4.2 Loop sounds

```c
void song_select(u16 off, u16 seg, u16 loop_tbl)      /* 0f38:6f0a */
{
    if (off == DSW(0x79B2) && seg == DSW(0x79B4)) {   /* == bg_song.base (2e6b:0000): back to the tune */
        if ((s16)DSW(0x58D6) != -1) { DSW(0x58D0) = DSW(0x58D6); DSW(0x58D6) = 0xFFFF; }
        if (DSW(0x58D0) == 0) { port61 &= 0xFC; DSB(0x47CF) = 0; }
        DSW(0x58D4) = 1;
        DSW(0x58D2) = 0x79AA;                          /* the tune continues where it was (its own
                                                          state was never touched) */
        DSW(0x82C4) = 0x5826 + 10 * DSW(DSW(0x79C4));  /* record of its current section play */
        DSW(0x72EC) = 1;
        /* 06E2 / 6C52 keep the loop sound's values until the tune's next note */
    } else {
        if ((s16)DSW(0x58D6) == -1) { DSW(0x58D6) = DSW(0x58D0); DSW(0x58D0) = 1; }  /* plays even
                                                          when music is off */
        DSW(0x58D4) = 2;
        fx_song.base = fx_song.sec_start = fx_song.cur = (seg:off);
        fx_song.loop_tbl = fx_song.loop_ptr = loop_tbl;   /* always 587Eh */
        DSW(0x58D2) = 0x79C6;
        music_start(2, 0);                             /* first event on the next tick */
    }
}

void vib_set(int on, int row, int period, int depth, int step)   /* 0f38:6fbd */
{
    if (on) { DSW(0x5826) = 0; DSW(0x5828) = row; DSW(0x582A) = depth;
              DSW(0x582C) = period; DSW(0x582E) = step; }
    DSW(0x72EC) = on;
}

/* 0f38:7548 7573 75c3 7613 7661 76af 76da: identical shape */
void loop_X(int on)
{
    if (on) { song_select(OFF_X, 0x2F93, 0x587E); if (HAS_VIB_X) vib_set(1, ROW, 8, DEPTH, 1); }
    else      song_select(0x0000, 0x2E6B, 0x5858);
}
/*  7548: OFF 00C0, no vib_set (record 0 keeps the last values; all-zero record = no vibrato)
    7573: OFF 00A0, vib_set(1, 0, 8, 0x0A, 1)
    75c3: OFF 00B0, vib_set(1, 0, 8, 0x05, 1)   (no callers)
    7613: OFF 0000, vib_set(1, 1, 8, 0x1E, 1)
    7661: OFF 002C, vib_set(1, 1, 8, 0x14, 1)
    76af: OFF 0050, no vib_set                   (no callers)
    76da: OFF 007C, vib_set(1, 1, 8, 0x0E, 1)  */
```

### 4.3 Timer ISR `0000:1fe2` — sound part (every tick, 72.82 Hz)

Runs after the tick counters / BIOS chaining (platform) and before the mouse/joystick part:

```c
void isr1fe2_sound(void)
{
    if (DSB(0x47CE) == 0 && DSB(0x47CF) != 0) SPK_OFF();     /* 2028 */
    /* ... demo/mouse part (platform) ... then at 20b8: */
    music_tick();
}

void music_tick(void)                                         /* 0000:20b8-2295 */
{
    SongState *s = (SongState *)DSW(0x58D2);
    if (!s->playing) return;
    if (--s->dur != 0) {
        if (DSW(0x72EC) == 0 || s->note == 0) return;
        if (--DSW(0x06E2) != 0) return;
        u16 *rec = (u16 *)DSW(0x82C4);
        s16 r = (s16)rec[0] % (s16)rec[3];                    /* idiv: phase % period (period 0 never
                                                                 reached: step 0 => countdown wraps) */
        rec[0] = r + 1;
        s16 w = DSW(0x57F6 + rec[1] * 16 + r * 2);            /* wave[row][r] (no bound check) */
        u16 div = (u16)(w * (s16)DSW(0x6C52)) + s->note;      /* imul low word + note */
        DSW(0x6C54) = div;
        pit2_div(div);
        DSW(0x06E2) = rec[4];                                 /* step */
        return;
    }
    for (;;) {                                                /* 21f1: next event(s) */
        s->dur = far_rd16(&s->cur);
        if (s->dur != 0) break;
        u16 mark = far_rd16(&s->cur);                         /* section marker (value unused) */
        if (mark == 0) {                                      /* end of song */
            SPK_OFF();
            s->playing = 0; s->regate = 1;
            s->cur = s->sec_start = s->base;
            return;
        }
        s->vib_ptr += 2;
        s->rep++;
        if ((s16)DSW(s->loop_ptr) >= s->rep) {
            s->cur = s->sec_start;                            /* play the section again */
        } else {
            s->loop_ptr += 2;
            if ((s16)DSW(s->loop_ptr) < 0) {                  /* end of table: whole song again */
                s->cur = far_add(s->base, 4);
                s->loop_ptr = s->loop_tbl;
                s->vib_ptr  = s->vib_tbl;
            }
            s->sec_start = s->cur;
            s->rep = 0;
        }
        DSW(0x82C4) = 0x5826 + 10 * DSW(s->vib_ptr);
    }
    s->note = far_rd16(&s->cur);
    if (s->note == 0) {                                       /* rest */
        s->regate = 1;
        SPK_OFF();
        return;
    }
    if (DSW(0x72EC)) {
        u16 *rec = (u16 *)DSW(0x82C4);
        DSW(0x06E2) = rec[4];
        DSW(0x6C52) = (u16)(rec[2] * s->note) / 1000;         /* mul; dx CLEARED before div: only the
                                                                 low 16 bits of depth*note count */
    }
    pit2_div(s->note);
    if (DSB(0x47CE) != 0 &&
        (DSB(0x47CF) != DSB(0x47CE) || s->regate)) {
        SPK_ON();
        s->regate = 0;
    }
}
```

A note of duration *d* sounds for *d* ticks (fetched on the tick the counter reaches 0). Section
markers and song restart are handled in the same tick (no gap). A rest only clears the gate: the
divisor keeps its old value.

### 4.4 Tune-up screen `0000:7fb1` (sound part only)

```c
    music_stop();                      /* 0d99 */
    DSW(0x58D4) = 2;                   /* blocks the jukebox button in 1417 */
    if (DSB(0x47CE)) SPK_ON();
    do {
        if (DSB(0x47CE) && DSB(0x47CE) != DSB(0x47CF)) SPK_ON();
        s8 v = car[0x20];              /* setting -8..4 */
        pit2_div(0x7530 + (abs(v) << 10));        /* 30000..38192 -> 39.8..31.2 Hz */
        ... draw ...
        while ((k = ui_poll(0)) == 0)            /* 0000:1417 */
            if (DSB(0x47CE) && DSB(0x47CE) != DSB(0x47CF)) SPK_ON();
        if (k == -10 && ++car[0x20] > 4) car[0x20] = 4;
        else if (k == -7 && --car[0x20] < -8) car[0x20] = -8;
    } while (k != -1);
    SPK_OFF();
    DSW(0x58D4) = 1;
    music_resume();                    /* 0dd8 */
```

### 4.5 Driving ISR install / remove

```c
void drive_isr_install(void)                        /* 0000:1aad */
{
    DSW(0x060A) = 0;
    setvect(8, isr_drive);                          /* 0000:238c; PIT0 divisor unchanged (4000h) */
    if (DSW(0x0610) == 0 && DSB(0x05F2)) { mouse_read_delta(); DSW(0x05E6) = DSW(0x05E8) = 0;
                                            DSW(0x05EA) = 1; DSW(0x05EC) = 6; }   /* platform */
    DSB(0x4736) = DSB(0x4732) = DSB(0x4731) = DSB(0x4730) = 0;
    DSW(0x78E2) = DSW(0x78E4) = 0;                  /* race: steering momentum */
    DSB(0x0609) = 0;                                /* race: physics divider */
    pit2_div(0x001E);                               /* 39.8 kHz: inaudible until the engine note */
    if (DSB(0x47CE)) SPK_ON();
}

void drive_isr_remove(void)                         /* 0000:1b27 */
{
    setvect(8, isr_timer);                          /* 0000:1fe2 */
    DSW(0x58DC) = 0; DSW(0x58DA) = 0; DSW(0x5E0E) = 0;
    SPK_OFF();
    if (DSW(0x0610) == 0 && DSB(0x05F2)) { mouse_read_delta(); DSW(0x05E6) = DSW(0x05E8) = 0;
                                            DSW(0x05EA) = 1; DSW(0x05EC) = 1; }
    DSB(0x4736) = DSB(0x4732) = DSB(0x4731) = DSB(0x4730) = 0;
}
```

The music structure is untouched: the tune (if not muted by `7001`) freezes while driving and goes
on from the same note after `1b27` (at its next note change, since the gate was switched off).

### 4.6 Driving ISR `0000:238c` — sound part

`tick_div4` (`DS:0608`) runs 3,2,1,0: the ISR reads it, decrements, and on the old value 0 chains
the BIOS and reloads 3 (and runs the physics every 3rd such tick = 6.07 Hz, see race). On the three
other ticks (new value 2, 1, 0) it EOIs and does the sound:

```c
void isr238c_sound(u8 v /* DS:0608 after the decrement: 2, 1 or 0 */)
{
    if (DSB(0x47CE) == 0) {                                   /* 24f6 */
        if (DSB(0x47CF) != 0) SPK_OFF();
        return;
    }
    if (DSB(0x47CF) != DSB(0x47CE)) SPK_ON();                 /* 23dd: keeps the gate on */
    if (v & 1) {                                              /* v == 1: engine note, 18.2 Hz */
        u16 div;
        if (DSW(0x0286) == 0) {
            div = engine_divisor((s16)DSW(0x78EE));
        } else if (DSW(0x5A0E) != 0) {                        /* animation player 0f38:8e48 */
            s16 d = (s16)(DSW(0x825E) - DSW(0x8B7C));
            u16 a = d < 0 ? -d : d;
            u16 t = (u16)((a >> 1) * a);                      /* mul, low word */
            if (t > 20000) t = 20000;                         /* unsigned compare */
            div = 60000 - t;                                  /* 19.9 .. 29.8 Hz */
        } else return;
        pit2_div(div);
    } else {                                                  /* v == 2 or 0 */
        DSB(0x06EA) ^= 1;
        if (DSW(0x58DC)) {                                    /* siren */
            u16 old = DSW(0x06E4)++;
            pit2_div(old < 10 ? 0x054B : 0x0712);             /* 880.6 Hz / 659.2 Hz */
            if ((s16)DSW(0x06E4) > 20) DSW(0x06E4) = 0;
        } else if ((DSW(0x58DA) || DSW(0x5E0E)) && DSB(0x06EA)) {
            pit2_div(0x0095);                                 /* 8008 Hz blip for one tick */
        }
    }
}

u16 engine_divisor(s16 rpm)                                   /* 0000:2404 */
{
    s32 sq = (s32)rpm * rpm;                                  /* _aFlmul 1e16:2488 */
    u16 t  = (u16)(sq >> 10);                                 /* _aFlshr 1e16:255e, low word */
    u8  cl = (u8)t + 2;
    u16 x  = shr16(t, cl);                                    /* SHR AX,CL: see note */
    x >>= 3;
    t += x;
    u16 bonus = (rpm == 0x1644) ? 0x2710 : 0;                 /* rev limiter: jump up */
    return (u16)(0xC350 - (u16)(t + bonus));                  /* neg(ax - C350h) */
}
/* shr16(v, cl): the 286+ (and DOSBox) mask the count to 5 bits: n = cl & 31; n >= 16 ? 0 : v >> n.
   On an 8088 the count is not masked (cl >= 16 -> 0). Use the masked form. */
```

Engine note: 1500 RPM (idle 5DCh) → divisor 47803 = 25.0 Hz; 3000 → 29.0 Hz; 5000 → 53.0 Hz; 5699 →
65.3 Hz; exactly 5700 (limiter) → 144 Hz. The odd `x` term adds small pitch jumps for some RPMs (see
`work/snd/engine.csv`). Resulting pattern per 4 ticks: `[siren|squeal|—] [engine] [siren|squeal|—]
[—]`: the siren shares the speaker with the engine (2 ticks siren, 2 ticks engine), the squeal is an 8
kHz blip on one tick of four (`06EA` alternates on the two siren/squeal ticks), cycle 21 half-steps:
10 × 880 Hz then 11 × 659 Hz, i.e. ≈ 0.58 s per siren period.

### 4.7 Busy-loop effects (`0f38:7101`–`7998`)

```c
void snd_init(void)                                   /* 0f38:7101, from main 0000:07d3 */
{
    DSW(0x58E2) = snd_calibrate_delay(10);
    out(0x43, 0xB6);
    spk_set_hz(25000);
}

int snd_calibrate_delay(int n)                        /* 0f38:703e */
{
    DSW(0x58E2) = 0x100;
    u32 t = BIOS_TICKS;  while (BIOS_TICKS == t) ;    /* 0000:046C */
    t = BIOS_TICKS;
    int cnt = 0;
    while (BIOS_TICKS == t) { delay(n); cnt++; }      /* calls in one 18.2 Hz tick (54.9 ms) */
    if (cnt > 0x350) cnt = sar16(abs16((s16)(3 * cnt)), 2) with sign;   /* 3/4, 16-bit imul */
    return cnt;
}

void delay(u16 n)                                     /* 2196:0002 */
{
    u32 p = (u32)n * DSW(0x58E2);
    u16 inner = (u16)(p >> 8);  u16 outer = (u16)(p >> 24);
    do { u16 c = inner; do { } while (--c); } while ((s16)--outer >= 0);   /* LOOP, inner 0 = 65536 */
}

void spk_set_hz(s32 hz) { pit2_div((u16)(0x1234DC / hz)); }   /* 0f38:70d5, _aFldiv 1e16:23ec */
void spk_on(void)  { spk_set_hz(25000); SPK_ON(); }            /* 0f38:7122 */
void spk_off(void) { SPK_OFF(); }                              /* 0f38:713d */
void fx_tone(s32 hz, u16 d) { spk_set_hz(hz); delay(d); }      /* 0f38:714c */

void fx_burst(int lo, int hi, u16 d, int n)                    /* 0f38:7169 */
{ for (; n > 0; n--) fx_tone(rng(hi - lo) + lo, d); }          /* note: loop body runs n times
                                                                   (do/while guarded by n > 0) */
void fx_sweep_down(int f, int to, int step, u16 d)             /* 0f38:71b2 */
{ for (; f >= to; f -= step) fx_tone(f, d); }
void fx_warble(int c, int dev, int step, u16 d, int n)         /* 0f38:71e4 */
{
    for (; n > 0; n--) {
        for (int i = 0, f = c - dev; i < 2 * dev; i += step, f += step) fx_tone(f, d);
        for (int i = 0, f = c + dev; i < 2 * dev; i += step, f -= step) fx_tone(f, d);
    }
}
void fx_noise(int range, int ormask, int n)                    /* 0f38:7464 */
{
    spk_off();                                                 /* gate 0: speaker follows bit 1 */
    for (; n > 0; n--) {
        port61 ^= 2;                                           /* in/xor/out, 47CF not touched */
        delay(rng(range) | ormask);
    }
}
void fx_noise_down(int a, int b, int sub, int step, int n)     /* 0f38:74b0 */
{ if (b < a) { int x = a; do { fx_noise(a, a - sub, n); a -= step; x -= step; } while (b < x); } }
void fx_noise_up(int a, int b, int sub, int step, int n)       /* 0f38:74fc */
{ if (a < b) { int x = a; do { fx_noise(a, a - sub, n); a += step; x += step; } while (x < b); } }

/* Every public effect starts with `if (DSB(0x47CE) == 0) return;` */
void fx_click(void)    { spk_on(); fx_warble(3000, 10, 10, 60, 9);               spk_off(); } /* 77ba */
void fx_chirp_hi(void) { spk_on(); fx_warble(rng(100) + 0x1838, 40, 10, 1, 10);  spk_off(); } /* 77e1 */
void fx_chirp_lo(void) { spk_on(); fx_warble(0x1450, 40, 10, 1, 10);             spk_off(); } /* 7817 */
void fx_clank(void)    { spk_on(); fx_noise(200, 10, 60);                         spk_off(); } /* 7841 */
void fx_crash(void)                                                                           /* 7863 */
{
    for (int i = 3; i; i--) {
        spk_on();
        fx_burst(3000, 8000, 60, 10);
        fx_noise_down(200, rng(30) + 18, 15, 5, 2);
        fx_noise(rng(300) + 40, 2, 30);
        fx_noise_up(16, rng(100) + 50, 15, 5, 4);
    }
    spk_off();
}
void fx_hit(void)                                                                             /* 790f */
{
    spk_on(); fx_burst(2000, 8000, 100, 10); fx_noise(120, 2, 30);
    spk_on(); fx_burst(4000, 9000, 100, 10); fx_noise(220, 2, 20);
    spk_off();
}
void fx_thud(void)     { spk_on(); fx_sweep_down(130, 90, 4, 1000);               spk_off(); } /* 7973 */
```

Argument evaluation order matters for the RNG stream: the compiler calls `rng()` for the argument
that needs it before pushing the constant ones; each effect calls `rng` in the order written above
(`fx_burst`: once per tone; `fx_noise`: once per toggle). `rng` = `0f38:5eb6` (shared with the whole
game — see platform).

Dead code, for completeness (not ported): `0f38:775a` = `spk_on(); pwm_swell(30000, 300, 100, 6, 1,
2); spk_off();`, `0f38:778a` = `…(30000, 200, 70, 5, 1, 3)…`; `pwm_swell` sets the carrier to 30 kHz
and, `n` times, walks a duty ramp `1, 1+step, … ≤ to` up (`7292`) and down (`7306`): per duty value
`reps` × (bit 1 high `delay(duty)`, low `delay(period-duty)`) — a PWM "swell" of the ultrasonic
carrier. `2196:001d` plays 3-word records `{count, voice-1 period byte, voice-2/3 period bytes}` as
one-iteration pulses on bit 1 with `DS:58E0` loop delay per step; nothing references it.

---

## 5. Data formats (in-EXE tables)

### 5.1 Songs (`2e6b:0000` background, `2f93:0000`–`00FB` loop sounds)

Little-endian u16 words:

```
header   u16 0, u16 0                     (skipped: playback starts at base+4)
event    u16 dur (ticks, >0), u16 div     note (div = PIT divisor, 1193182/div Hz) or rest (div 0)
marker   u16 0, u16 m (m != 0, always 1)  end of a section
end      u16 0, u16 0                     end of song (stops; never reached by the shipped data,
                                          whose loop tables end in -1 = restart)
```

Section repetition: the song's loop table (s16 per section) gives *extra* plays (0 = once, 2 =
three times); a negative entry after the last section restarts the song at `base+4`. The vibrato
table has one entry per section *play* (sum of plays = 36 for the tune), each an index into the
vibrato records.

| song | data | size | loop tbl | vib tbl | vib records used | length of one pass |
|---|---|---|---|---|---|---|
| background tune | `2e6b:0000` | 1278h (2364 words, 18 sections) | `DS:5858` | `DS:5882` | 1–4 | 23 900 ticks = 5 min 28 s |
| `7613` | `2f93:0000` | 2Ch | `DS:587E` | `DS:58CC` | 0 (`vib_set` row 1, depth 30) | 16 ticks |
| `7661` | `2f93:002C` | 24h | 〃 | 〃 | 0 (row 1, depth 20) | 28 ticks |
| `76af` (dead) | `2f93:0050` | 2Ch | 〃 | 〃 | 0 (as left) | 32 ticks |
| `76da` | `2f93:007C` | 24h | 〃 | 〃 | 0 (row 1, depth 14) | 26 ticks |
| `7573` | `2f93:00A0` | 10h | 〃 | 〃 | 0 (row 0, depth 10) | 36 ticks |
| `75c3` (dead) | `2f93:00B0` | 10h | 〃 | 〃 | 0 (row 0, depth 5) | 7 ticks |
| `7548` | `2f93:00C0` | 3Ch | 〃 | 〃 | 0 (as left) | 36 ticks |

The tune starts `C6 A5 G5 F5 E5 C5` (divisors 1140 1355 1521 1708 1809 2280, 5 ticks each) — full
event list in `work/snd/songs.txt`.

### 5.2 Vibrato

Records `DS:5826 + 10·i`, `{s16 phase, u16 row, u16 depth, s16 period, u16 step}` (file values):
`0: 0 0 0 0 0` (set by `vib_set`), `1: 0 0 0 8 10` (depth 0: none), `2: 0 0 4 8 1`, `3: 0 0 6 8 1`,
`4: 0 2 5 2 1`. Waves `DS:57F6` (s16[8] per row): row 0 triangle `0 1 2 1 0 -1 -2 -1`, row 1 ramp
`0 … 7`, row 2 `0 1 0 0 0 0 0 0`. Divisor = `note + wave[row][phase % period] · ((depth · note) &
FFFFh) / 1000`, updated every `step` ticks; the phase lives in the record and is not reset per note.
Record 0 all zero ⇒ `step` 0 ⇒ the countdown wraps and never fires ⇒ no vibrato (and no division
by the zero period).

### 5.3 Button labels

`DS:2BA9` = 0336h, `DS:2BAB` = 0325h are offsets relative to `DS:239E` of the menu strings
"Squelch it !" (`DS:26D4`) and "Catch some tunes" (`DS:26C3`); they are written into the label word
(+0) of the button record with id -40 (+2) in the 18-byte button table at `DS:09F8` (see game_flow).

---

## 6. Hardware dependencies and SDL3 replacement

| original | where | replacement |
|---|---|---|
| `out 43h, B6h` (ch 2, lo/hi, mode 3) | `1111`, `7101` | no-op |
| `out 42h` lo, hi | sequencer, `238c`, `1aad`, `7fb1`, `70d5` | `host_pit2_divisor(div)` |
| `in al,61h / or al,3 / out` | `SPK_ON` everywhere | `host_port61(host_port61_get() \| 3)` |
| `in al,61h / and 0FCh / out` | `SPK_OFF` everywhere, `0fea` | `host_port61(host_port61_get() & ~3)` |
| `in al,61h / xor al,2 / out` | `fx_noise 7464` | `host_port61(host_port61_get() ^ 2)` |
| `out 61h` bit 7 pulse (kbd ack) | `2c42` | none (platform) |
| `delay()` `LOOP` busy wait | `2196:0002` | `host_busy_wait_us(n * SND_DELAY_UNIT_US)` (+ per-call overhead, section 7) |
| BIOS tick poll `0:046C` | `703e` calibration | dropped: `DS:58E2` unused by the port (keep writing a plausible value, e.g. 3432, for memory fidelity) |
| INT 8 vector swap | `1aad`/`1b27`/`1111`/`0fb7` | `host_set_tick_handler(isr_drive / isr_timer)`; PIT0 stays 4000h |

**Needed host extension** (the current `host_speaker(divisor, on)` with per-tick granularity cannot
express the bit-banged noise, whose edges are 10–1000 µs apart, nor gate/data separately):

```c
/* PC speaker at sub-tick resolution. Writes are time-stamped on the host's emulated timeline:
 * tick-handler writes at that tick's time, other writes at (current tick time + busy-wait time
 * accumulated since). */
void host_pit2_divisor(u16 divisor);        /* PIT ch 2 reload (0 = 65536), mode 3 */
void host_port61(u8 value);                  /* bit 0 = timer-2 gate, bit 1 = speaker data */
u8   host_port61_get(void);                  /* last value written (the game reads 61h back) */
void host_busy_wait_us(u32 us);              /* advances the emulated timeline by us; runs the tick
                                                handler for every tick boundary crossed, at its place;
                                                paces against real time and pumps events */
/* host_speaker(div, on) becomes: host_pit2_divisor(div); host_port61(on ? 3 : 0); */
```

Synthesis (host): speaker level(t) = `bit1 && (bit0 ? OUT2(t) : 1)`; OUT2 is a mode-3 square wave of
period `divisor/1193182` s restarted at a gate rising edge (a new divisor takes effect at once — the
PIT would wait for the current half cycle; inaudible). Integrate the 1-bit signal exactly over each
output sample (area of the high parts: handles pulses shorter than a sample and makes the 25 kHz /
39.8 kHz "silent" carriers average out instead of aliasing), then a DC blocker (the speaker is AC
coupled; ~20 Hz high-pass — keep it low, the engine note is 25–65 Hz) and a one-pole low-pass
(~10 kHz). Render with a fixed latency (e.g. 40 ms) behind the emulated timeline so that events of
busy loops arrive in order. `tools/srsnd.py` implements this model (16× oversampled box filter) for
reference renders.

The port-side sound code keeps all state in `mem[]` (song structures, tables, flags) and uses only
the four calls above, so a headless run (`SDL_AUDIO_DRIVER=dummy`) is deterministic.

---

## 7. Timing

* **Ticks**: PIT0 divisor 4000h → 72.8227 Hz in both ISRs (`1fe2` outside driving, `238c` while
  driving). Music durations are in these ticks (≈ 13.73 ms). Vibrato steps every `step` ticks (1 or
  10). The BIOS is chained every 4th tick (18.2 Hz).
* **Driving**: engine divisor refreshed on 1 tick of 4 (18.2 Hz); siren / squeal on the other two
  sound ticks; nothing on the BIOS tick. Car physics in the ISR every 12th tick (6.07 Hz, race).
* **Busy-loop effects** block the caller (and anything else except the ISR). Their duration depends
  on the CPU on the original: `delay(n)` is calibrated (`703e`) so that `delay(10)` + its loop
  overhead fits `DS:58E2` times into a 54.9 ms BIOS tick, and scaled by 3/4 on machines where the
  count exceeds 848 (every machine faster than an XT, and DOSBox). Measured by instruction counting
  (36 instructions per calibration iteration, 1 per `LOOP`), **DOSBox reference: 1 delay unit ≈
  4.47 µs**, independent of the cycles setting; an 8088 gives ≈ 6.5 µs, a 286/386 5.5–7.7 µs. The
  frequency-setting overhead (`_aFldiv` + calls in `714c`) and the `rng` call in `fx_noise` are not
  calibrated: ≈ 20 µs and ≈ 25 µs in DOSBox at 3000 cycles/ms, several times more on an XT (where
  the chirps last ~50 ms instead of ~4 ms). Proposed port constants (`sound.h`):
  `SND_DELAY_UNIT_US 4.47`, `SND_TONE_OVERHEAD_US 20`, `SND_NOISE_OVERHEAD_US 25` — i.e.
  `fx_tone(hz,d)` = `host_pit2_divisor(...)`, `host_busy_wait_us(20 + d·4.47)`; `fx_noise` step =
  `port61 ^= 2`, `host_busy_wait_us(25 + delay·4.47)`. Resulting durations (`tools/srsnd.py`):
  click 10 ms, chirps 4 ms, clank 30 ms, hit 30 ms, thud 49 ms, crash ≈ 270 ms.
* **ISR interleaving**: the ISR keeps running inside every busy loop: while an effect plays, the
  music sequencer (a note change: new divisor, gate on/off) or the driving ISR (engine / siren
  divisor every 1–2 ticks, gate forced back on after `fx_noise` switched it off) change the speaker
  under the effect. `fx_hit` during a race therefore sounds as noise chopped by the engine square
  wave. `host_busy_wait_us` must run the tick handler at the crossed tick boundaries to reproduce
  this.
* **Busy-wait rule**: loops that poll input (e.g. the tune-up screen) already call `host_pump()`.

---

## 8. Open questions

1. **Effect speed reference**: DOSBox (4.47 µs/unit, short overheads) or a period machine (XT/AT:
   6.5 µs/unit, ~150–300 µs per `fx_tone` call)? The chirps/click differ ×10 in length. Recommend
   recording the effects from DOSBox (cycles=auto) and from real hardware if available, then fixing
   the three constants; the port keeps them as `#define`s.
2. Real names of the loop-sound situations (`7548`, `7613`, `7661`, `76da`) and of the effect call
   sites in `7aa4`/`81ab` — the garage / game_flow specs should confirm (paint, wrench, idle, …).
3. `DS:0286`/`DS:5A0E`/`DS:825E - DS:8B7C`: which animations of `0f38:8e48` (modes 1, 2, 4, 6) drive
   the alternative engine note — see video/game_flow.
4. `0000:1417` rotates three colours (`DS:0698`–`069A` via `0f38:1f4b(6..8, c)`) every 16 ticks while
   the music plays (`58D0 && 47CF`, EGA/Tandy drivers, `DS:4BC8`): jukebox lights; belongs to
   video/game_flow.
5. Mode-3 detail: the real PIT loads a new divisor only at the end of the current half period and
   restarts on a gate rising edge; the host model above ignores the former (inaudible at ≥ 18 Hz
   update rates, but noted for exactness).
