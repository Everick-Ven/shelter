# Version comparison and port record

## Reference points

| Label | Git object | Meaning |
|---|---|---|
| `shelter-v1.0.164-reference-02e99c2` | `02e99c262b851ea9788bd5a9b96bf0e349e7f329` | Complete reference project snapshot from `arena/01a0fae6-shelter`. |
| `shelter-v1.0.160-pre-port-d0d3b32` | `d0d3b3289d44121149bb26583d6504014cffd279` | Snapshot of this session branch before importing the reference project. |
| `shelter-v1.0.165-port-02e99c2` | added after acceptance | Ported project version; version metadata is kept in sync across UI, CEF window, installer, and macOS DMG. |

The source histories do not share a merge base in this checkout. Therefore this port uses the **full tracked tree** from `02e99c2` as the coherent source snapshot (UI, `host-bridge.js`, CEF shell, platform code, packaging, and installer), rather than copying only the 55-line UI commit onto the incompatible `app/` bridge.

The labels and version numbers above describe historical snapshots; they do not
set the current product version. For current builds, `SHELTER_VERSION` in
`CMakeLists.txt` generates the native/UI and Windows resource versions, supplies
installer and workflow packaging metadata, and determines the versioned macOS
DMG name. The release job checks that the pushed `v*` tag agrees with that value.

## Acceptance gates

The packaged runtime job is intentionally blocking, not `continue-on-error`. It must:

1. report the packaged UI version generated from `SHELTER_VERSION`;
2. pass dashboard horizontal-overflow checks at browser viewport widths 1280, 997, 768, and 390 px;
3. open and render `https://example.com/` in a native CEF tab;
4. navigate that tab to `https://example.org/` and verify Back returns to `example.com`;
5. pass on the installed Windows package and the app copied from the macOS DMG.

Compare snapshots with:

```bash
git diff --stat shelter-v1.0.160-pre-port-d0d3b32..shelter-v1.0.165-port-02e99c2
git diff --stat shelter-v1.0.164-reference-02e99c2..shelter-v1.0.165-port-02e99c2
```

The three labels are annotated local Git tags so the reference and pre-port state remain directly inspectable. The release tag is not pushed as part of this port; only the session branch is pushed.
