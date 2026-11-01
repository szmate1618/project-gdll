# Interior zombie reserve memory estimate

Decision: reserve **10,000 additional stable actor slots in a shared session pool**.
This preserves the fixed crowd/renderer population and existing scenario routes.
See [encounter allocation and lifecycle](../FLOORPLANS.md#interior-zombie-encounters).

Measured on Linux x86-64 with an AMD Radeon 660M, comparing 20,000 normal actors
against 20,000 normal actors plus 11,682 dormant slots. Assets were all ten
installed characters, each with its original mesh, three LoDs, ten severed mesh
variants and eleven closure variants. No town generation or simulation step was
run. The larger reserve was the initial sizing experiment; the chosen 10,000-slot
column below is a linear estimate from that measurement.

| Additional allocated capacity | Measured: 11,682 slots | Estimated: 10,000 slots |
| --- | ---: | ---: |
| Headless authority CPU, without navigation | 8.02 MiB | 6.87 MiB |
| Headless authority CPU, with navigation | 12.17 MiB | 10.41 MiB |
| Multiplayer client CPU, including renderer | 62.31 MiB | 53.34 MiB |
| Standalone viewer CPU, including renderer | 65.07 MiB | 55.70 MiB |
| GPU instance buffers per renderer | 26.74 MiB | 22.89 MiB |
| Embedded host: client CPU + navigated authority CPU + GPU | 101.21 MiB | **86.64 MiB** |

Authority rows are alternatives; standalone includes authority work, so do not
add it to the server/client rows. Allow roughly another 1 MiB for encounter
records and multiplayer ID tables. Actual overhead varies with the number of
character variants and renderer geometry batches.

CPU values are baseline-subtracted `mallinfo2().uordblks + hblkhd`, including
allocated reserve capacity even where pages are not resident. GPU capacity was
counted by instrumenting `GL_STREAM_DRAW` `glBufferData` calls during renderer
construction: 96 bytes per slot per geometry, 250 buffers across ten characters,
giving 2,400 bytes per extra slot. Shared asset meshes and animation data are
excluded from the difference because both runs loaded the same assets.

These are allocation/capacity measurements, not process RSS or a frame-time
benchmark. On integrated graphics, adding GPU allocations to RSS can double count
shared physical memory. Driver/allocator peaks may vary. Costs after activation
(AI, ragdolls, wound palettes and network payloads) are excluded. Sleeping slots
avoid living queries, behavior activation and visible draws, but fixed-size CPU
and GPU bookkeeping remains allocated.
