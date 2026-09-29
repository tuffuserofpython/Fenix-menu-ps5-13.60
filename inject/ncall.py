"""REPL de natives: chama um handler do GTA V pelo PROC_CALL do ps5debug.

O handler recebe rdi = NativeCtx*. Montamos o ctx na memoria do alvo, chamamos,
e lemos o slot de retorno de volta.
"""
import struct
import dbg as d

CTX = None   # (pid, base_ctx)

def setup(pid):
    global CTX
    a, st = d.alloc(pid, 0x1000)
    if not a:
        raise RuntimeError("alloc falhou: " + d.nm(st))
    CTX = (pid, a)
    return a

def call(rva, args=(), floats=()):
    """args: inteiros/ponteiros. floats: dict idx->float, aplicado depois."""
    pid, a = CTX
    ctx_at = a
    ret_at = a + 0x100
    arg_at = a + 0x200
    body = bytearray(0x40)
    struct.pack_into("<Q", body, 0x00, ret_at)      # pReturn
    struct.pack_into("<I", body, 0x08, len(args))   # nArgCount
    struct.pack_into("<Q", body, 0x10, arg_at)      # pArgs
    d.write(pid, ctx_at, bytes(body))
    d.write(pid, ret_at, b"\0" * 0x40)
    ab = bytearray(8 * max(len(args), 1))
    for i, v in enumerate(args):
        struct.pack_into("<Q", ab, i * 8, v & 0xFFFFFFFFFFFFFFFF)
    for i, f in (floats or {}).items():
        struct.pack_into("<f", ab, i * 8, f)
        struct.pack_into("<I", ab, i * 8 + 4, 0)
    d.write(pid, arg_at, bytes(ab))

    base, _ = d.exe_base(pid)
    rip = base + rva
    pkt = struct.pack("<IQQQQQQQQ", pid, 0, rip, ctx_at, 0, 0, 0, 0, 0)
    r = d.call(d.CMD_PROC_CALL, pkt, 16)
    stt = d.status(r)
    rax = struct.unpack_from("<Q", r, 8)[0] if len(r) >= 16 else None
    st2, rb = d.read(pid, ret_at, 16)
    r0, = struct.unpack_from("<Q", rb, 0)
    return stt, rax, r0
