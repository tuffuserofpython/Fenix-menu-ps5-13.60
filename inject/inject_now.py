"""Injeta o blob do menu no GTA V via ps5debug-NG.

Endereco: real = base_do_modulo + RVA. (Nao existe correcao de -4; aquilo foi um
bug de parsing: os dados do PROC_READ comecam no offset 4, nao 8.)

Se ja houver um hook instalado (prologo comeca com FF 25), o prologo canonico e
restaurado antes de instalar o novo, para nao empilhar hooks.
"""
import struct, time, sys
import dbg as d

RVA_RUN = 0x0305B8E0
# prologo canonico deste build (lido da memoria viva; disp32 estavel pois base = 0x400000)
ORIG = bytes.fromhex("488D8754010000488905023BD302")
STOLEN = 14


def syms():
    out = {}
    for line in open("blob_syms.txt"):
        parts = line.split()
        if len(parts) == 2:
            out[parts[0]] = int(parts[1], 16)
    return out


def main():
    S = syms()
    blob = open("proj/blob.bin", "rb").read()
    print(f"blob: {len(blob)} bytes | hook_run@{S['hook_run']:#x} g_origRun@{S['g_origRun']:#x}")

    pid = d.gta_pid()
    if not pid:
        print("GTA V nao esta rodando"); return 1
    base, _ = d.exe_base(pid)
    if not base:
        print("nao achei a base do executavel"); return 1
    run = base + RVA_RUN
    print(f"pid={pid} base={base:#x} run={run:#x}")

    st, cur = d.read(pid, run, STOLEN)
    print("prologo atual:", cur.hex(' ').upper())
    if cur[:2] == b"\xFF\x25":
        print("ja existe hook instalado -> restaurando prologo canonico")
        if d.write(pid, run, ORIG) != d.SUCCESS:
            print("falha restaurando"); return 1
        st, cur = d.read(pid, run, STOLEN)
        print("prologo restaurado:", cur.hex(' ').upper())
    if cur[:7] != ORIG[:7]:
        print("prologo inesperado - abortando sem escrever"); return 1

    disp, = struct.unpack_from("<i", cur, 10)
    abs_dest = run + 14 + disp
    print(f"destino absoluto do mov [rip+disp]: {abs_dest:#x}")

    addr, st = d.alloc(pid, 0x14000)
    if not addr or addr < 0x10000:
        print("ALLOC falhou:", d.nm(st), addr); return 1
    print(f"memoria: {addr:#x}")

    if d.write(pid, addr, blob) != d.SUCCESS:
        print("falha escrevendo o blob"); return 1
    st, back = d.read(pid, addr, 256)
    if back[:256] != blob[:256]:
        print("verificacao do blob falhou"); return 1
    print("blob escrito e verificado")

    tramp = addr + ((len(blob) + 15) & ~15)
    code = bytearray()
    code += cur[:7]
    code += b"\x49\xBB" + struct.pack("<Q", abs_dest)
    code += b"\x49\x89\x03"
    code += b"\xFF\x25\x00\x00\x00\x00" + struct.pack("<Q", run + STOLEN)
    if d.write(pid, tramp, bytes(code)) != d.SUCCESS:
        print("falha escrevendo o trampolim"); return 1
    if d.write(pid, addr + S["g_origRun"], struct.pack("<Q", tramp)) != d.SUCCESS:
        print("falha gravando g_origRun"); return 1
    print(f"trampolim {tramp:#x}, g_origRun gravado")

    print("protect blob:", d.nm(d.protect(pid, addr, 0x14000, 7)))
    print("protect text:", d.nm(d.protect(pid, run & ~0x3FFF, 0x8000, 7)))

    patch = b"\xFF\x25\x00\x00\x00\x00" + struct.pack("<Q", addr + S["hook_run"])
    if d.write(pid, run, patch) != d.SUCCESS:
        print("falha aplicando o hook"); return 1
    print("hook aplicado")

    print("\nsinal de vida:")
    prev, alive = None, False
    for i in range(8):
        time.sleep(1.0)
        st, b = d.read(pid, addr + S["g_hits"], 8)
        if len(b) < 8:
            continue
        h, = struct.unpack("<Q", b)
        print(f"  t+{i+1}s g_hits={h}")
        if prev is not None and h > prev:
            alive = True; break
        prev = h

    if alive:
        open("injected.txt", "w").write(f"{pid} {base} {addr} {tramp}")
        print("\nHOOK VIVO. R1 + R2 abre o menu.")
        return 0

    print("\nsem sinal - revertendo")
    d.write(pid, run, ORIG)
    return 2


if __name__ == "__main__":
    sys.exit(main())
