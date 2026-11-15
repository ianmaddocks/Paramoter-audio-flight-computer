#!/usr/bin/env python3
"""Decodes the crash record the vario keeps in flash (GET /api/coredump) against the firmware.elf of the build that crashed.

    python tools/crash_decode.py core.bin .pio/build/test/firmware.elf

Prints, for the crashed task first and then every other task: its name, where it was (pc, ra) and every code address found on
its stack, each turned into function + file:line with the toolchain's addr2line. The ELF must be of the same build that crashed
(same commit, same environment), otherwise the names are wrong.
"""
import glob
import os
import struct
import subprocess
import sys

REGS = ["pc", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"]
CAUSES = {0: "instruction address misaligned", 1: "instruction access fault", 2: "illegal instruction", 3: "breakpoint",
          4: "load address misaligned", 5: "load access fault", 6: "store address misaligned", 7: "store access fault"}


def addr2line():
    home = os.path.expanduser("~/.platformio/packages")
    for pat in ("toolchain-riscv32-esp*/bin/riscv32-esp-elf-addr2line*",):
        hits = glob.glob(os.path.join(home, pat))
        if hits:
            return hits[0]
    sys.exit("riscv32-esp-elf-addr2line not found under ~/.platformio/packages")


def symbolise(tool, elf, addrs):
    if not addrs:
        return {}
    out = subprocess.run([tool, "-pfiaC", "-e", elf] + ["0x%08x" % a for a in addrs], capture_output=True, text=True).stdout
    res, cur = {}, None
    for line in out.splitlines():
        if line.startswith("0x"):
            cur = int(line.split(":")[0], 16)
            res[cur] = line.split(": ", 1)[1]
        elif cur is not None:
            res[cur] += "\n" + " " * 27 + line.strip()
    return res


def main():
    core_path, elf = sys.argv[1], sys.argv[2]
    raw = open(core_path, "rb").read()
    e = raw.find(b"\x7fELF")
    if e < 0:
        sys.exit("no ELF image in the crash record")
    d = raw[e:]
    phoff, = struct.unpack_from("<I", d, 28)
    phentsize, phnum = struct.unpack_from("<HH", d, 42)
    notes, segs = [], []
    for i in range(phnum):
        ptype, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from("<8I", d, phoff + i * phentsize)
        if ptype == 4:
            p = off
            while p < off + filesz:
                namesz, descsz, ntype = struct.unpack_from("<3I", d, p)
                name = d[p + 12:p + 12 + namesz].rstrip(b"\0").decode()
                dp = p + 12 + (namesz + 3 & ~3)
                notes.append((name, ntype, d[dp:dp + descsz]))
                p = dp + (descsz + 3 & ~3)
        elif ptype == 1:
            segs.append((vaddr, d[off:off + filesz]))

    def mem(addr, n):
        for va, data in segs:
            if va <= addr and addr + n <= va + len(data):
                return data[addr - va:addr - va + n]
        return None

    def is_code(a):
        return 0x42000000 <= a < 0x42800000 or 0x40380000 <= a < 0x403E0000 or 0x40000000 <= a < 0x40060000

    tasks, extra, panic = [], None, None
    for name, ntype, desc in notes:
        if ntype == 1 and len(desc) >= 72 + 128:  # PRSTATUS: pid at 24, registers at 72
            pid, = struct.unpack_from("<I", desc, 24)
            tasks.append((pid, dict(zip(REGS, struct.unpack_from("<32I", desc, 72)))))
        elif name == "EXTRA_INFO":
            extra = desc
        elif name == "ESP_PANIC_DETAILS":
            panic = desc.rstrip(b"\0").decode("ascii", "replace")
        elif name == "ESP_CORE_DUMP_INFO":
            print("build sha256 (start):", desc[4:].rstrip(b"\0").decode("ascii", "replace"))
    if panic:
        print("panic message:", panic)
    crashed = None
    if extra:
        crashed, = struct.unpack_from("<I", extra, 0)
        print("crashed task TCB 0x%08x" % crashed)
        # The crashed task's stack record begins with the exception frame (160 bytes under its sp): 32 registers, then mstatus,
        # mtvec, mcause, mtval.
        sp = dict(tasks).get(crashed, {}).get("sp", 0)
        fr = mem(sp - 160, 160) if sp else None
        if fr:
            w = struct.unpack_from("<36I", fr)
            print("exception frame: mepc 0x%08x  ra 0x%08x  mcause %d (%s)  mtval 0x%08x" % (w[0], w[1], w[34], CAUSES.get(w[34], "?"), w[35]))
    tool = addr2line()
    for pid, r in sorted(tasks, key=lambda t: t[0] != crashed):
        tcb = mem(pid, 0x60)
        tname = "?"
        if tcb:  # pcTaskName sits after the list items; find a printable run
            for o in range(0x30, 0x58, 4):
                s = tcb[o:o + 16].split(b"\0")[0]
                if len(s) >= 3 and all(32 <= c < 127 for c in s):
                    tname = s.decode()
                    break
        addrs = [r["pc"], r["ra"]]
        for va, data in segs:
            if va <= r["sp"] < va + len(data):
                st = data[r["sp"] - va:]
                addrs += [w for w in struct.unpack_from("<%dI" % (len(st) // 4), st) if is_code(w)]
        seen, uniq = set(), []
        for a in addrs:
            if a not in seen:
                seen.add(a)
                uniq.append(a)
        sym = symbolise(tool, elf, uniq)
        print("\n=== task %s (TCB 0x%08x)%s  sp 0x%08x  a0 0x%08x a1 0x%08x a2 0x%08x" % (
            tname, pid, "  <-- CRASHED" if pid == crashed else "", r["sp"], r["a0"], r["a1"], r["a2"]))
        for k, a in enumerate(uniq):
            print("  %-5s 0x%08x  %s" % ("pc" if k == 0 else "ra" if k == 1 else "stack", a, sym.get(a, "")))


if __name__ == "__main__":
    main()
