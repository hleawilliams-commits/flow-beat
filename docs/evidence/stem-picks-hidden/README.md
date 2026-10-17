# STEM PICKS header button hidden

The existing **STEM PICKS browser shortcut** setting is off in the emulator. The header now shows TAG MIX and MODS without STEM PICKS. The harmonic-picks 0.4.1 mod, its optional custom page and all saved source remain available. This is a settings change; no package, core, playback or physical USB change was made.

Verified on the actual 4 DECK screen after returning from Mod Menu. The working configuration at `/home/root/settings/azmod.conf` contains:

```ini
harmonic-picks:quick-button = off
```

The mod checks this setting every tick. No restart was needed or performed. Re-enable **Mod Menu > STEM PICKS browser shortcut > Enable** to restore the button. The shown state is emulator-only; no claim is made about the physical deck or a physical USB.

![Main screen without STEM PICKS](main-screen.png)
