# Generated lookup comparison

This synthetic test extracts the production index builder and lookup directly
from `EmitProject` output for two immutable emitter headers. It substitutes
only ordered segment metadata and stub block pointers; it never executes a
generated block or uses game data. The default baseline is local commit
`84568fcadc`, before the indexed early-NULL change. `--baseline-header` can
select a preserved header instead.

From a Visual C++ environment, using Python, CMake and a C/C++20 compiler:

```text
python tests/recompiler_lookup/run.py --work-dir <new-local-directory> [--generator Ninja] [--benchmark]
```

`--cc`, `--cxx` and `--cmake` select tools. MSVC uses release `/MD` and explicit
`/O2 /Ob2 /bigobj /fp:precise`; LTO is disabled. Keep the work directory local.
It retains copied headers, hashes, generated C, objects and the test executable.

Eight legs cover old/new lookup with successful/failed allocation, at low and
near-maximum 64-bit addresses. An independent linear oracle checks every byte
around the tables, side entries, holes, segment gaps, alignment, both endpoints,
wrap-around and seeded random PCs. Repeated index setup must not retry a failed
allocation. A deliberately wrong candidate answer must fail with exit 1.

The optional benchmark repeats `ABBAABBA` for valid hits, aligned holes,
misaligned PCs, outside PCs and a 75%-hit/25%-outside mix. Both arms perform the
same calls and checksums; setup and random generation occur before timing.
Compare medians only while other CPU work is idle. Reported Windows unwind
ranges cover the entry fragment, excluding any other fragments and callees.
Synthetic lookup timings and leaf CPU sample proportions do not establish game
FPS or exact savings. Full integration smoke checks remain separate.

To time an already validated executable in a later idle window:

```text
<work-dir>/build/lookup_test.exe --bench-only
```
