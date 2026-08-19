# AGENTS.md migration map

This ledger maps all 14 sections of the former root guide to their new homes.

| Former section | New home |
| --- | --- |
| AI Agent Identity and Cognitive Rules | Root `AGENTS.md`, plus the mandatory firmware handoff |
| Development Environment Awareness | `environment.md` |
| Platform and Hardware Constraints | `hardware-resources.md` |
| Project Architecture | `architecture-hal.md` |
| Coding Standards | `coding-standards.md` |
| UI and Orientation Guidelines | `ui-activities.md` |
| Common Patterns | `ui-activities.md` |
| Testing and Debugging | `testing-debugging.md` |
| Git Workflow and Repository Awareness | `git-workflow.md` |
| Generated Files and Build Artifacts | `generated-files-cache.md` |
| Local Development Configuration | `environment.md` |
| Testing and Verification Workflow | `testing-debugging.md` and `firmware-handoff` |
| Serial Monitoring and Live Debugging | `testing-debugging.md` |
| Cache Management and Invalidation | `generated-files-cache.md` |

The August 19 refactor (`21ea3517c`, based on `a4a03c6b4`) initially copied
the detailed rules by section boundaries. The root retains project identity,
evidence, API-verification, resource-justification, and verification duties,
and adds human ownership and the firmware review gate. Reusable skills moved
from `.skills/` to `.agents/skills/`; their maintenance/discovery guidance lives
in `.agents/README.md`. `CLAUDE.md` remains a symlink to `AGENTS.md`.

The removed `.github/skills/crosspoint-reader.md` symlink targeted the absent
`.skills/SKILL.md`; the active instructions are the root guide and routed skills.

One preserved rule was intentionally superseded: local commits no longer require
completed device testing. They require explicit human approval; actual hardware
testing remains a human-owned prerequisite for opening a PR.

## Rebase onto October 6 develop

