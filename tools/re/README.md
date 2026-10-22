# Reverse-engineering helpers

Scripts used to produce `docs/PROTOCOL.md` and `MODULES.md`. None of ROCCAT's
binaries or artwork are part of this repository; point the scripts at your own
copy of the Swarm installer.

| Script | Purpose |
|---|---|
| `allmods.py SETTINGS.xml OUT.json` | ask Swarm's update server which module exists for every PID in `settings.xml` |
| `mkmods.py OUT.json MODULES.md` | render that answer as the module checklist |
| `qrc.py PE OUTDIR TREE NAMES DATA` | dump Qt resources (`qRegisterResourceData` arguments) from a PE file |
| `qm.py FILE.qm` | dump a Qt translation file as JSON |
| `qini.py FILE` | decode `@ByteArray(...)` values in a QSettings ini (e.g. `default.dat`) |
| `rtti.py PE CLASS...` | find Itanium-ABI vtables (MinGW builds) by mangled class name |
| `calls2.py PE ADDR...` | list direct `call`/`jmp` sites targeting the given addresses |
| `show.py DECOMP.c FUNC...` | print functions from a `DumpAll.java` decompilation |
| `FixFuncs.java` | Ghidra post-script: create functions referenced only from vtables |
| `FwFix.java` | Ghidra post-script for the raw mouse firmware: functions from the vector table and Thumb prologues |
| `DumpAll.java OUT.c` | Ghidra post-script: decompile every function into one file |

Workflow:

```sh
7z x "ROCCAT Swarm_v19482.exe" -oinst          # NSIS installer
python3 -c "import zlib,sys; d=open('inst/settings.xml','rb').read(); sys.stdout.buffer.write(zlib.decompress(d[4:]))" > settings.xml
python3 allmods.py settings.xml modules.json    # module list + download links
analyzeHeadless proj kar -import KONE_AIMO.dll -scriptPath . \
    -postScript FixFuncs.java -postScript DumpAll.java kar_all.c
```

The mouse firmware shipped in the module (`firmware/KONE_AIMO_16K_V1.05.bin`)
is a raw ARM Cortex-M image:

```sh
analyzeHeadless proj fw -import KONE_AIMO_16K_V1.05.bin -loader BinaryLoader \
    -processor ARM:LE:32:Cortex -loader-baseAddr 0x0 -scriptPath . \
    -postScript FwFix.java -postScript DumpAll.java fw_all.c
```

Findings are in [docs/PROTOCOL.md](../../docs/PROTOCOL.md#firmware-notes).

Module downloads (`.../swarm1/download/driver/2/<id>`) answer with a signed
object-storage URL to a 7z archive containing `data/<ALIAS>/<DLL>.dll`.
