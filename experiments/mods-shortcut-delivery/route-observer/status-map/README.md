# Offline observer scalar address planner

`layout.py` derives a bounded read plan for named 4- or 8-byte OBJECT symbols in
one AArch64 little-endian ELF64 shared object. It opens only the supplied local
ELF and parses supplied `/proc/maps` text. It has no process, transport, pointer
following, or device access. Its output is an address plan, not read values or a
coherent snapshot.

Use `elf_layout`, then `parse_maps`, then `plan_reads`. Supply the target page
size; the private sampler obtains it from that process's auxiliary vector. The
4096-byte default is not discovery of a target's page size. A caller must bind
the ELF, module pathname, maps, file identity, and process lifetime separately.

## Address and mapping rules

The executable mapping anchors load bias using both the PT_LOAD virtual address
and file offset, rounded to the target page size. The data mapping is validated
against its own PT_LOAD displacement. Subtracting the RW map's file offset from
its address is insufficient when its `p_vaddr - p_offset` differs from RX.

All maps must have page-aligned bounds and offsets. Named module maps must agree
on device/inode and match the exact pathname; deleted or multiple ambiguous
instances are refused. Readable named rows are bounded by the rounded PT_LOAD
**file** extent, including the legitimate zero-filled tail of the final file
page. They cannot cover whole pages beyond that extent.

For each requested scalar, the entire rounded file prefix of its unique writable,
non-executable PT_LOAD must be covered by a union of matching named rows. This
allows the loader's read-only RELRO page and writable data page to contribute
separately. A missing prefix or a pure zero-file-size data load is refused.
The symbol must be an allocated, defined OBJECT within its declared section;
its complete 4- or 8-byte width must be aligned and inside the exact PT_LOAD
memory extent and one readable map.

Anonymous BSS is accepted only after the rounded file extent, with empty path,
zero device/inode/offset, and writable non-executable permissions. A merged
anonymous VMA may extend beyond the segment; every requested byte must still be
inside the segment's exact `p_memsz` bound. The planner does not attribute every
byte of that larger VMA to this module.

The loader can retain file-backed `---p` reservation rows between segments.
These are accepted only inside gaps in the union of rounded PT_LOAD memory
extents, with file offsets matching the initial file mapping's displacement.
They must share the named module identity. Readable/shared/wrong-offset rows,
rows crossing a load extent, and rows after the final load are refused.
Reservation rows never supply scalar prefix coverage or readable scalar storage.
This recognizes a bounded mapping shape; it does not prove which loader action
created the row.

## Review findings and controls

The first source fingerprint
`57be633a3487e5c278547df2a7ccd06490a725fdd86a95ebabf84614785cd8ab`
passed its original 33 checks but accepted three malformed cases during review:

1. A named data mapping extended into whole BSS pages and classified a BSS
   scalar as file-backed.
2. An executable map had both its starting address and file offset shifted by
   one byte, preserving the apparent bias despite invalid map alignment.
3. A request for only the BSS scalar succeeded with every named data-prefix
   mapping removed.

Those cases are retained in `test_layout.py` and must now fail. Three independent
in-memory controls remove exactly the file-extent, alignment, or prefix guard.
Each control must accept its corresponding invalid layout; a crash or unrelated
failure is not a passing control. The original untracked source was not archived
here; its recorded hash and observed cases are review evidence, while these
controls are targeted guard regressions, not a full replay of that source.

The final map suite passes 62 assertions, including RELRO union coverage,
reservation gaps, two ASLR positions, 4 KiB and 64 KiB target pages, a merged BSS
VMA, exact memory bounds, and the three discriminating controls. The ELF suite
passes 21 synthetic extraction assertions and 27 checks on the local built
observer. The latter verifies all 26 scalar widths (24 four-byte and two
eight-byte values), then plans reads using **fabricated** mappings. All 26 reads
in that particular fabricated mapping are in file-backed pages; it supplies no
actual runtime map or process-read evidence. The built binary is pinned in
`review-result.json`.

Run from this directory:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 test_layout.py
PYTHONDONTWRITEBYTECODE=1 "$PYTHON_WITH_PYELFTOOLS" test_elf_layout.py --binary ../out/encoder-route-observer.so
```

`test_layout.py` uses only the Python standard library. `elf_layout` and its
fixture suite require pyelftools. The fixture suite creates and removes local
synthetic ELF files. The optional built binary is an ignored build artifact;
no firmware, process maps, memory values, or runtime logs are included here.

## Limits

Only the explicitly named observer scalars are planned. Width and alignment do
not establish C++ atomicity of an external read, synchronization, consistency
between separate reads, or absence of torn values. Maps describe address ranges;
they do not establish source authenticity, prove that a mapped page still equals
its file, or retain a live process/module. Before/after caller checks can detect
some changes but cannot prove no intervening change occurred.

The planner does not prove native event delivery, argument provenance, hook
ownership, callback lifetime, queue ordering, hardware timing, or readiness to
activate a native UI operation. It intentionally refuses layouts outside these
mapping rules rather than guessing an address. No runtime or physical-device
operation was performed for this review.