The reconciliation covers all 207 commits from `a4a03c6b4` through
`9610da702`. Among instruction files, upstream changed only `AGENTS.md`, in
`cdbe2a779` (#3184): translations now require `_bcp47`. That requirement is
retained in `generated-files-cache.md`, together with its ordering semantics
and the later build-time language selection contract.

Other changes are preserved in their upstream source/docs and made discoverable
through the existing topic rules, rather than copied into the always-loaded
root. The following map records the changes that affect agent guidance:

| Upstream area and representative commits | Guidance home and primary sources |
| --- | --- |
| Language ordering and compiled-language selection: `cdbe2a779`, `af820353a` | `generated-files-cache.md`; `docs/i18n.md`, `scripts/gen_i18n.py` |
| Catalog-owned script grouping and compact font catalog: `93d572fcb`, `3d5c4a509`, `a7df86fe2` | `generated-files-cache.md`; `sd-fonts.yaml`, manifest generator, `FontDownloadActivity` |
| Font compression, kerning, ligatures, prewarm and cache lifetimes: `c484dc72b`, `8141d7b9c`, `93b6fe111`, `c80c537f2`, `06b5d5b3a`, `f1d0af1af`, `9610da702` | `ui-activities.md`, `heap-discipline`; built-in fonts, `FontCacheManager`, `SdCardFont` |
| Runtime vector fonts and web upload: `aaf683798`, `99c2f09d1` | `ui-activities.md`, `hardware-resources.md`; `VectorFontSupport.h`, `FontInstaller`, web handler, `docs/sd-card-fonts.md` |
| X4 Classic, Metalio, board/display capabilities and USB-MSC ownership: `f021d1e5f`, `a68548d9c`, `e5dcc64fd`, `218dc6aaf`, `123f3760e` | `architecture-hal.md`, `hardware-resources.md`; `platformio.ini`, SDK board profiles, `HalDisplay`, `UsbDriveActivity` |
| Reader overlays, input precedence, gestures, headers and list navigation: `b4cd1af6b`, `e7a3bb488`, `b6cbb5e12`, `720b995a5`, `1f08ce934`, `a09460975` | `ui-activities.md`; shared UI hosts, `ButtonNavigator`, reader input, `docs/contributing/touch-and-ui.md` |
| Reusable/lazy list rows and PSRAM cover grids: `d707f14bd`, `dc9c3eabc`, `362c88112`, `f03d7f4db` | `ui-activities.md`; `FileBrowserActivity::provideRow`, `fui::ListNav`, theme gates |
| Fragmentation, ownership, framebuffer loans and render locking: `c33a8b0e8`, `c4d8c395d`, `3555ff556`, `10cb2c3b2`, `38280863a`, `56c677f68` | `architecture-hal.md`, `ui-activities.md`, `heap-discipline`; `HalMemory`, `BuildScratch`, `GfxRenderer`, `ActivityManager` |
| Library index, refresh/freshness and file rename: `652ae0d83`, `10d0aa1ec`, `e0fb688bf`, `4a6283db9` | `generated-files-cache.md`; `lib/LibraryIndex/`, existing rename flow, CLX1 in `docs/file-formats.md` |
| TXT/Markdown through the EPUB reader: `9c103c87f` | `generated-files-cache.md`, `testing-debugging.md`; `lib/Txt/`, `Epub`, reader/progress helpers |
| Tables, links, RTL/CJK/soft-flush layout and new spacing/indentation settings: `da3d50c24`, `7bcc3a692`, `ce6c6cbce`, `c81b58d33`, `794ac97f4`, `458be7ed2`, `d5693c321`, `7394e40fa` | `generated-files-cache.md`; `Section.cpp`, `ReaderRenderSpec`, `docs/file-formats.md` |
| SD plugins, semantic events, sidecars, protected reads and per-book keys: `2600a79ea`, `5b1f0605f`, `f331030f3` | Root plugin route and `architecture-hal.md` contract links; `docs/sd-plugins.md`, `docs/plugin-events.md`, SDK `ContentProtection` |
| HTTP path/filename handling and SDK-backed file streaming: `03c484778`, `b5fb406f5`, `cdac66ffe` | `architecture-hal.md`; HAL files, existing web helpers, `docs/webserver-endpoints.md`, `docs/webserver.md` |
| Toolchain pins, selective SDK build, scoped Git version, SD transfer/cache patches: `7db14a015`, `e1c267b46`, `93e98bb78`, `e33e3cf39`, `e9245489f`, `9ea86717b`, `bbb45c39a` | `environment.md`, `architecture-hal.md`, `testing-debugging.md`; `platformio.ini`, patch scripts, workflows, getting-started docs |
| CI board matrices, artifact links and OTA release naming: `fe6535985`, `71342eacb`, `46d912535` | `testing-debugging.md`; current workflows and OTA source |
| PSRAM monitoring, low-power/logging behavior, dictionary memory guidance: `3e60e1302`, `1f77b83db`, `5ab0f290a`, `039f03ec3` | `hardware-resources.md`, `testing-debugging.md`, `ui-activities.md`; `HalMemory`, power/logging source, `docs/dictionary.md` |

## Replaced stale statements

These are explicit corrections, not removed topics:

- X4 hardware facts remain the C3 baseline; they no longer describe every board.
  PSRAM and display/input/storage capabilities come from the selected profile.
- `gh_release` logs at level 1. USB mode 1 selects native Serial/JTAG on applicable
  boards. Miniz compatibility defines live in `lib/miniz/src/MinizConfig.h`, not an obsolete global
  build flag. Existing build-flag and destructor/close rules remain intact.
- The raw `main.cpp` activity example becomes the actual `ActivityManager`
  unique ownership, stack, and render-lock contract. Cleanup duties remain;
  task sizes are examples for workers, not defaults for the shared render task.
  Activities consume input supplied by the main loop rather than updating it.
- The static-font description applies to built-ins; runtime SD/vector ownership,
  prewarm, size-matched UI fallback, and cache release are documented separately.
  Font inventories and registration locations are source lookups, not old counts
  or fixed line ranges.
- Book caches are keyed by path, not content. Renames use the existing state
  handling; replacement at the same path cannot be detected by that hash.
  The old full-directory clear command remains, with its settings/progress loss
  stated; narrower cache clearing is preferred for verification.
- Fixed `book.bin` v7 / section v25 claims and the v26 example become source
  lookups and an illustrative increment. Format bump duties remain and cover
  layout changes plus completed/partial cache compatibility.
- The 50 KB C3 free-heap target remains a baseline; internal/PSRAM and largest
  blocks must also be measured. Checked buffer ownership replaces the generic
  debugging suggestion to move stack buffers to raw `malloc`.

No firmware, build, generated-source, translation, or release output is changed
by this reconciliation. The full upstream implementation and documentation
remain in the rebased history.
