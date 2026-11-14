"""Check unsigned AMD64 kernel PE and matching MSF 7 PDB, without third-party modules."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import uuid


def require(condition, message):
    if not condition:
        raise ValueError(message)


def pe_identity(data):
    require(data[:2] == b"MZ", "Missing DOS header")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    require(data[pe:pe + 4] == b"PE\0\0", "Missing PE header")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    require(machine == 0x8664, "Driver is not AMD64")
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    require(struct.unpack_from("<H", data, opt)[0] == 0x20B, "Driver is not PE32+")
    require(struct.unpack_from("<H", data, opt + 68)[0] == 1, "Driver subsystem is not Native")
    flags = struct.unpack_from("<H", data, opt + 70)[0]
    require(flags & 0x100, "NX compatibility flag is missing")
    require(flags & 0x40, "ASLR dynamic-base flag is missing")
    require(struct.unpack_from("<I", data, opt + 108)[0] >= 7, "Missing data directories")
    directories = opt + 112
    require(struct.unpack_from("<II", data, directories + 4 * 8) == (0, 0),
            "SYS has an embedded certificate; unsigned output required")
    sections = []
    for i in range(count):
        offset = opt + optional_size + i * 40
        virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", data, offset + 8)
        characteristics = struct.unpack_from("<I", data, offset + 36)[0]
        require(not (characteristics & 0x20000000 and characteristics & 0x80000000),
                "Writable executable section")
        sections.append((rva, max(virtual_size, raw_size), raw))

    def file_offset(rva):
        for start, size, raw in sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise ValueError("RVA outside PE sections")

    debug_rva, debug_size = struct.unpack_from("<II", data, directories + 6 * 8)
    require(debug_rva != 0 and debug_size % 28 == 0, "Missing/malformed debug directory")
    debug = file_offset(debug_rva)
    for pos in range(debug, debug + debug_size, 28):
        _, _, _, _, kind, size, _, raw = struct.unpack_from("<IIHHIIII", data, pos)
        if kind == 2:
            cv = data[raw:raw + size]
            if cv[:4] == b"RSDS":
                return {"machine": "AMD64", "subsystem": "Native", "embeddedSignature": False,
                        "nxCompatible": True, "dynamicBase": True,
                        "pdbGuid": str(uuid.UUID(bytes_le=cv[4:20])),
                        "pdbAge": struct.unpack_from("<I", cv, 20)[0],
                        "pdbName": cv[24:].split(b"\0", 1)[0].decode("utf-8", errors="replace").replace("\\", "/").split("/")[-1]}
    raise ValueError("Missing RSDS PDB identity")


def pdb_identity(data):
    require(data[:32] == b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0", "PDB is not MSF 7")
    block_size, _, block_count, directory_size, _, block_map = struct.unpack_from("<6I", data, 32)
    require(block_size in (512, 1024, 2048, 4096, 8192), "Invalid PDB block size")
    require(block_count * block_size <= len(data), "Truncated PDB")
    directory_blocks = (directory_size + block_size - 1) // block_size
    require(directory_blocks * 4 <= block_size, "Unsupported PDB directory block map")
    indexes = struct.unpack_from(f"<{directory_blocks}I", data, block_map * block_size)
    directory = b"".join(data[n * block_size:(n + 1) * block_size] for n in indexes)[:directory_size]
    streams = struct.unpack_from("<I", directory)[0]
    require(2 <= streams <= len(directory) // 4, "Invalid PDB stream count")
    sizes = struct.unpack_from(f"<{streams}I", directory, 4)
    cursor = 4 + 4 * streams
    for i, size in enumerate(sizes):
        blocks = 0 if size == 0xFFFFFFFF else (size + block_size - 1) // block_size
        indexes = struct.unpack_from(f"<{blocks}I", directory, cursor)
        cursor += 4 * blocks
        if i == 1:
            stream = b"".join(data[n * block_size:(n + 1) * block_size] for n in indexes)[:size]
            require(len(stream) >= 28, "Truncated PDB information stream")
            return {"guid": str(uuid.UUID(bytes_le=stream[12:28])),
                    "age": struct.unpack_from("<I", stream, 8)[0]}
    raise ValueError("Missing PDB information stream")


def validate(folder):
    expected = {"timecard.sys", "timecard.inf", "timecard.cat", "timecard.pdb", "timecard.ico"}
    expected.update(f"timecard-{name}.ico" for name in
                    ("atomic", "flash", "gnss", "gnss2", "i2c", "nmea", "phc", "ptm", "sma", "timing", "tod"))
    actual = {p.name for p in folder.iterdir()}
    require(actual == expected, f"Unexpected package file set: {actual ^ expected}")
    pe = pe_identity((folder / "timecard.sys").read_bytes())
    pdb = pdb_identity((folder / "timecard.pdb").read_bytes())
    require(pe["pdbGuid"] == pdb["guid"] and pe["pdbAge"] == pdb["age"], "SYS and PDB do not match")
    require(pe["pdbName"].lower() == "timecard.pdb", "Unexpected PDB name")
    raw = (folder / "timecard.inf").read_bytes()
    inf = raw.decode("utf-16" if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8-sig")
    require(re.search(r"DriverVer\s*=\s*11/13/2026,1\.50\.0\.0", inf), "Wrong INF DriverVer")
    require("[Standard.NTamd64.10.0.1..22000]" in inf, "Wrong INF OS target")
    require(re.search(r"KmdfLibraryVersion\s*=\s*1\.33", inf), "Wrong KMDF version")
    require((folder / "timecard.cat").stat().st_size > 0, "Empty catalog")
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.iterdir())}
    return {"status": "pass", "driverVersion": "1.50.0.0", "abi": 15, "pe": pe, "pdb": pdb,
            "pdbMatches": True, "fileCount": len(expected), "sha256": hashes,
            "limitations": "Structural and identity checks only. Catalog generation is checked by WDK build logs. No Microsoft signing or hardware qualification."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(validate(args.folder), indent=2) + "\n"
    if args.output:
        args.output.write_text(result, encoding="utf-8")
    print(result)
