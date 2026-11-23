# Latency metadata for Wyldtone Essentials

Latency is an **algorithmic delay in whole input sample frames**, not an
intentional feedback delay or the time spent executing a DSP callback.

The public SDK provides two optional WAP exports, `plugin_latency_samples`
and `plugin_max_latency_samples`; the host may compensate them at Split/Mix.

| Plugin | Current latency | Maximum | Reason |
| --- | ---: | ---: | --- |
| Modern Distortion | 16 | 16 | Existing internal 4x oversampler and dry-path alignment |
| Vintage Fuzz | 16 | 16 | Existing internal 4x oversampler and dry-path alignment |
| Warm Drive | 16 | 16 | Existing internal 4x oversampler and dry-path alignment |
| Noise Reduction | 511 | 511 | 512-point STFT / overlap-add pipeline |
| Poly Octaver (legacy) | 0 or 4096 | 4096 | FFT_SIZE dry-align mode; unaligned dry intentionally remains 0 |
| Poly Octaver Live v0.2 | 0 or nominal 512 | 512 | Optional half-window dry-align mode |
| Poly Octaver Studio v0.2 | 0 or nominal 1024 | 1024 | Optional half-window dry-align mode |

When `dry_align` is OFF, polyphonic octavers mix a zero-latency dry
component with delayed spectral voices. There is **no single exact output
latency** in that mode; 0 is a conservative backward-compatible declaration.
The v0.2 wet paths have approximate, frequency-dependent group delay even
with dry alignment enabled. Validate recorded impulses and actual signals
before phase-critical parallel use.

The rest of the Essentials plugins operate with direct paths (0 algorithmic
latency) or intentionally delayed wet paths (echo/chorus/reverb), which do not
qualify for plugin delay compensation.

Old WAP binaries do not gain these exports automatically: rebuild the
affected plugins against the updated SDK. Old hosts ignore optional exports,
while the updated Engine uses them to align parallel paths.

## Updated WAP versions

Patch versions were incremented to ensure clients and the Wyldtone Cloud
can distinguish rebuilt, latency-aware WAPs from earlier packages:

| Plugin | Previous | New |
|---|---|---|
| Modern Distortion | 0.1.0 | 0.1.1 |
| Vintage Fuzz | 0.1.0 | 0.1.1 |
| Warm Drive | 0.1.0 | 0.1.1 |
| Noise Reduction | 0.2.1 | 0.2.2 |
| Poly Octaver | 0.1.0 | 0.1.1 |
| Poly Octaver Live | 0.2.0 | 0.2.1 |
| Poly Octaver Studio | 0.2.0 | 0.2.1 |

Plugin IDs and parameter IDs are unchanged. Existing preset references will
continue to use the same plugin identity, while version-pinned presets must
follow the existing Wyldtone upgrade/compatibility rules.
