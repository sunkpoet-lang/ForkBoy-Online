# Contract — Two-player protocol and game (routine map)

The two-player mode's code in the original, routine by routine, with where each lands in the port. Companion to
[`serial-multiplayer-state.md`](serial-multiplayer-state.md), which owns the *state* (`MultiplayerState`) and the
wire vocabulary; this contract owns the *code* that reads and writes it. Line anchors are `tetris.asm` in the
kaspermeerts/tetris disassembly at `b95c668`, the commit every other contract here is pinned to.

Status column: **ported** (already in the port), **to port** (this work), **mechanism** (replaced by the link port
rather than transcribed), **dead** (unreachable in the original; kept out or as a no-op).

---

## The cable: interrupt, handshake, transfer phases

| Routine | Lines | What it does | Port target | Status |
|---|---|---|---|---|
| `_Serial` | 45–66 | Serial interrupt: jump through a 5-entry table on `hSerialState`, then `hSerialInterruptTriggered = 1` | `LinkPort` "transfer completed" event → `serialDispatch(game)`, then `multiplayer.transferCompleted = 1` | mechanism + to port |
| `Handshake` | 71–98 | On the title screen (`$07`): received `SLAVE` → become `MASTER`, `rSC = $01`; received `MASTER` → become `SLAVE`, `rSC = 0`. Elsewhere: back to the title (`hGameState = $06`) unless already going there | `serialDispatch` index 0 | to port |
| `SerialState_01` | 100–103 | Latch `rSB` into `hSerialRx` | index 1 | to port |
| `SerialState_02` | 105–117 | Latch; the slave loads `hSerialTx` into `rSB`, sets `hSerialTx = $FF`, re-arms with the external clock | index 2 | to port |
| `SerialState_03` | 119–131 | Latch; the slave loads `hSerialTx`, waits `DelayMillisecond`, re-arms with the external clock | index 3 (the delay becomes "next resolve") | to port |
| 5th slot | 66 | `ret` (upstream "XXX Is this used?") | no-op | dead |
| `Unused_00D0` | 133–141 | Unreferenced | not ported | dead |
| `VBlank` send | 201–212 | Master only: if `$FFCE` (`sendPending`) is set, clear it, `rSB = hSerialTx`, start an internal-clock transfer | the frame's VBlank step: `if (sendPending && role == MASTER) link.start(tx, INTERNAL)` | to port |
| `DelayMillisecond` | 1554 | ~1 ms busy wait between transfers | dropped: the in-process cable delivers at the next resolve | mechanism |
| `WAIT_FOR_SERIAL_INTERRUPT` | `macros.asm` 1–6 | Spin on `hSerialInterruptTriggered` | a state that returns until `transferCompleted` is set (no spin) | mechanism |

Every `rSC` write in the original, for the link port's start/arm semantics: `Handshake` 97, `SerialState_02` 116,
`SerialState_03` 130, `VBlank` 211, `Init` 287, `GameState_07` 646/681 (title), `CheckForEndOfDemo` 742/749,
`Label_5C0` 875 and `Label_670` 984 (menu sends), `GameState_19` 1315–1536 (bulk transfer, 11 sites), `GameState_08`
3119. `hSerialState` is set to `$03` by `GameState_2A`/`16`/`19`/`1B`, `$01` by `GameState_18`, `$02` by
`GameState_1C`.

## The two-player screens and round

