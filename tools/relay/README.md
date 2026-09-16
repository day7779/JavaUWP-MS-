# Bandit Relay

Bandit Relay turns a phone or PC into a mouse and a microphone for the Xbox
UWP launcher, and can play the console's game audio back on the same device.
Everything runs over UDP on the local network:

- App to Xbox mouse input: UDP `42731`.
- Xbox status to app: UDP `42732`.
- App to Xbox microphone: UDP `42733`.
- Xbox game audio to app: UDP `42734`.
- Gameplay packet: `dx,dy,l,r,m,scroll,x1,x2`.
- Menu packet: `ABSW:x,y,l,r,m,scroll,x1,x2`.
- Status packets: `MODE:GAMEPLAY ...`, `MODE:MENU ...`, `SYNC:x,y`,
  and `SYNCW:x,y`. The reply to `ping` starts with `javauwp_glfw_mouse:ready`
  and carries `mic=1` when the console build has the mic receiver.
- Mic packet (little-endian): `magic "BMA1"` (4) + `seq` (u32) +
  `sampleRate` (u32) + `channels` (u8) + `bitsPerSample` (u8) +
  `sampleCount` (u16) + PCM payload. The app sends 48000 Hz mono signed
  16-bit in 10 ms frames (480 samples).
- Game audio packet: same header with magic `BMA2`, sent by the console after
  the app subscribes with a `BMAS` keepalive every 1.5 s.

This tool does not launch Minecraft and does not handle account auth or
entitlement checks.

## Finding the Xbox

There is no IP address to type. When the app opens it looks for the launcher:
the last known address is tried first, then a broadcast and a sweep of the
local `/24` for anything answering `ping` on `42731`. The first console that
answers is saved, so the next launch connects straight away. If the console
stops answering for a few seconds mid session (a new DHCP lease, a reboot) the
app releases any held buttons, scans again and swaps to the new address
without leaving the relay screen. `Enter IP` on the search screen and
`Change IP` in the menu still allow a manual address.

Both devices have to be on the same network and the launcher has to be open
on the console; the relay listener starts with the launcher.

## Modes

After the console is found the app asks what to relay: `Mouse`, `Mic`, or
`Mouse + Mic`. The choice is remembered and can be changed from the menu. When
the console build predates the mic relay the mic options are greyed out.

- `Mouse` behaves like the previous mouse relay.
- `Mic` shows only the microphone panel. Nothing captures the mouse, so the
  ring can simply be tapped or clicked to mute and unmute.
- `Mouse + Mic` relays both. The mic control is placed where game input cannot
  reach it: on a PC it is `F6` and the `Mic` entry in the `Esc` menu, on a
  phone it is a pad in the top right corner that has to be held for half a
  second. A short tap only shows a hint.

The ring around the mic indicator moves with your voice while the mic is on
and shows a crossed out ring when it is muted. Muting stops sending packets
rather than sending silence.

Game audio is a separate toggle (`F7` or the menu). The app subscribes to the
console's audio helper and plays the stream through the device's default
output with about 80 to 250 ms of buffering.

## Microphone selection

The first time you pick a mode that uses the mic the app shows a list of
recording devices, starting with `System default`, and the chosen device is
remembered by name. `Mic input` in the menu opens the same list later. If the
remembered device is missing at the next start the app falls back to the
default and says so on the relay screen.

Android asks for the microphone permission the first time the mic is switched
on. iOS asks the first time as well and needs local network access accepted
for any of the relay to reach the console.

## Windows Build

```powershell
.\tools\relay\scripts\build-windows.ps1
```

Portable zip:

```powershell
.\tools\relay\scripts\package-windows.ps1
```

Output:

```text
dist/relay/BanditRelay-nightly-win-x64.zip
```

## Android Build

The Android project uses the SDL3 Android AAR and Gradle/NDK/CMake. Install
the Android SDK command-line tools, then install the same packages used by CI:

```powershell
sdkmanager "platforms;android-36" "build-tools;36.0.0" "ndk;28.2.13676358" "cmake;3.22.1"
```

```powershell
cd tools\relay\android
gradle :app:assembleDebug
```

Output:

```text
tools/relay/android/app/build/outputs/apk/debug/app-debug.apk
```

## iOS Build

iOS builds need Xcode, so they run on the `ios` job in
`.github/workflows/relay-nightly.yml` (macOS runner) rather than locally on
Windows. Push to a branch the workflow watches, or trigger it by hand, then grab
the `BanditRelay-nightly-ios` artifact.

The job produces an **unsigned** IPA. Signing happens on the PC with Sideloadly
or AltStore using your Apple ID, so no Mac is needed at any point.

Output:

```text
dist/relay/BanditRelay-nightly-ios.ipa
```

On a Mac the same build is:

```bash
cmake -S tools/relay -B build/ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build build/ios --config Release
```

Notes:

- SDL3 links statically on iOS. A sideloaded bundle cannot load an unembedded
  dylib, and one Mach-O keeps re-signing to just the app binary.
- `ios/Info.plist` carries `NSLocalNetworkUsageDescription` and
  `NSMicrophoneUsageDescription`. Without the first, iOS 14+ drops UDP to LAN
  addresses while `sendto` still reports success, which looks exactly like the
  Xbox not answering. The search screen retries on its own once the prompt is
  accepted.
- The bundle has no app icon yet, so it installs with a blank tile.
- Free-cert sideloads expire after 7 days and need re-signing.

## Controls

- `Esc` opens the relay menu and releases mouse capture. Resume captures it again.
- `F3` changes the Xbox IP by hand.
- `F6` mutes and unmutes the mic.
- `F7` toggles game audio.
- `F8` quits.
- `F9` toggles the local relay mode for troubleshooting.
- `F10` records sent and received packets to a log file.
- `1`, `2`, `3` pick a mode on the mode screen.
- On-screen utility buttons are hidden during normal desktop relay use so game
  clicks cannot hit them by accident. Touch devices keep their input buttons.
- Android and iOS: the app stays in landscape immersive mode. Drag empty space to
  move the mouse, hold `Hold L` or `Hold R` with one finger, and drag with another
  finger for held-click actions such as breaking blocks. `Wheel Up` and
  `Wheel Down` repeat while held. Hold the mic pad in the top right corner to
  mute or unmute.
