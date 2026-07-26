# Repository working agreements

## Nix flake verification

- Before running Nix flake checks after adding or renaming files, register them in the Git index.
- Validate with `nix flake check` against the repository's Git flake input. Do not substitute `path:.`, because it bypasses Git source filtering.

## Specification decisions

- Before implementation, record user-confirmed specification decisions in the authoritative repository documents and use those documents, rather than conversation context alone, as the implementation source of truth.
