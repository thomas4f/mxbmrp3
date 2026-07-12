#!/usr/bin/env python3
# Standalone minidump triage (no Windows debugger needed).
#
# Prints the exception code/faulting address, the full register file, the
# faulting instruction bytes (disassembled if `capstone` is installed), a
# heuristic live-stack scan to attribute the crash to the plugin vs the game
# vs a GPU/system DLL, and an automatic check for the most common pointer
# bugs (64-bit->32-bit truncation, near-null deref).
#
# It is intentionally dependency-free: `capstone` is used only if present.
# Symbol-less, so it gives module+offset, not function names -- pair the dump
# with the matching .pdb (and ideally a full dump, `procdump -ma -e <exe>`)
# when you need the actual function / locals.
#
# It also reads the host EXE build fingerprint (PE TimeDateStamp) from the dump's
# module record -- independent of any log -- and, if a
# known-crash registry (known_game_crashes.json) is found, matches the faulting
# instruction bytes against it (build-independent) and prints a [KNOWN CRASH]
# section. The registry is auto-discovered next to the dump, next to this script,
# or in CWD; override with --known <file.json> or $MDMP_KNOWN.
#
# Usage:
#   python3 tools/mdmp_analyze.py <file.dmp> [<file2.dmp> ...]
#   python3 tools/mdmp_analyze.py --compare <a.dmp> <b.dmp>
#   python3 tools/mdmp_analyze.py --known <registry.json> <file.dmp> ...
import struct, sys, hashlib, datetime, json, os
from collections import Counter

# Optional path to a known-crash registry (JSON). Set by --known; otherwise
# print_report() searches the dump's directory, this script's dir, and CWD.
KNOWN_PATH = None

try:
    import capstone
    _CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
except Exception:
    _CS = None

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

ARCH_NAMES = {0: "x86", 6: "IA64", 9: "AMD64", 12: "ARM64"}

# CONTEXT_AMD64 integer register offsets
GP_REGS = [
    ("Rax", 0x78), ("Rcx", 0x80), ("Rdx", 0x88), ("Rbx", 0x90),
    ("Rsp", 0x98), ("Rbp", 0xA0), ("Rsi", 0xA8), ("Rdi", 0xB0),
    ("R8", 0xB8), ("R9", 0xC0), ("R10", 0xC8), ("R11", 0xD0),
    ("R12", 0xD8), ("R13", 0xE0), ("R14", 0xE8), ("R15", 0xF0),
]

# Third-party in-process code worth surfacing (overlays / injectors / capture).
INJECTOR_HINTS = {
    "gameoverlayrenderer": "Steam overlay",
    "nvspcap": "NVIDIA GeForce Experience / ShadowPlay overlay",
    "rtsshooks": "RivaTuner Statistics Server overlay",
    "rtss": "RivaTuner Statistics Server overlay",
    "msiafterburner": "MSI Afterburner",
    "discord": "Discord overlay",
    "overlay": "generic overlay",
    "fraps": "Fraps capture",
    "obs": "OBS capture hook",
    "graphics-hook": "OBS game-capture hook (graphics-hook64.dll -- known to crash OpenGL games via DXGI; see OBS #9168)",
    "nahimic": "Nahimic audio (known to inject/crash games)",
    "sonar": "SteelSeries Sonar audio",
    "easyanticheat": "EasyAntiCheat",
    "battleye": "BattlEye",
    "reshade": "ReShade",
    "specialk": "Special K",
    "rivatuner": "RivaTuner",
}

GPU_HINTS = {
    "nvoglv": "NVIDIA (OpenGL driver)", "nvwgf2umx": "NVIDIA (D3D driver)",
    "nvldumdx": "NVIDIA", "nvapi64": "NVIDIA",
    "atiumd": "AMD", "amdvlk": "AMD", "aticfx": "AMD", "amdxc64": "AMD",
    "igdumd": "Intel", "igd10": "Intel", "igdml": "Intel",
}


