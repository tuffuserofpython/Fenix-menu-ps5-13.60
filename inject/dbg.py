"""ps5debug-NG client (ps4debug wire protocol).

Set your console IP with the PS5_IP environment variable, e.g.
    PS5_IP=192.168.1.50 python inject_now.py

Nota de protocolo: o servidor manda as palavras de status com os bits pares/impares
trocados, entao na rede CMD_SUCCESS = 0x80000000 e CMD_ERROR = 0xF0000001.
Corpos de resposta e memoria NAO passam pela troca.
"""
import socket, struct, time

import os
HOST = os.environ.get("PS5_IP", "192.168.0.109")
PORT = 744
MAGIC = 0xFFAABBCC
SUCCESS, ERROR, DATA_NULL, ALREADY = 0x80000000, 0xF0000001, 0xF0000003, 0xF0000004

CMD_PROC_LIST, CMD_PROC_READ, CMD_PROC_WRITE = 0xBDAA0001, 0xBDAA0002, 0xBDAA0003
CMD_PROC_MAPS, CMD_PROC_CALL, CMD_PROC_PROTECT = 0xBDAA0004, 0xBDAA0006, 0xBDAA0008
CMD_PROC_ALLOC, CMD_PROC_FREE = 0xBDAA000B, 0xBDAA000C


def nm(x):
    return {SUCCESS: "SUCCESS", ERROR: "ERROR", DATA_NULL: "DATA_NULL",
            ALREADY: "ALREADY_DEBUG"}.get(x, hex(x))


def _s(t=8):
    s = socket.create_connection((HOST, PORT), timeout=t)
    s.settimeout(2.0)
    return s


def _rx(s, n, deadline=8.0):
    b = b""
    t0 = time.time()
    while len(b) < n and time.time() - t0 < deadline:
        try:
            c = s.recv(min(65536, n - len(b)))
        except socket.timeout:
            continue
        if not c:
            break
        b += c
    return b


def status(b):
    return struct.unpack_from("<I", b + b"\0" * 4, 0)[0]


def call(cmd, payload=b"", nresp=16):
    s = _s()
    s.sendall(struct.pack("<III", MAGIC, cmd, len(payload)))
    if payload:
        s.sendall(payload)
    b = _rx(s, nresp)
    s.close()
    return b


def proc_list():
    s = _s()
    s.sendall(struct.pack("<III", MAGIC, CMD_PROC_LIST, 0))
    b = _rx(s, 8)
    cnt, = struct.unpack_from("<I", b, 4)
    body = _rx(s, cnt * 36, deadline=10.0)
    s.close()
    out = []
    for i in range(cnt):
        e = body[i * 36:(i + 1) * 36]
        if len(e) < 36:
            break
        name = e[:32].split(b"\0")[0].decode(errors="replace")
        pid, = struct.unpack("<i", e[32:36])
        out.append((pid, name))
    return out


def gta_pid():
    for pid, name in proc_list():
        if name == "eboot.bin":
            return pid
    return None


def maps_all(pid):
    s = _s()
    s.sendall(struct.pack("<III", MAGIC, CMD_PROC_MAPS, 4))
    s.sendall(struct.pack("<i", pid))
    b = _rx(s, 8)
    cnt, = struct.unpack_from("<I", b, 4)
    body = _rx(s, cnt * 58, deadline=15.0)
    s.close()
    out = []
    for i in range(cnt):
        e = body[i * 58:(i + 1) * 58]
        if len(e) < 58:
            break
        name = e[:32].split(b"\0")[0].decode(errors="replace")
        start, end, off = struct.unpack_from("<QQQ", e, 32)
        prot, = struct.unpack_from("<H", e, 56)
        out.append((name, start, end, off, prot))
    return out


def exe_base(pid):
    """Base do modulo principal: a entrada 'executable' executavel, nunca a primeira as cegas."""
    m = maps_all(pid)
    for name, start, end, off, prot in m:
        if name == "executable" and (prot & 4):
            return start, m
    for name, start, end, off, prot in m:
        if (prot & 4) and (end - start) > 0x3000000:
            return start, m
    return None, m


def read(pid, addr, n):
    s = _s()
    s.sendall(struct.pack("<III", MAGIC, CMD_PROC_READ, 16))
    s.sendall(struct.pack("<IQI", pid, addr, n))
    b = _rx(s, 4 + n)
    s.close()
    return status(b), b[4:4 + n]


def write(pid, addr, data):
    """PROC_WRITE: corpo de 16 bytes, o servidor acusa SUCCESS, depois le os dados
    e manda um SEGUNDO status. Duas palavras de status no total."""
    s = _s()
    s.sendall(struct.pack("<III", MAGIC, CMD_PROC_WRITE, 16))
    s.sendall(struct.pack("<IQI", pid, addr, len(data)))
    ack = _rx(s, 4)
    if status(ack) != SUCCESS:
        s.close()
        return status(ack)
    s.sendall(data)
    fin = _rx(s, 4)
    s.close()
    return status(fin)


def alloc(pid, size):
    b = call(CMD_PROC_ALLOC, struct.pack("<II", pid, size), 12)
    st = status(b)
    addr = struct.unpack_from("<Q", b, 4)[0] if st == SUCCESS and len(b) >= 12 else None
    return addr, st


def protect(pid, addr, n, prot):
    return status(call(CMD_PROC_PROTECT, struct.pack("<IQII", pid, addr, n, prot), 8))
