# Contributing

DSP fixes and new examples are welcome once the source license has been finalized.

- Keep each effect self-contained in `plugins/<Name>/`, with its own `plugin.config.json`.
- Use the [public SDK](https://github.com/wyldtone/wyldtone-plugin-sdk); do not depend on private Audio Engine sources.
- Declare one of the canonical categories from the SDK, with `Other` allowed for novel types.
- Add audio validation, deterministic offline tests and documentation where possible.
- Keep audio-thread DSP allocation-free and avoid blocking IO or locks in processing callbacks.
- Do not include third-party models, captures, binaries, samples or copyrighted source without redistribution permission.
- Run `./wyldtone build` and `./wyldtone validate` against each affected WAP.

Commercial reuse will depend on the finalized repository license. Until that license is published, **do not assume a grant of rights from public availability**.