def fmt_time(ts):
    """Format a minidump time_t as UTC, or '?' if absent."""
    if not ts:
        return "?"
    return datetime.datetime.fromtimestamp(ts, datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


def parse_dump(path):
    with open(path, "rb") as fh:
        data = fh.read()
    sig, ver, nstreams, dir_rva, chksum, tds, flags = struct.unpack_from("<IIIIIIQ", data, 0)
    if sig != 0x504D444D:
        raise ValueError(f"{path}: not a minidump (bad signature 0x{sig:08X})")

    d = {"path": path, "data": data, "header_time": tds,
         "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}

    streams = {}
    for i in range(nstreams):
        st, dsize, rva = struct.unpack_from("<III", data, dir_rva + i * 12)
        streams.setdefault(st, []).append((dsize, rva))
    d["nstreams"] = nstreams

    def mdstring(rva):
        ln = struct.unpack_from("<I", data, rva)[0]
        return data[rva + 4:rva + 4 + ln].decode("utf-16-le", "replace")

    # --- Modules ---
    modules = []  # (base, size, name)
    module_meta = {}  # name -> {timedatestamp, fileversion}
    if 4 in streams:
        _, rva = streams[4][0]
        n = struct.unpack_from("<I", data, rva)[0]
        off = rva + 4
        for i in range(n):
            base, size = struct.unpack_from("<QI", data, off)
            tds = struct.unpack_from("<I", data, off + 16)[0]  # MINIDUMP_MODULE.TimeDateStamp
            name_rva = struct.unpack_from("<I", data, off + 20)[0]
            # VS_FIXEDFILEINFO starts at off+24; valid only if dwSignature==0xFEEF04BD
            vsig, _vstruc, fvMS, fvLS = struct.unpack_from("<IIII", data, off + 24)
            name = mdstring(name_rva)
            modules.append((base, size, name))
            fileversion = (f"{fvMS >> 16}.{fvMS & 0xffff}.{fvLS >> 16}.{fvLS & 0xffff}"
                           if vsig == 0xFEEF04BD else None)
            module_meta[name] = {"timedatestamp": tds, "fileversion": fileversion}
            off += 108
    modules.sort()
    d["modules"] = modules
    d["module_meta"] = module_meta

    def mod_for(addr):
        for base, size, name in modules:
            if base <= addr < base + size:
                return f"{name.split(chr(92))[-1]}+0x{addr - base:x}"
        return None
    d["mod_for"] = mod_for

    # "System/runtime" = anything under \Windows\ (incl. CRT in WinSxS and the
    # GPU driver in DriverStore). Used to see past leaf frames in ntdll / the C
    # runtime / drivers to the nearest non-system caller -- the likely culprit.
    def is_system(addr):
        for base, size, name in modules:
            if base <= addr < base + size:
                return "\\windows\\" in name.lower()
        return False
    d["is_system"] = is_system

    # --- Misc info (PID / process times) ---
    d["pid"] = None
    d["proc_create"] = None
    if 15 in streams:
        _, rva = streams[15][0]
        size_info, flags1, pid, create_t = struct.unpack_from("<IIII", data, rva)
        if flags1 & 0x1:
            d["pid"] = pid
        if flags1 & 0x2 and create_t:
            d["proc_create"] = create_t

    # --- System info ---
    d["sysinfo"] = None
    if 7 in streams:
        _, rva = streams[7][0]
        arch, plvl, prev, ncpu, ptype = struct.unpack_from("<HHHBB", data, rva)
        major, minor, build = struct.unpack_from("<III", data, rva + 8)
        d["sysinfo"] = {"arch": arch, "ncpu": ncpu,
                        "os": f"{major}.{minor}.{build}"}

    # --- Memory map ---
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
                avail = min(length, sa + sz - addr)
                start = fo + (addr - sa)
                return data[start:start + avail]
        return None
    d["read_mem"] = read_mem

    # --- Exception + full register context ---
    d.update(exc_code=None, fault_rip=None, fault_rsp=None, crashed_tid=None,
             av_rw=None, fault_addr=None, regs={})
    if 6 in streams:
        _, rva = streams[6][0]
        tid, _align = struct.unpack_from("<II", data, rva)
        d["crashed_tid"] = tid
        er = rva + 8
        (ecode, eflags, erec, eaddr, nparams, _pad) = struct.unpack_from("<IIQQII", data, er)
        params = struct.unpack_from("<15Q", data, er + 32)
        ctx_size, ctx_rva = struct.unpack_from("<II", data, er + 32 + 15 * 8)
        d["exc_code"] = ecode
        d["exc_addr"] = eaddr
        if ecode == 0xC0000005 and nparams >= 2:
            d["av_rw"] = {0: "READ", 1: "WRITE", 8: "EXECUTE"}.get(params[0], str(params[0]))
            d["fault_addr"] = params[1]
        if ctx_rva and ctx_size >= 0x100:
            for nm, o in GP_REGS:
                d["regs"][nm] = struct.unpack_from("<Q", data, ctx_rva + o)[0]
            d["fault_rip"] = struct.unpack_from("<Q", data, ctx_rva + 0xF8)[0]
            d["fault_rsp"] = d["regs"]["Rsp"]

    # --- Crashed thread stack ---
    d["thread_stack"] = None
    if 3 in streams and d["crashed_tid"] is not None:
        _, rva = streams[3][0]
        n = struct.unpack_from("<I", data, rva)[0]
        off = rva + 4
        for i in range(n):
            (tid, susp, prio_c, prio, teb,
             stk_start, stk_size, stk_rva,
             ctx_size, ctx_rva2) = struct.unpack_from("<IIIIQQII II", data, off)
            off += 48
            if tid == d["crashed_tid"]:
                stk = read_mem(stk_start, stk_size) if mem_ranges else None
                if stk is None and stk_size:
                    stk = data[stk_rva:stk_rva + stk_size]
                d["thread_stack"] = (stk_start, stk)

    # --- Faulting instruction bytes ---
    d["code"] = None
    if d["fault_rip"]:
        d["code"] = read_mem(d["fault_rip"], 16)

    return d


# MSVC / Windows debug-fill and heap-guard sentinel values. Seeing these in a
# faulting address points at a *specific* bug class -- and notably NOT pointer
# truncation -- so they help keep triage honest across crash types.
SENTINELS = {
    0xCCCCCCCC: "uninitialized stack memory (MSVC /RTC fill) -> uninitialized local pointer",
    0xCDCDCDCD: "uninitialized heap memory (operator new fill) -> uninitialized member pointer",
    0xFEEEFEEE: "freed heap memory (HeapFree fill) -> use-after-free",
    0xDDDDDDDD: "freed memory (MSVC delete fill) -> use-after-free",
    0xBAADF00D: "uninitialized LocalAlloc memory",
    0xABABABAB: "heap guard bytes after a block -> buffer overrun",
    0xFDFDFDFD: "heap no-mans-land guard bytes -> buffer overrun/underrun",
}


def _sentinel(v):
    if v is None:
        return None
    for s, desc in SENTINELS.items():
        if v == s or (v & 0xFFFFFFFF) == s:
            return desc
    return None


def classify(d):
    """Breadth-first crash classification. Recognizes the common Windows crash
    families (AV read/write/execute, sentinel/use-after-free, pointer
    truncation, near-null, stack overflow, /GS|__fastfail, heap corruption,
    C++ throw, illegal instruction). Returns {class, hints, truncation}.
    Heuristic and deliberately NOT exhaustive -- it never claims a verdict it
    can't back from the dump, and the report always tells the reader to
    corroborate with the live stack + paired .log."""
    ec = d.get("exc_code")
    out = {"class": None, "hints": [], "truncation": False}
    if ec is None:
        return out
    regs = d["regs"]
    fa = d.get("fault_addr")

    if ec == 0xC0000005:  # access violation
        rw = d["av_rw"]
        if rw == "EXECUTE":
            out["class"] = "execute-access-violation (tried to run non-code)"
            out["hints"].append(
                f"attempted to EXECUTE 0x{fa:016x} -- corrupted return address, "
                f"trashed function pointer, or call through a bad vtable (DEP)")
            s = _sentinel(fa)
            if s:
                out["hints"].append(f"target is a sentinel value: {s}")
        else:
            out["class"] = f"{(rw or 'read').lower()}-access-violation"
            s = _sentinel(fa)
            if s:
                out["hints"].append(f"fault address 0x{fa:x} is a sentinel: {s}")
            for nm, v in regs.items():
                if v == fa:
                    out["hints"].append(f"fault address is exactly {nm} (the dereferenced pointer)")
            for nm, v in regs.items():
                if v > 0xFFFFFFFF and (v & 0xFFFFFFFF) == fa:
                    valid = d["read_mem"](v, 8)
                    extra = (f"; real address 0x{v:016x} is mapped (holds {valid.hex()}) "
                             f"so memory was fine -- the pointer was truncated") if valid else ""
                    out["hints"].append(
                        f"** 64-bit->32-bit POINTER TRUNCATION: fault addr 0x{fa:08x} "
                        f"== low 32 bits of {nm}=0x{v:016x}{extra}")
                    out["truncation"] = True
            if fa is not None and fa < 0x10000:
                out["hints"].append(
                    f"near-null dereference (0x{fa:x}) -- null pointer + struct field offset")
    elif ec == 0xC00000FD:
        out["class"] = "stack overflow (unbounded recursion or oversized stack allocation)"
    elif ec == 0xC0000409:
        out["class"] = "stack buffer overrun / __fastfail"
        out["hints"].append("/GS stack-cookie corruption or an explicit __fastfail; "
                             "the fail-fast code is usually in RCX")
        if "Rcx" in regs:
            out["hints"].append(f"RCX (likely fast-fail code) = 0x{regs['Rcx']:x}")
    elif ec == 0xC0000374:
        out["class"] = "heap corruption"
        out["hints"].append("heap metadata was already corrupted (earlier overrun/double-free); "
                            "the crashing frame is the detector, not the culprit -- look upstream")
    elif ec == 0xE06D7363:
        out["class"] = "unhandled C++ exception (throw with no matching catch)"
    elif ec in (0xC000001D, 0xC0000096):
        out["class"] = "illegal/privileged instruction (corrupted code page or bad jump target)"
    else:
        out["class"] = EXC_NAMES.get(ec, f"0x{ec:08X} (uncommon)")
    return out


def _registry_candidates(dump_path):
    """Search order for the known-crash registry: --known (KNOWN_PATH), $MDMP_KNOWN,
    the dump's own directory, this script's dir, the sibling crash_analysis/, then CWD."""
    candidates = []
    if KNOWN_PATH:
        candidates.append(KNOWN_PATH)
    if os.environ.get("MDMP_KNOWN"):
        candidates.append(os.environ["MDMP_KNOWN"])
    dump_dir = os.path.dirname(os.path.abspath(dump_path)) if dump_path else None
    if dump_dir:
        candidates.append(os.path.join(dump_dir, "known_game_crashes.json"))
    here = os.path.dirname(os.path.abspath(__file__))
    candidates += [
        os.path.join(here, "known_game_crashes.json"),
        os.path.join(here, "..", "crash_analysis", "known_game_crashes.json"),
        os.path.join("crash_analysis", "known_game_crashes.json"),
        "known_game_crashes.json",
    ]
    # Fallback to the pre-rename filename so an old local copy still resolves.
    if dump_dir:
        candidates.append(os.path.join(dump_dir, "known_crashes.json"))
    candidates += [
        os.path.join(here, "..", "crash_analysis", "known_crashes.json"),
        os.path.join("crash_analysis", "known_crashes.json"),
        "known_crashes.json",
    ]
    return candidates


def load_known_registry(dump_path):
    """Find and parse the known-crash registry. Returns the parsed dict or None.
    Never raises -- a missing/bad registry just disables matching."""
    for path in _registry_candidates(dump_path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            continue
    return None


def find_registry_path(dump_path):
    """On-disk path of the known-crash registry (first existing candidate), or None.
    Used by --record to write the file back to the same place it was read from."""
    for path in _registry_candidates(dump_path):
        if os.path.exists(path):
            return path
    return None


def host_exe(d):
    """(full name, meta) of the host .exe module, or (None, None). 'modules' is
    sorted by base address, so this is the first/lowest-base .exe."""
    for _, _, name in d["modules"]:
        if name.lower().endswith(".exe"):
            return name, d.get("module_meta", {}).get(name)
    return None, None


def host_exe_tds(d):
    """Host exe PE TimeDateStamp as '0x%08X', or None if unavailable."""
    _, m = host_exe(d)
    return f"0x{m['timedatestamp']:08X}" if m else None


def _is_hex_build(k):
    try:
        int(k, 16)
        return True
    except (ValueError, TypeError):
        return False


def match_known(d, registry):
    """Match this dump against the registry by faulting instruction bytes (build-
    independent) + AV kind. Returns (entry, build_status_string) or (None, None)."""
    if not registry or not d.get("code"):
        return None, None
    fault_hex = d["code"].hex()
    av = d.get("av_rw")
    tds_key = host_exe_tds(d)  # e.g. "0x6A21833D"
    faultmod = (d["mod_for"](d["fault_rip"]) or "") if d.get("fault_rip") else ""
    for entry in registry.get("crashes", []):
        mb = entry.get("match", {})
        want = (mb.get("bytes") or "").lower()
        # '?' is a wildcard nibble -- lets a pattern skip call/jump rel32 bytes
        # that change between game builds while the surrounding opcodes don't.
        if not want or len(want) > len(fault_hex) or not all(
                p == "?" or p == c for p, c in zip(want, fault_hex)):
            continue
        if mb.get("av") and av and mb["av"].upper() != av.upper():
            continue
        if mb.get("module") and not faultmod.lower().startswith(mb["module"].lower()):
            continue
        builds = entry.get("builds", {})
        hex_builds = {k.upper(): v for k, v in builds.items() if _is_hex_build(k)}
        if not hex_builds:
            # Module-stable crash (e.g. CRT): offset is the same on every game build.
            off = next(iter(builds.values()), "?")
            where = mb.get("module") or "a fixed module"
            status = (f"build-independent -- faults in {where} at {off} "
                      f"(offset stable across game builds)")
        elif tds_key and tds_key.upper() in hex_builds:
            status = f"build {tds_key} is catalogued -> fault offset {hex_builds[tds_key.upper()]}"
        elif tds_key:
            known = ", ".join(f"{k}={v}" for k, v in builds.items()) or "(none yet)"
            status = (f"build {tds_key} NOT yet catalogued -- matched by instruction bytes "
                      f"(build-independent); known builds: {known}")
        else:
            status = "host build unknown; matched by instruction bytes"
        return entry, status
    return None, None


def print_report(d):
    p = d["path"].split("/")[-1]
    print(f"\n==================== {p} ====================")
    htime = fmt_time(d["header_time"])
    print(f"streams={d['nstreams']}  modules={len(d['modules'])}  size={d['size']}")
    line = f"sha256={d['sha256'][:16]}…  captured={htime}"
    if d['pid']:
        line += f"  pid={d['pid']}"
    if d['proc_create'] and d['header_time']:
        line += f"  uptime-at-crash={d['header_time'] - d['proc_create']}s"
    print(line)
    if d["sysinfo"]:
        si = d["sysinfo"]
        print(f"system: {ARCH_NAMES.get(si['arch'], si['arch'])}, {si['ncpu']} CPUs, Windows {si['os']}")
    # GPU inferred from driver modules
    gpus = set()
    for _, _, name in d["modules"]:
        sn = name.split("\\")[-1].lower()
        for k, v in GPU_HINTS.items():
            if sn.startswith(k):
                gpus.add(v.split(" (")[0])
    if gpus:
        print(f"gpu: {', '.join(sorted(gpus))}")

    # Host EXE build fingerprint. The faulting module's offsets are only valid
    # for a specific game build, so surface the build so two dumps can be told
    # apart (e.g. a fault offset that "moved" is really just a different build).
    exe_full, m = host_exe(d)
    if exe_full and m:
        ver = f"  FileVersion={m['fileversion']}" if m["fileversion"] else ""
        print(f"build: {exe_full.split(chr(92))[-1]}  "
              f"TimeDateStamp=0x{m['timedatestamp']:08X}{ver}")

    if d["exc_code"] is not None:
        name = EXC_NAMES.get(d["exc_code"], "UNKNOWN")
        print(f"\n[EXCEPTION] code=0x{d['exc_code']:08X} {name}")
        print(f"  exception address = 0x{d['exc_addr']:016x}  -> {d['mod_for'](d['exc_addr'])}")
        print(f"  crashed thread id = {d['crashed_tid']}")
        if d["fault_addr"] is not None:
            print(f"  access violation: {d['av_rw']} at 0x{d['fault_addr']:016x}")

    # Registers
    if d["regs"]:
        print(f"  RIP=0x{d['fault_rip']:016x} -> {d['mod_for'](d['fault_rip'])}")
        print("  registers:")
        for i in range(0, len(GP_REGS), 4):
            row = GP_REGS[i:i + 4]
            print("    " + "  ".join(f"{nm}=0x{d['regs'][nm]:016x}" for nm, _ in row))

    # Faulting instruction
    if d["code"]:
        print(f"\n[FAULTING INSTRUCTION] @ 0x{d['fault_rip']:016x}")
        print(f"  bytes: {d['code'].hex()}")
        if _CS:
            for insn in _CS.disasm(d["code"], d["fault_rip"]):
                print(f"  asm:   {insn.mnemonic} {insn.op_str}")
                break
        else:
            print("  (install 'capstone' for disassembly)")

    # Crash classification + root-cause heuristic
    cls = classify(d)
    if cls["class"]:
        print(f"\n[CRASH CLASS] {cls['class']}")
    print("\n[ROOT-CAUSE HINTS] (heuristic & NOT exhaustive -- always corroborate with the")
    print("  live call stack below and the paired .log; vanilla MX Bikes has many distinct crashes)")
    if cls["hints"]:
        for n in cls["hints"]:
            print(f"  {n}")
    else:
        print("  no specific signature matched -- this is not a recognized pattern, so don't assume")
        print("  a prior diagnosis. Inspect the registers, faulting instruction, and live stack by hand.")

    # Known-crash registry match (build-independent: matches on instruction bytes)
    registry = load_known_registry(d.get("path"))
    if registry is not None:
        entry, status = match_known(d, registry)
        if entry:
            print(f"\n[KNOWN CRASH] {entry.get('name', entry.get('id'))}")
            if entry.get("summary"):
                print(f"  {entry['summary']}")
            if entry.get("trigger"):
                print(f"  trigger: {entry['trigger']}")
            print(f"  workaround: {entry['workaround'] if entry.get('workaround') else 'none known'}")
            print(f"  {status}")
        else:
            print("\n[KNOWN CRASH] no registry match -- possibly a NEW signature; "
                  "analyse by hand and consider adding it to known_game_crashes.json")

    # Stack scan
    live_frames = []  # ordered (sp, val, mod) for sp >= RSP -- includes residue
    if d["thread_stack"] and d["thread_stack"][1]:
        base, stk = d["thread_stack"]
        fault_rsp = d["fault_rsp"]
        print(f"\n[STACK SCAN] crashed thread stack {len(stk)} bytes @ 0x{base:016x}")
        seen, counts = [], {}
        for i in range(0, len(stk) - 8, 8):
            val = struct.unpack_from("<Q", stk, i)[0]
            m = d["mod_for"](val)
            if m:
                mn = m.split("+")[0]
                counts[mn] = counts.get(mn, 0) + 1
                seen.append((base + i, val, m))
        print("  module occurrence counts (return-addr candidates):")
        for mn, c in sorted(counts.items(), key=lambda x: -x[1]):
            print(f"    {c:5d}  {mn}")
        plug = [(sp, val, m) for sp, val, m in seen if "mrp3" in m.lower()]
        print(f"\n  PLUGIN (mxbmrp3) frames found in stack: {len(plug)}")
        for sp, val, m in plug:
            live = " LIVE" if (fault_rsp and sp >= fault_rsp) else " (dead/residue)"
            print(f"    sp=0x{sp:016x}  0x{val:016x}  {m}{live}")
        if fault_rsp:
            live_frames = [(sp, val, m) for sp, val, m in seen if sp >= fault_rsp]
            print(f"\n  LIVE CALL STACK (sp >= RSP=0x{fault_rsp:x}), in call order:")
            print("  NOTE: this is a scan, not a real unwind -- it includes stale residue between")
            print("  genuine return addresses. The frames nearest RSP (top) are the most reliable.")
            for sp, val, m in live_frames:
                tag = "  <<< PLUGIN" if "mrp3" in m.lower() else ""
                print(f"    sp=0x{sp:016x}  0x{val:016x}  {m}{tag}")

    # Third-party / injected modules
    extra_mods = []
    for base, size, name in d["modules"]:
        low = name.lower()
        sn = name.split("\\")[-1]
        if "\\windows\\" in low or "mx bikes" in low:
            continue
        tag = ""
        for k, v in INJECTOR_HINTS.items():
            if k in low:
                tag = f"   <- {v}"
                break
        extra_mods.append((name, tag))
    if extra_mods:
        print("\n[THIRD-PARTY / NON-SYSTEM MODULES]")
        for name, tag in extra_mods:
            print(f"    {name}{tag}")

    # API-proxy / wrapper injectors (ReShade, ENB, SpecialK, dgVoodoo, ...). A core
    # graphics/input/system DLL loaded from the GAME folder (or anywhere outside
    # \Windows\) instead of System32 is a proxy-DLL injection: Windows' DLL search
    # order loads the app-dir copy first, and that copy chain-loads the real one. For
    # an OpenGL game like MX Bikes, ReShade ships as opengl32.dll in the game dir, so
    # the same basename ends up loaded TWICE (proxy + real System32). The generic
    # third-party scan above intentionally skips game-folder modules (so it MISSES
    # these), hence this dedicated pass -- it matters because such a wrapper sits
    # directly in the render/input path and is a prime suspect for a fault there.
    PROXY_DLLS = {"opengl32", "dxgi", "d3d8", "d3d9", "d3d10", "d3d11", "d3d12",
                  "ddraw", "dinput8", "dsound", "winmm", "version", "wininet"}
    name_counts = Counter(name.split("\\")[-1].lower() for _, _, name in d["modules"])
    proxies = []
    for base, size, name in d["modules"]:
        sn = name.split("\\")[-1]
        stem = sn.lower().rsplit(".", 1)[0]
        if stem in PROXY_DLLS and "\\windows\\" not in name.lower():
            why = "loaded from outside System32 (game-folder proxy DLL)"
            if name_counts[sn.lower()] > 1:
                why += " + DUPLICATE basename also loaded (classic ReShade/ENB proxy chain)"
            proxies.append((name, why))
    if proxies:
        print("\n[API-PROXY / WRAPPER INJECTORS]  (ReShade / ENB / SpecialK etc. -- hook the render/input API)")
        for name, why in proxies:
            print(f"    {name}\n        <- {why}")
        print("    A wrapper here sits in the API path; if it is on the crash stack, treat it as a")
        print("    prime suspect for a render/input fault and re-test with it removed.")

    # Verdict
    print("\n[VERDICT]")
    fault_addr_for_mod = d["fault_rip"] or d.get("exc_addr", 0)
    crash_mod = d["mod_for"](fault_addr_for_mod) or "?"
    print(f"  faulting instruction: {crash_mod}")
    # If the fault is in a system/runtime leaf (CRT, ntdll, GPU driver), the
    # culprit is the caller -- find the nearest non-system live frame.
    culprit = crash_mod
    if d.get("is_system") and d["is_system"](fault_addr_for_mod):
        nearest = next((m for sp, val, m in live_frames if not d["is_system"](val)), None)
        print(f"  ^ this is a system/runtime leaf (not the culprit). Nearest non-system caller:")
        print(f"    {nearest or '(none found in scan -- needs a symboled unwind)'}")
        culprit = nearest or crash_mod
    if culprit and "mrp3" in culprit.lower():
        where = "the PLUGIN (mxbmrp3)"
    elif culprit and "mxbikes" in culprit.lower():
        where = "the game (mxbikes.exe)"
    elif culprit and culprit != "?":
        where = f"{culprit.split('+')[0]}"
    else:
        where = "undetermined"
    line = f"  => likely culprit: {where}"
    if cls["class"]:
        line += f"; crash class: {cls['class']}"
    print(line)
    print("  (heuristic attribution from a stack scan -- confirm with a symboled full dump)")


def signature(d):
    code = d["code"].hex() if d["code"] else None
    return (d["exc_code"], d["mod_for"](d["fault_rip"]) if d["fault_rip"] else None,
            code, classify(d)["truncation"])


def compare(a, b):
    da, db = parse_dump(a), parse_dump(b)
    print(f"\n==================== COMPARE ====================")
    if da["sha256"] == db["sha256"]:
        print("  IDENTICAL FILES (same sha256) -- this is the SAME dump re-sent, not a new crash.")
        return
    sa, sb = signature(da), signature(db)
    print(f"  A: {a.split('/')[-1]}  captured {fmt_time(da['header_time'])}  pid={da['pid']}")
    print(f"  B: {b.split('/')[-1]}  captured {fmt_time(db['header_time'])}  pid={db['pid']}")
    labels = ["exception code", "fault module+offset", "instruction bytes", "truncation?"]
    same = True
    for i, (lab, va, vb) in enumerate(zip(labels, sa, sb)):
        mark = "==" if va == vb else "!="
        if va != vb:
            same = False
        if i == 0:  # exception code -> hex
            va = f"0x{va:08X}" if va is not None else None
            vb = f"0x{vb:08X}" if vb is not None else None
        print(f"  {lab:22s} {mark}  A={va}  B={vb}")

    # Game build (host .exe TimeDateStamp): two dumps with the same fault offset
    # but different builds are NOT directly comparable -- offsets shift per build.
    ta, tb = host_exe_tds(da), host_exe_tds(db)
    bmark = "==" if ta == tb else "!="
    print(f"  {'game build (exe TDS)':22s} {bmark}  A={ta}  B={tb}")
    if ta != tb:
        print("    (different game builds -- a moved fault offset across these is expected)")

    print(f"\n  => {'SAME crash signature (same bug)' if same else 'DIFFERENT crashes'}")


# ---- per-crash provenance ('samples' in the signature registry) ----
# 'samples' records the source files behind a CATALOGUED crash, stored inside that
# crash's entry. It answers "which .dmp/.log files do I keep for this bug?".
def make_sample(d, path, note=""):
    """Provenance record for one dump, to append to a crash entry's 'samples'.
    Idempotency is by sha256. 'source' is the file's basename -- in the normal
    workflow that's the real crash filename (mxbmrp3_crash_<date>_<pid>.dmp), whose
    date+pid suffix also names the paired .log. 'note' is a free-text field (never
    auto-filled) for things like a video link; set it via --record --note."""
    rip = d.get("fault_rip")
    fault = (d["mod_for"](rip) or "?") if rip else "?"
    return {
        "source": os.path.basename(path),
        "sha256": d.get("sha256", "?"),
        "captured": fmt_time(d["header_time"]) if d.get("header_time") else "?",
        "build": host_exe_tds(d) or "?",
        "fault": fault,
        "note": note or "",
    }


def record_sample(reg_path, d, dump_path, note=None):
    """Append this dump's provenance to the matching crash's 'samples' list and write
    the registry back. Records ONLY dumps that match a catalogued crash -- the
    catalogue tracks understood crashes, so a non-match is reported and skipped rather
    than inventing an entry. Idempotent by sha256: a dump already on file is not
    duplicated, but a supplied --note still updates that existing sample's note (so you
    can attach a video link after the fact). Returns a status string."""
    src = os.path.basename(dump_path)
    try:
        with open(reg_path, "r", encoding="utf-8") as f:
            reg = json.load(f)
    except Exception as e:
        return f"ERROR: cannot read registry {reg_path}: {e}"
    entry, _ = match_known(d, reg)
    if not entry:
        return ("NOT RECORDED -- no known-crash match. The catalogue only tracks "
                "understood crashes; add a crash entry for this signature first, then "
                "re-run with --record.")
    samples = entry.setdefault("samples", [])
    sha = d.get("sha256", "")

    def write_back():
        with open(reg_path, "w", encoding="utf-8") as f:
            json.dump(reg, f, indent=2, ensure_ascii=False)
            f.write("\n")

    for s in samples:
        if s.get("sha256") == sha:
            if note is not None and note != s.get("note", ""):
                old = s.get("note", "")
                s["note"] = note
                try:
                    write_back()
                except Exception as e:
                    return f"ERROR: cannot write registry {reg_path}: {e}"
                what = "set" if not old else "updated"
                return (f"already on file under '{entry['id']}' as '{s.get('source')}' "
                        f"-- note {what}")
            return (f"already on file under '{entry['id']}' as '{s.get('source')}' "
                    f"(sha256 {sha[:16]}…) -- no change")
    samples.append(make_sample(d, dump_path, note or ""))
    try:
        write_back()
    except Exception as e:
        return f"ERROR: cannot write registry {reg_path}: {e}"
    tail = " (with note)" if note else ""
    return (f"recorded '{src}' under '{entry['id']}'{tail} "
            f"({len(samples)} sample(s) on file)")


def main(argv):
    global KNOWN_PATH
    # Optional: --known <registry.json> overrides registry auto-discovery.
    if "--known" in argv:
        i = argv.index("--known")
        try:
            KNOWN_PATH = argv[i + 1]
            del argv[i:i + 2]
        except IndexError:
            print("usage: mdmp_analyze.py --known <registry.json> <file.dmp> ...")
            return
    if argv and argv[0] == "--compare":
        if len(argv) != 3:
            print("usage: mdmp_analyze.py --compare <a.dmp> <b.dmp>")
            return
        compare(argv[1], argv[2])
        return
    # --record: append each dump's provenance to the matching crash's 'samples' list
    # in known_game_crashes.json (matched crashes only; non-matches are reported, not added).
    # --note "<text>": free-text note (e.g. a video link) attached to the sample(s)
    # recorded this run -- also updates the note on a dump that's already on file.
    record = "--record" in argv
    note = None
    if "--note" in argv:
        i = argv.index("--note")
        try:
            note = argv[i + 1]
            del argv[i:i + 2]
        except IndexError:
            print("usage: mdmp_analyze.py --record --note \"<text>\" <file.dmp> ...")
            return
    if "--record" in argv:
        argv.remove("--record")
    if note is not None and not record:
        print("note: --note has no effect without --record (ignored)")
        note = None
    for p in argv:
        d = parse_dump(p)
        print_report(d)
        if record:
            reg_path = find_registry_path(p)
            if not reg_path:
                print("\n[RECORD] no known_game_crashes.json found -- cannot record")
            else:
                print(f"\n[RECORD] {record_sample(reg_path, d, p, note)}")


if __name__ == "__main__":
    main(sys.argv[1:])
