# Repository working agreements

## Nix flake verification

- Before running Nix flake checks after adding or renaming files, register them in the Git index.
- Validate with `nix flake check` against the repository's Git flake input. Do not substitute `path:.`, because it bypasses Git source filtering.
