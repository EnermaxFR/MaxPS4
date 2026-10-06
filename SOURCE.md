# Corresponding source for MaxPS4

This document identifies the upstream source and the MaxPS4 modifications used to produce the iOS build.

## Upstream engine

Project: shadPS4

Repository: https://github.com/shadps4-emu/shadPS4

Pinned upstream commit used by the current MaxPS4 build:

`b3452cf6a9bd0969771fcfbf919c359368473d1b`

To obtain that upstream revision:

```sh
git clone --recursive https://github.com/shadps4-emu/shadPS4.git
git -C shadPS4 checkout b3452cf6a9bd0969771fcfbf919c359368473d1b
git -C shadPS4 submodule update --init --recursive
```

## FFmpeg used by the iOS build

The current iOS build uses FFmpeg 7.1 built from source for iOS arm64. The build downloads the official `ffmpeg-7.1.tar.xz` source archive and compiles static libraries for the iPhoneOS SDK.

The FFmpeg configuration used by `.github/scripts/build66-patches.py` does not enable FFmpeg's `--enable-gpl` or `--enable-nonfree` options. The exact FFmpeg source tree used for a release build is included in the corresponding-source artifact under:

`third-party/ffmpeg-7.1/`

The corresponding-source package also records the MaxPS4 revision, shadPS4 revision and FFmpeg version used by the build. FFmpeg's own copyright and license files remain part of its included source tree and continue to apply.

## MaxPS4 iOS modifications

The iOS-specific changes are applied during the build by files committed in this repository, principally:

- `.github/workflows/maxps4-ipa.yml`
- `.github/workflows/maxps4-build66.yml`
- `.github/scripts/build66-patches.py`
- the source files under `MaxPS4-iOS/`

These files should be kept together with the exact MaxPS4 revision used to produce a distributed IPA so that the modified source can be reconstructed.

## Distribution note

If an IPA or other executable containing GPL-covered shadPS4 code is distributed, the complete corresponding source for that exact distributed build must also be made available in a manner compliant with the applicable GPL terms. This includes the source and build/patch scripts needed to reproduce the modified GPL-covered code, subject to the license's system-component exceptions.

For releases, record:

1. the MaxPS4 Git commit used for the IPA;
2. the pinned shadPS4 Git commit used by that MaxPS4 revision; and
3. the FFmpeg version and source used by that build.

## Licenses

The repository root contains MaxPS4's `LICENSE`. shadPS4's upstream repository contains its own `LICENSES/` directory with the license texts for shadPS4 and its bundled third-party components. The FFmpeg source tree included in the corresponding-source artifact contains FFmpeg's applicable copyright and license information. Original notices and copyright statements remain applicable.
