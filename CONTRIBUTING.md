# Contributing

Builds, bug reports, new companion dashboards and docs fixes are all welcome.
The project is small, so the process is small.

## Before you start

- Read `SPEC.md`. It is the contract the firmware and the host tools are
  written against, and it is kept current with the code.
- Adding a companion dashboard for another game? Start from
  `docs/companion-dashboard-game-candidates-2026-09-13.md` (ranked games with
  RAM maps and disassemblies) and the Pokemon build as the worked example:
  `docs/pokemon-aux-display-plan-2026-09-12.md`, the pixel layout in
  `docs/pokemon-aux-display-layout.md`, the pack format in
  `docs/pokered-pack-format.md`, and `tools/pokered_pack.py` for the asset
  generator. Ship a generator, never the assets or a ROM.
- Open an issue for anything bigger than a bug fix so it does not collide
  with work in flight.

## Licensing of contributions

By contributing you agree that your contribution is licensed under the same
terms as the part of the project it lands in: Apache-2.0 for code, CC BY 4.0
for docs and drawings (see `NOTICE`). There is no contributor license
agreement to sign. Instead we use the Developer Certificate of Origin
(https://developercertificate.org): you certify that you wrote the change or
otherwise have the right to submit it under these licenses. Sign off every
commit with `git commit -s`.

## Style

- No em-dashes anywhere: prose, comments, commit messages.
- Compile-time settings live in `include/config.h`, pins in `include/pins.h`.
- Keep `SPEC.md` in step with any behaviour change in the same commit.
