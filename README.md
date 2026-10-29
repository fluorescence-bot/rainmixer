# RainMixer — Automatic Solution Mixer

<table>
  <tr>
    <td align="center" width="50%"><img src="docs/screenshots/kawaii_gui.png" alt="Kawaii GUI"><br><b>Kawaii GUI</b> (<code>code/src/kawaii_gui</code>)</td>
    <td align="center" width="50%"><img src="docs/screenshots/reglo_gui.png" alt="Reglo GUI"><br><b>Reglo GUI</b> (<code>code/src/reglo_gui</code>)</td>
  </tr>
</table>

An automatic solution mixer for a fluorescence scientist. A robot pumps up to
four liquids (A, B, C) into a mixing bowl (D), which is then pumped out to a
measuring instrument. The pumping is done by an Ismatec Reglo ICC peristaltic
pump, and a small robot rotates the cuvette (0–60°) for the measurement.

Built for the course Digital Fabrication and Maker Space.

**Before first use**, see the [setup guide](docs/setup-guide.md) for how to
connect the pump and the robot arm.

## Download

Ready-to-run apps, rebuilt automatically from `main` on every merge (no Python or
Node needed). Get them from the **Releases** box on the right of this page
(*RainMixer (latest build)* → *Assets*), or directly:

| Computer | Download | Then |
|---|---|---|
| Windows 10/11 | [RainMixer.exe](https://github.com/fluorescence-bot/rainmixer/releases/latest/download/RainMixer.exe) | Double-click it. If SmartScreen warns, choose *More info → Run anyway*. |
| Mac, Apple silicon (M1 or newer) | [RainMixer-macOS-AppleSilicon.zip](https://github.com/fluorescence-bot/rainmixer/releases/latest/download/RainMixer-macOS-AppleSilicon.zip) | Open the zip, double-click *RainMixer*. The first time, macOS can't check the app: open *System Settings → Privacy & Security*, scroll down and click *Open Anyway*. |
| Mac, Intel | [RainMixer-macOS-Intel.zip](https://github.com/fluorescence-bot/rainmixer/releases/latest/download/RainMixer-macOS-Intel.zip) | As above. |
| Linux (x86-64) | [RainMixer-Linux.tar.gz](https://github.com/fluorescence-bot/rainmixer/releases/latest/download/RainMixer-Linux.tar.gz) | Extract it and run `RainMixer/RainMixer`. |

Saved presets go in a `presets/` folder next to the app (Windows, Linux) or in
`Documents/RainMixer/presets` (Mac). `RainMixer-debug.exe` is the Windows app with
a console window that shows any error. To run from source instead:
`code/src/kawaii_gui/run.sh`.

## Repository layout

```
code/
├── src/
│   ├── kawaii_gui/       # Main desktop GUI (pywebview + TypeScript) — start here
│   ├── reglo_gui/        # Simpler Tkinter GUI for the Reglo pump + rotator
│   └── cuvette_rotater/  # Arduino firmware for the cuvette rotator (Nano)
└── lib/
    └── ismatec-pump/     # Python driver for the Ismatec Reglo ICC pump (GPL-3)
```

## CI

- `.github/workflows/test.yml`: Python and TypeScript tests plus the frontend build, on pushes and PRs.
- `.github/workflows/build-apps.yml`: builds the GUI for Windows, macOS and Linux
  with PyInstaller, smoke-tests each, and on every push to `main` replaces the
  `latest` release with them.

## License

The project's own code is [MIT](LICENSE). The vendored pump driver in
`code/lib/ismatec-pump` is GPL-3 (see its [LICENSE](code/lib/ismatec-pump/LICENSE)),
and since `RainMixer.exe` bundles it, the exe as a whole is distributed under GPL-3.
