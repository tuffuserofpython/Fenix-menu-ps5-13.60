# GTA V PS5 Mod Menu — `PPSA03420` v01.011.001

An in-game mod menu for the **native PS5 build** of Grand Theft Auto V, injected into the
running game over the network. Opens with **R1 + R2**.

Feature set and menu layout follow [SimpleMenu by MO3IZO](https://www.gta5-mods.com/scripts/simplemenu-gta-v-single-player-mod-menu-open-source),
but **no code is shared** — SimpleMenu is C# on ScriptHookVDotNet, none of which exists inside
a PS5 game process. This is a ground-up C++ rewrite.

> **Story mode only.** Story mode and GTA Online are the same process and the same binary.
> Do not load this and then go online.

---

## Status

Working and tested on a real console: the hook runs, the menu draws, and features fire.
It is **not** polished — see [Known issues](#known-issues). Firmware here was 13.60.

**Hard requirement:** GTA V **`PPSA03420` version `01.011.001`**. Every address in
`include/rvas.h` is specific to that build. A game update invalidates all of them, and the
injector refuses to patch if the bytes it expects aren't there.

## Requirements

| | |
|---|---|
| Console | Jailbroken PS5 with **kstuff** loaded |
| Payload | **ps5debug-NG** running (listens on TCP 744) — installable from pldmgr's repository |
| Game | GTA V `PPSA03420` `01.011.001`, running, in story mode |
| PC | Python 3.8+, on the same network |

To build from source you also need the [ps5-payload-dev SDK](https://github.com/ps5-payload-dev/sdk)
and clang/ld.lld with the `x86_64-sie-ps5` target (LLVM 16+; developed against 22.1.4).

## Install / inject

Nothing is installed on the console. The menu lives only in the game's memory, so it
disappears when GTA V closes — just re-run the injector.

```bash
# 1. launch GTA V and get into story mode
# 2. launch ps5debug-NG from pldmgr
# 3. from your PC:
cd inject
PS5_IP=192.168.0.109 python inject_now.py
```

Expected output:

```
blob: 45776 bytes | hook_run@0x0 g_origRun@0xaf60
pid=477 base=0x400000 run=0x345b8e0
prologo atual: 48 8D 87 54 01 00 00 48 89 05 02 3B D3 02
memoria: 0x6c000
blob escrito e verificado
trampolim 0x772d0, g_origRun gravado
hook aplicado

sinal de vida:
  t+1s g_hits=66
  t+2s g_hits=134

HOOK VIVO. R1 + R2 abre o menu.
```

`g_hits` climbing is the proof of life — that counter increments once per script frame from
inside the game. If it doesn't move, the injector restores the original bytes automatically
and reports failure, so a bad run leaves the game untouched.

Re-running the injector is safe: it detects an existing hook and restores the canonical
prologue before installing the new one, so hooks never stack.

## Controls

| Input | Action |
|---|---|
| **R1 + R2** | Open / close |
| D-pad Up / Down | Navigate |
| Cross (or D-pad Right) | Select / toggle / cycle value |
| Circle | Back to main page, or close |

22 pages, ~110 items: Player, Abilities, Vision, Vehicle, Car Tuning, Utility Features,
Vehicle Functions, Spawn Vehicle, Weapons, Custom Weapons, Wanted Level, Teleport, Landmarks,
Important Teleports (incl. **North Yankton**), World, NPC, Misc, HUD, Bodyguards, MP Maps, About.

Teleport freezes you for ~1 second on arrival. That is deliberate — see below.

---

## How it was made

No prior reverse engineering of this build existed, so everything below was derived from
scratch. The steps are documented because they generalise to other PS5 games.

### 1. Getting the decrypted executable

The game ships as a signed SELF; reading `eboot.bin` off disk gives encrypted bytes. But
etaHEN's FTP server **decrypts SELFs on read**, and while the game runs its sandbox is mounted
at `/mnt/sandbox/PPSA03420_000/app0/`. So the decrypted 70 MB ELF came straight over FTP —
no dumper payload needed.

### 2. The native table

GTA V registers its script natives through `rage::scrEngine::RegisterNativeHandler`. Scanning
`.text` for the registration pattern found every call site:

```asm
48 8d 35 <rel32>    lea    rsi, [rip+X]     ; handler
48 bf <imm64>       movabs rdi, <hash>      ; native hash
e8  <rel32>         call   RegisterNativeHandler
```

**6700 registrations**, all calling one function, giving a complete `hash → handler` map.

| Symbol | RVA |
|---|---|
| `rage::scrEngine::RegisterNativeHandler` | `0x0305DFE0` |
| `NativeRegistration* table[256]` (bucketed on `hash & 0xFF`) | `0x05D8EBC0` |
| `rage::scrThread::Run` | `0x0305B8E0` |
| bytecode interpreter | `0x0305BA00` |
| active-thread global | `0x05D8F3E0` |
| active thread in TLS | `fs:[0] - 0x140` |

### 3. Naming the natives

The PS5 build **rotates its native hashes** — only 260 of 6700 match PC. Those 260 became
anchors. Grouping the registration sites by proximity gave **48 blocks**, and every block
containing anchors mapped to exactly one namespace, which pinned 25 of them outright.

Registration order turned out to equal NativeDB order (HUD holds delta 0 across 18 anchors
spanning indices 129–274), so anchors pin the sequence and spans between them map 1:1.

To validate the rest, a signature extractor reads each handler's argument count and whether
it writes a return value. Calibrated against the 260 known-correct anchors it scores
**95.8% / 93.8%** — the first attempt scored 45% because the args register gets reused later
in a function, which naive matching kept counting.

Using that signature as the scoring function for sequence alignment, plus the rule
*"when a block's size equals its namespace's, the mapping is identity"*, produced
`data/ps5_natives_named_v3.json`: **6677 of 6700 named**, 84.6% at high confidence.

### 4. The `NativeContext` ABI

Read straight out of two tiny handlers. `GET_FRAME_COUNT` is four instructions:

```asm
mov eax, [frame_counter]
mov rcx, [rdi]          ; ctx->result  (+0x00)
mov [rcx], eax
retn
```

and `GET_HASH_KEY` does `mov rax,[rdi+0x10]` (`ctx->args`). So:
`result @ +0x00`, `argCount @ +0x08`, `args @ +0x10`, each arg an 8-byte slot.
Vector3 returns are `scrVector` — components **8 bytes apart**, not packed.

### 5. Control IDs

Taken from the build's own control-name table (402 entries, 24-byte stride) rather than a
web list — and it self-validates: `INPUT_NEXT_CAMERA = 0`, `INPUT_ATTACK = 24`,
`INPUT_AIM = 25`, `INPUT_VEH_HANDBRAKE = 76`, all canonical.
**R1 = 206** (`INPUT_FRONTEND_RB`), **R2 = 208** (`INPUT_FRONTEND_RT`).

### 6. Getting code into the process

Several dead ends worth recording:

- **`elfldr`** runs payloads as separate processes and cannot target an existing one.
- **A payload doing its own `ptrace`** fails: `PT_ATTACH` is refused even on its own pid, and
  `__sys_dynlib_get_list_for_libdbg` returns `EPERM`. Payloads launched via elfldr have no
  debug authority.
- **`sysctl KERN_PROC`** does not exist on PS5 — `ENOENT` for every pid, including your own.
  Process discovery must go through `sceKernelGetProcessName` + `sceKernelGetAppInfo`
  (title id at offset 16 of the app-info struct).
- **A codecave inside `.text`** is impossible: the 54 MB executable segment has **zero**
  contiguous free bytes.

What works is **ps5debug-NG**, which solves the privilege problem itself and exposes
read / write / alloc / protect over TCP. The injector drives it from the PC:

1. find the pid (`PROC_LIST`, `eboot.bin`)
2. find the module base (`PROC_MAPS`, the `executable` entry — `0x400000`)
3. `PROC_ALLOC` a region (already mapped RWX)
4. write the blob
5. build a trampoline and point `g_origRun` at it
6. overwrite `scrThread::Run`'s first 14 bytes with `jmp [rip+0]; <blob>`

The blob's hook calls the original `Run` first, then once per frame reinstalls the active
thread into TLS (`Run` restores it on exit) so natives execute in genuine script context.

The trampoline can't copy the prologue verbatim — the second stolen instruction is
`mov [rip+disp], rax`, which is RIP-relative. It's rebuilt with an absolute address through
`r11`, which is scratch in the SysV ABI and holds no argument at function entry.

### 7. The blob's constraints

Injected code can't rely on a loader, so `src/blob.cpp`:

- has **zero relocations** — no function-pointer or string-pointer tables; everything is a
  `switch`, which compiles to RIP-relative `lea`. `build.sh` fails the build if any
  relocation survives.
- has **zero imports** — no libc. Own `memset`, string builder, integer formatter.
- is position independent, so it runs at whatever address `PROC_ALLOC` returns.

### 8. Debugging from the PC

`inject/ncall.py` calls **any GTA V native from your PC** through ps5debug's `PROC_CALL`:
build a `NativeContext` in the target, call the handler with `rdi` pointing at it, read the
result back. `GET_FRAME_COUNT` returning a live incrementing counter validates the whole path.

This found both real bugs, neither of which was guessable:

- `GET_VEHICLE_PED_IS_IN(ped, 0)` returns a **stale non-zero handle** when on foot instead of
  0. Anything trusting it operates on a junk entity. `Veh()` now gates on
  `IS_PED_IN_ANY_VEHICLE` first. This alone broke teleport *and* every vehicle feature.
- A single long-distance `SET_ENTITY_COORDS` is **reverted by the game** because the
  destination isn't streamed in. Proof: a 25 m hop moved 29.5 m; Maze Bank Roof moved 0 m.
  Teleport now freezes the entity and re-sends coords for ~70 frames.

---

## Repository layout

```
src/blob.cpp                  the menu (freestanding, relocation-free)
include/rvas.h                120 native RVAs, each tagged with confidence
include/gtav.h                engine structs, RVAs, control IDs
blob.ld                       flat-image linker script
build.sh                      builds blob.bin, fails on any relocation
prebuilt/blob.bin             prebuilt image (45776 bytes)
prebuilt/blob_syms.txt        symbol offsets the injector patches
inject/dbg.py                 ps5debug-NG client (read/write/alloc/protect/maps)
inject/ncall.py               native REPL over PROC_CALL
inject/inject_now.py          the injector
cheats/PPSA03420_*.json       etaHEN cheat table (no injection needed)
data/ps5_natives_named_v3.json   all 6700 natives: hash, RVA, name, confidence
tools/diag4.cpp               standalone payload: finds the game's pid, on-screen notification
```

### `cheats/` — the no-injection option

`cheats/PPSA03420_01.011.001_gtav.json` goes in `/data/etaHEN/cheats/` and needs none of the
above — etaHEN applies it directly (**R3 + L3** in game). Two toggles, both verified against
live memory:

| Cheat | Patch |
|---|---|
| God Mode | NOPs 8 bytes at `0x01049A31`, killing `health = max(0, health − damage)` |
| Freeze Clip Ammo | NOPs 4 bytes at `0x02518F83`, the ammo-in-clip store |

Both affect **all entities**, not just you — that damage function is reached through vtable
entries, so enemies become unkillable too. Toggle accordingly.

### Not included, deliberately

The decrypted `eboot.bin` and the IDA database are **not** in this repository. They are
copyrighted game code. Dump your own — §1 above explains how, and it takes one FTP command.

## Known issues

- **14 of 120 natives are `medium` confidence** (tagged in `include/rvas.h`). Features using
  them may misbehave: Drunk Mode, Remove All Weapons, Kill All Enemies, NPC density sliders.
  `medium` means the order alignment says so but no anchor pins it directly — it can be off by
  a slot. Verify with `ncall.py` before trusting one.
- Not every one of the ~110 items has been individually tested on hardware.
- Four SimpleMenu items don't port: Menu Theme, Save Changes, Load Changes, and the About
  page's clickable links. About shows build info and a live native-call counter instead.
- Bodyguard removal is a stub.
- The menu dies with the game process. Re-run the injector after any restart.
- Firmware 13.60 is above what most PS5 homebrew tooling has been tested against.

## Credits

- **MO3IZO** — SimpleMenu, whose feature set and menu structure this follows
- **Pharaoh2k** — [ps5debug-NG](https://github.com/Pharaoh2k/ps5debug-NG), the injection primitive
- **John Törnblom / ps5-payload-dev** — [SDK](https://github.com/ps5-payload-dev/sdk) and `elfldr`
- **etaHEN** — app jailbreak, cheat engine, and the SELF-decrypting FTP server
- **alloc8or** — [gta5-nativedb-data](https://github.com/alloc8or/gta5-nativedb-data), the basis for naming

## Licence

GPL-3.0-or-later.

Not affiliated with or endorsed by Rockstar Games or Sony Interactive Entertainment.
"GTA", "PlayStation" and "PS5" are trademarks of their respective owners.
For use with your own legally owned copy, in single player.