| Routine | Lines | What it does | Port target | Status |
|---|---|---|---|---|
| `GameState_2A` | 880–905 | Init music selection (two-player) | dispatcher handler `INIT_2P_MUSIC_SELECTION` | to port |
| `GameState_2B` | 906–988 | Music select: the master sends its choice, the slave acks `$39`, `$50` confirms; `SwitchMusic` | `SELECT_2P_MUSIC` | to port |
| `GameState_16` | 989–1072 | Init difficulty selection | `INIT_2P_DIFFICULTY` | to port |
| `GameState_17` | 1073–1216 | Start height for both players, `$60` = start | `SELECT_2P_HEIGHT` | to port |
| `GameState_18` | 1217–1298 | Init two-player game | `INIT_2P_GAME` | to port |
| `GameState_19` | 1299–1553 | Bulk transfer: VBlank interrupt off; the master sends 10 rows × 10 cells of garbage (top to bottom) and the 256-entry piece list (wrapping `b`), each byte after `DelayMillisecond` + `WAIT_FOR_SERIAL_INTERRUPT`; `$30`/`$56` done handshake; both shift the garbage down by `10 − 2·startHeight` rows | `INIT_2P_GAME_2`; see "Bulk transfer pacing" | to port |
| `GameState_1C` | 1609–1669 | Last step before the round: prepares the height gauge (18 sprites, one per row) and the garbage-attack sprites while the wipe runs, then sets `hSerialState = $02`, renders the pieces, starts the music and goes to `$1A` | `PREPARE_GARBAGE` | to port |
| `GameState_1A` | 1670–1726 | The two-player round frame | `TWO_PLAYER_GAME` (reuses `normalGameplay`'s shared pieces) | to port |
| `Call_B9B` | 1727–1766 | Stack height into `$FFB1` (rows from the top, 0–18); danger music at ≥ 12 | `stackHeightStatus(game)` | to port |
| `pauseMultiplayerGame` | 1768–1781 | Slave-side pause entry; parks `tx`/`rx` | `gameplay.cpp` | ported |
| `Call_BF0` | 1783–1889 | Compose and exchange the in-round status: attack `$80\|rows`, `$77` won by lines, `$AA` topped out, `$94` pause | `exchangeRoundStatus(game)` | to port |
| `Call_C8C` | 1890–1961 | Apply the received status: garbage rows (`& $7F`, < 5) into the pending pipeline, round end | `applyRoundStatus(game)` | to port |
| `GameState_1B` | 1962–2005 | End-of-round jingle: waits on `hTimer1`, settles `roundOutcome` from `$D1` and `hSerialRx`, goes to `$1D` (victory) or `$1E` (defeat) | `TWO_PLAYER_END_JINGLE` | to port |
| `GameState_1D` / `20` | 2007–2067 / 2068–2162 | Init / run the victory screen | `INIT_2P_VICTORY`, `TWO_PLAYER_VICTORY` | to port |
| `GameState_1E` / `21` | 2163–2223 / 2224–… | Init / run the defeat screen | `INIT_2P_DEFEAT`, `TWO_PLAYER_DEFEAT` | to port |
| `Call_1085` | 2530–2590 | Win tally; a tally of 4 is promoted to 5 (first to 4, shown as 5); advantage/deuce writers unreachable | `promoteWinTally(game)` | to port (live part) / dead |
| `GameState_1F` | 2635–… | Init two-player game (3×), the rematch path | `INIT_2P_GAME_3` | to port |
| Pause sync | 4490–4545 | `$94` toggles pause on both sides; `HandlePausedMultiplayer` | `gameplay.cpp` | ported |

## The audio read-back is sound-only

The two-player code reads the sound driver back in one place: `Call_B9B` (line 1751) reads `wCurrentMusicID` to
decide whether to switch to the danger music (song `$08`) when the stack reaches row 12, or back through
`SwitchMusic` when it falls. `SwitchMusic` only writes `wNewMusicID` (and `$FF` = silence for the "off" music type).
The read never reaches game state: no branch the round takes depends on it. `GameState_1B` waits on the frame
timer `hTimer1`, not on the music. (The only other read-back in the whole game is the Type B ending, `GameState_23`
line 4818, single-player.)

**Rule for the port:** the danger-music decision must not come from the audio thread's read-back when two Game
Boys must stay identical (online). The port keeps the music the game last *requested* in game state and decides
from that; the result is the same song choices the cartridge makes, and the audio's own pace never reaches the
synchronised state.

## Bulk transfer pacing

`GameState_19` disables the VBlank interrupt, so the screen does not change while about 360 bytes cross
(100 garbage cells, 256 pieces, the handshakes), each after a ~1 ms delay: roughly 0.4–0.5 s of a frozen frame on
hardware. The port runs the exchange byte by byte through the link port in one scheduler step (both sides are
frozen anyway) and then holds the frame for the measured duration in frames, so what the players see matches the
cartridge without cycle-exact serial timing. The exact hold is measured in the two-player tests.

## Kept quirks

- The `$FFAF` piece-list pointer high byte is written and never read (line 1452 region).
- `transferCompleted` takes `$1B`/`$1F` at two sites (flagged as bugs upstream); the struct keeps `uint8_t`.
- The advantage/deuce trio stays dead (see the state contract).

## Tested by (planned)

Per-routine tests pinned to these anchors, as the other contracts, plus a two-instance determinism test (the same
recorded controls of both players, twice, the same checksum every frame) and a full-match test to the victory
screen.
