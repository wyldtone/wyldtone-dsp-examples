# Wyldtone DSP Examples

A collection of real DSP effects built for the public [Wyldtone Plugin SDK](https://github.com/wyldtone/wyldtone-plugin-sdk). These are **standalone plugin projects**, not part of the proprietary Wyldtone Audio Engine. You can study, fork and build them using the SDK alone.

## Projects

| Category | Example projects |
| --- | --- |
| Drive | ModernDistortion, VintageFuzz, WarmDrive |
| Dynamics | SmoothExpander, StudioCompressor, StudioGate, WyldtoneNoiseReduction |
| EQ / Filter | StudioEQ |
| Modulation | ClassicPhaser, EnvelopeWah, OpticalTremolo, StereoChorus, StereoFlanger |
| Pitch | AnalogOctaver, PolyOctaver, PolyOctaverLive, PolyOctaverStudio |
| Reverb | StudioReverb |
| Delay | TapeDelay |
| Utility | VolumeSwell |

All 20 example configurations declare an SDK-recognized category. The helper sources shared by several examples are in `plugins/wyldtone_fx_common/`; retain this directory layout when building.

## Build

Install Docker and clone both repositories next to one another:

```bash
git clone https://github.com/wyldtone/wyldtone-plugin-sdk.git
git clone https://github.com/wyldtone/wyldtone-dsp-examples.git
cd wyldtone-plugin-sdk
```

Create a local `compose.override.yaml` inside the SDK checkout:

```yaml
services:
  sdk:
    volumes:
      - ../wyldtone-dsp-examples/plugins:/workspace/projects
```

Build and validate an effect, for example:

```bash
./wyldtone build projects/StudioEQ/plugin.config.json
./wyldtone validate projects/StudioEQ/build/wyldtone/Studio-EQ.wap
```

Build and validate affected plugins locally before submitting changes. Automated build-and-validation CI is planned but not yet configured. See `CONTRIBUTING.md` to submit changes.

## Licensing and scope

**License review is pending.** This migration branch intentionally contains no license grant yet. Public visibility of source code does not by itself permit copying or commercial redistribution. Before the code is released for third-party reuse, the maintainer must audit ownership, embedded algorithms and dependencies and add the selected license. The intent is a permissive license suitable for commercial derivatives.
