# Amp Collection (Wyldtone WAP)

**Amp Collection** is a single stereo Wyldtone WebAssembly plugin containing the 12 algorithmic
amp models from [Dan Michael's 0xFX](https://github.com/averagenative/0xFX).
Select the model through the `Amp Model` **Choice** parameter; there are no
neural NAM files and no external Cabinet/IR dependencies.

## Included models (Choice order)

| Choice | Amp |
| ---: | --- |
| 0 | Fullerton Clean |
| 1 | British Crunch |
| 2 | Southwest Lead |
| 3 | Essex Chime |
| 4 | Tweed Blues |
| 5 | Meridian High Gain |
| 6 | Citrus Roar |
| 7 | Citrus Terror |
| 8 | Regent 800 |
| 9 | Solar Monolith |
| 10 | Eclipse Drone |
| 11 | Emerald Ratrod Deluxe |

The models come from the original 0xFX C99 `amp.c` and `biquad.c`,
kept in `vendor/0xfx` with a trimmed derivative header. Upstream source
revision: `5b248779c96dce6e64d0ed29cfa598014dc4d3c4`. The adapter uses
its own per-channel DSP states, a short crossfade on model selection and the
WAP stereo bus ABI. Audio-thread processing performs no allocations.

## Parameters

`Amp Model` is a real WAP `Choice` with the upstream's original names.
The other fixed host-visible normalized parameters are `Gain`, `Volume`,
`Bass`, `Mid`, `Treble`, `Presence`, `Sag`, `Master`, `Cut`,
`Tone`, `Feedback` and the appended **Oversampling** Choice (Off / 2x / 4x).
The 12 existing parameter indices and the stable plugin ID do not change.
Old presets default to **Off (1x)**, so they keep their previous tone and timing.


**Model-specific parameters:** Cut affects Essex Chime, Tone affects Citrus
Terror and Feedback affects Eclipse Drone. These are exposed on all models
because WAP parameter schemas are static. `Bright` is deliberately omitted:
the original 0xFX `amp.c` declares that parameter but does not implement
any processing behavior for it. Other model-dependent parameters may also
be inactive.

The copied upstream `amp.c` includes a documented **MIT-derived Sag
fix**: supply-voltage droop now follows the target voltage under signal load.
Upstream compared envelope load to supply voltage, so Sag could remain
inactive. `tests/native_amp_test.cpp` contains a sustained-signal A/B test.

The SDK's C99 backend must compile the `.c` files with `emcc -std=c99`
before the C++ link. The first user build succeeded but logged 31
`-Wreorder-init-list` warnings because the C source was still treated as
C++; rebuild with the latest SDK PR #13 code.

The Wyldtone adapter provides streaming FIR anti-aliasing with Off (1x),
2x and 4x modes. Its two-stage 63-tap linear-phase half-band filters bracket
the complete nonlinear amp DSP (including preamp, tone stack and power amp).
The default is 1x for backward compatibility. 2x and 4x cost extra CPU, and
not every model benefits equally.

**Important latency limitation:** 2x introduces 31 input-rate samples and
4x introduces 46.5 samples of linear-phase group delay. The current WAP host
ABI does not provide dynamic latency compensation for this plugin. Parallel
dry/wet or split/mix processing may produce comb filtering when oversampling
is enabled. Leave oversampling Off for phase-critical parallel routing until
host latency reporting/compensation is implemented. Switching quality uses
a short crossfade but can still change path alignment.

The vendor 3rd-order TMB tone-stack filter now uses double-precision
coefficients and states: at high processing rates, single precision could
shift slow filter poles outside the unit circle, resulting in audible
low-frequency drift and DC offsets. All six sample rates 44.1/48/88.2/96/
176.4/192 kHz and all three quality levels are covered by native regression
tests; WASM/host listening and real-time performance testing are still needed.
Cabinet IR loading, mic simulation and post-amp effects are intentionally
not included: chain Wyldtone's existing **IR Loader** after the Amp.

## Build & test

This example uses the public SDK's **C/C++ builder**; it does not need JUCE.
The SDK branch `feat/wap-license-metadata-notices` adds two required
capabilities: mixed C99/C++ compilation through Emscripten, and license
notice packaging.

From the sibling `wyldtone-plugin-sdk` repo, mount the examples project
to `/workspace/projects` using `compose.override.yaml`:

```yaml
services:
  sdk:
    volumes:
      - ../wyldtone-dsp-examples/plugins:/workspace/projects
```

Then run:

```bash
./wyldtone build projects/0xFXAmpCollection/plugin.config.json
./wyldtone validate projects/0xFXAmpCollection/build/wyldtone/Amp-Collection.wap
```

The generated WAP must contain `plugin.wasm`, `manifest.json`,
`licenses/0xFX-MIT.txt` and `licenses/Wyldtone-Apache-2.0.txt`. To verify
with system unzip: `unzip -l <file.wap>`.

**Audio acceptance tests:** run clean and distorted bass/guitar signals at
44.1/48/96/176.4/192 kHz, test mono/true stereo isolation, change models while audio
plays (no clicks or NaN/Inf), confirm all twelve Choice labels and parameter
save/restore, verify model CPU cost, and compare against standalone 0xFX
using cabinet bypass. These live listening/CPU tests remain necessary even
after a successful WASM build. A NAM A2 model is not directly comparable to
these algorithmic amplifier models.

## License and attribution

- **Upstream 0xFX amp DSP and derived header:** MIT, Copyright (c) 2026
  Dan Michael. Full text: [licenses/0xFX-MIT.txt](licenses/0xFX-MIT.txt).
- **Wyldtone WAP adapter (`plugin.cpp`):** Apache License 2.0,
  Copyright 2026 Wyldtone contributors.
  Full text: [licenses/Wyldtone-Apache-2.0.txt](licenses/Wyldtone-Apache-2.0.txt).

The combined distributed plugin contains both MIT and Apache-2.0 code,
hence manifest SPDX expression `Apache-2.0 AND MIT`. The full texts
must be shipped **inside each WAP**, not just as a repository link or
as prose in `description`. No 0xFX GUI or third-party impulse responses
are vendored. Check all assets separately before extending the package.

This is independent of Wyldtone's future paid plugin entitlements
(`licensing` / DRM), which are not active in this WAP.
