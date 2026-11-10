# Contributing

DSP fixes and new examples are welcome under the repository's [Apache License 2.0](LICENSE).

- Keep each effect self-contained in `plugins/<Name>/`, with its own `plugin.config.json`.
- Use the [public SDK](https://github.com/wyldtone/wyldtone-plugin-sdk); do not depend on private Audio Engine sources.
- Declare one of the canonical categories from the SDK, with `Other` allowed for novel types.
- Add audio validation, deterministic offline tests and documentation where possible.
- Keep audio-thread DSP allocation-free and avoid blocking IO or locks in processing callbacks.
- Do not include third-party models, captures, binaries, samples or copyrighted source without redistribution permission.
- Run `./wyldtone build` and `./wyldtone validate` against each affected WAP.

Commercial derivatives are permitted under Apache 2.0, subject to attribution and other license conditions. Third-party sources and assets may have different licensing requirements.
