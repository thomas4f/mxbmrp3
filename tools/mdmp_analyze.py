#!/usr/bin/env python3
import struct, sys

EXC_NAMES = {
    0xC0000005: "EXCEPTION_ACCESS_VIOLATION",
    0xC0000094: "EXCEPTION_INT_DIVIDE_BY_ZERO",
    0xC000001D: "EXCEPTION_ILLEGAL_INSTRUCTION",
    0xC0000096: "EXCEPTION_PRIV_INSTRUCTION",
    0x80000003: "EXCEPTION_BREAKPOINT",
    0xC00000FD: "EXCEPTION_STACK_OVERFLOW",
    0xC0000409: "STATUS_STACK_BUFFER_OVERRUN",  # __fastfail / GS / std::terminate
    0xC0000374: "STATUS_HEAP_CORRUPTION",
    0xE06D7363: "MSVC C++ EXCEPTION (throw)",
}

def main(path):
    data = open(path, "rb").read()
    sig, ver, nstreams, dir_rva, chksum, tds, flags = struct.unpack_from("<IIIIIIQ", data, 0)
    assert sig == 0x504D444D, "not a minidump"

    streams = {}
    for i in range(nstreams):
        st, dsize, rva = struct.unpack_from("<III", data, dir_rva + i*12)
        streams.setdefault(st, []).append((dsize, rva))

    def mdstring(rva):
        ln = struct.unpack_from("<I", data, rva)[0]
        return data[rva+4:rva+4+ln].decode("utf-16-le", "replace")

    # --- Modules ---
    modules = []  # (base, size, name)
    if 4 in streams:
        _, rva = streams[4][0]
        n = struct.unpack_from("<I", data, rva)[0]
        off = rva + 4
        for i in range(n):
            base, size = struct.unpack_from("<QI", data, off)
            name_rva = struct.unpack_from("<I", data, off+20)[0]
            modules.append((base, size, mdstring(name_rva)))
            off += 108
    modules.sort()

    def mod_for(addr):
        for base, size, name in modules:
            if base <= addr < base + size:
                short = name.split("\\")[-1]
                return f"{short}+0x{addr-base:x}"
        return None

    # --- System info ---
    if 7 in streams:
        _, rva = streams[7][0]
        arch, lvl, build = struct.unpack_from("<HHI", data, rva)[0:3] if False else (struct.unpack_from("<H", data, rva)[0], 0, 0)

    print(f"\n==================== {path.split('/')[-1]} ====================")
    print(f"streams={nstreams}  modules={len(modules)}")

    # --- Exception ---
    fault_rip = None
    ctx_rva = None
    crashed_tid = None
    fault_rsp = None
    if 6 in streams:
        _, rva = streams[6][0]
        tid, _align = struct.unpack_from("<II", data, rva)
        crashed_tid = tid
        er = rva + 8
        (ecode, eflags, erec, eaddr, nparams, _pad) = struct.unpack_from("<IIQQII", data, er)
        params = struct.unpack_from("<15Q", data, er+32)
        ctx_size, ctx_rva = struct.unpack_from("<II", data, er+32+15*8)
        name = EXC_NAMES.get(ecode, "UNKNOWN")
        print(f"\n[EXCEPTION] code=0x{ecode:08X} {name}  flags=0x{eflags:X}")
        print(f"  crashed thread id = {tid}")
        print(f"  exception address = 0x{eaddr:016x}  -> {mod_for(eaddr)}")
        if ecode == 0xC0000005 and nparams >= 2:
            rw = {0:"READ",1:"WRITE",8:"EXECUTE"}.get(params[0], str(params[0]))
            print(f"  access violation: {rw} at 0x{params[1]:016x}")
        # context Rip
        if ctx_rva and ctx_size >= 0x100:
            rip = struct.unpack_from("<Q", data, ctx_rva+0xF8)[0]
            rsp = struct.unpack_from("<Q", data, ctx_rva+0x98)[0]
            rbp = struct.unpack_from("<Q", data, ctx_rva+0xA0)[0]
            fault_rip = rip
            fault_rsp = rsp
            print(f"  RIP=0x{rip:016x} -> {mod_for(rip)}")
            print(f"  RSP=0x{rsp:016x}  RBP=0x{rbp:016x}")

    # --- Build memory map (Memory64List preferred) ---
    mem_ranges = []  # (start, size, file_offset)
    if 9 in streams:
        _, rva = streams[9][0]
        nmem, base_rva = struct.unpack_from("<QQ", data, rva)
        off = rva + 16
        cur = base_rva
        for i in range(nmem):
            sa, sz = struct.unpack_from("<QQ", data, off)
            mem_ranges.append((sa, sz, cur))
            cur += sz
            off += 16
    elif 5 in streams:
        _, rva = streams[5][0]
        nmem = struct.unpack_from("<I", data, rva)[0]
        off = rva + 4
        for i in range(nmem):
            sa, dsize, drva = struct.unpack_from("<QII", data, off)
            mem_ranges.append((sa, dsize, drva))
            off += 16

    def read_mem(addr, length):
        for sa, sz, fo in mem_ranges:
            if sa <= addr < sa + sz:
                avail = min(length, sa+sz-addr)
                start = fo + (addr-sa)
                return data[start:start+avail]
        return None

    # --- Threads: find crashed thread stack ---
    thread_stack = None  # (stack_base_addr, bytes)
    if 3 in streams:
        _, rva = streams[3][0]
        n = struct.unpack_from("<I", data, rva)[0]
        off = rva + 4
        for i in range(n):
            (tid, susp, prio_c, prio, teb,
             stk_start, stk_size, stk_rva,
             ctx_size, ctx_rva2) = struct.unpack_from("<IIIIQQII II", data, off)
            off += 48
            if crashed_tid is not None and tid == crashed_tid:
                stk = read_mem(stk_start, stk_size) if mem_ranges else None
                if stk is None and stk_size:
                    stk = data[stk_rva:stk_rva+stk_size]
                thread_stack = (stk_start, stk)

    # --- Poor-man's stack walk: scan stack for return addrs in modules ---
    if thread_stack and thread_stack[1]:
        base, stk = thread_stack
        print(f"\n[STACK SCAN] crashed thread stack {len(stk)} bytes @ 0x{base:016x}")
        seen = []
        counts = {}
        for i in range(0, len(stk)-8, 8):
            val = struct.unpack_from("<Q", stk, i)[0]
            m = mod_for(val)
            if m:
                modname = m.split("+")[0]
                counts[modname] = counts.get(modname, 0) + 1
                seen.append((base+i, val, m))
        print("  module occurrence counts in stack memory (return-addr candidates):")
        for mn, c in sorted(counts.items(), key=lambda x: -x[1]):
            print(f"    {c:5d}  {mn}")
        # Explicitly surface any plugin frames
        plug = [(sp, val, m) for sp, val, m in seen
                if ("mrp3" in m.lower())]
        print(f"\n  PLUGIN (mxbmrp3) frames found in stack: {len(plug)}")
        for sp, val, m in plug:
            live = " LIVE" if (fault_rsp and sp >= fault_rsp) else " (dead/residue)"
            print(f"    sp=0x{sp:016x}  0x{val:016x}  {m}{live}")

        # Live call stack only (sp >= RSP): the real chain leading to the fault
        if fault_rsp:
            print(f"\n  LIVE CALL STACK (sp >= RSP=0x{fault_rsp:x}), in call order:")
            for sp, val, m in seen:
                if sp >= fault_rsp:
                    tag = "  <<< PLUGIN" if "mrp3" in m.lower() else ""
                    print(f"    sp=0x{sp:016x}  0x{val:016x}  {m}{tag}")

    # --- Always list plugin module(s) base/size ---
    print("\n[PLUGIN MODULES]")
    for base, size, name in modules:
        sn = name.split("\\")[-1].lower()
        if "mrp3" in sn:
            print(f"    {name}  base=0x{base:x} size=0x{size:x} (end=0x{base+size:x})")

if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
