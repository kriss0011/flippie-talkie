---
name: Flippie Talkie
description: A matte black radio console where one white plot of real audio owns the screen.
colors:
  ground: "#050505"
  ink: "#ffffff"
  ink-2: "#bdbdbd"
  ink-3: "#8c8c8c"
  rule: "#2b2b2b"
  rule-strong: "#5c5c5c"
  raised: "#1b1b1b"
  link: "#3d86e6"
  rx: "#f2c230"
  tx: "#e6502e"
  tx-past: "#8a3a26"
  tx-pressed: "#f6c9bc"
typography:
  numeral:
    fontFamily: "Archivo, system-ui, sans-serif"
    fontSize: "clamp(150px, 34vh, 340px)"
    fontWeight: 200
    lineHeight: 0.98
    letterSpacing: "-0.03em"
  display:
    fontFamily: "Archivo, system-ui, sans-serif"
    fontSize: "48px"
    fontWeight: 300
    lineHeight: 1.05
  action:
    fontFamily: "Archivo, system-ui, sans-serif"
    fontSize: "32px"
    fontWeight: 500
    lineHeight: 1.1
  display-phone:
    fontFamily: "Archivo, system-ui, sans-serif"
    fontSize: "40px"
    fontWeight: 300
    lineHeight: 1.05
  brand:
    fontFamily: "Archivo, system-ui, sans-serif"
    fontSize: "18px"
    fontWeight: 600
  heading:
    fontFamily: "Archivo, system-ui, sans-serif"
    fontSize: "15px"
    fontWeight: 600
  body:
    fontFamily: "Archivo, system-ui, sans-serif"
    fontSize: "15px"
    fontWeight: 400
    lineHeight: 1.45
  small:
    fontFamily: "Archivo, system-ui, sans-serif"
    fontSize: "13px"
    fontWeight: 400
  caption:
    fontFamily: "Archivo, system-ui, sans-serif"
    fontSize: "12px"
    fontWeight: 400
  option:
    fontFamily: "Archivo, system-ui, sans-serif"
    fontSize: "16px"
    fontWeight: 400
rounded:
  none: "0"
spacing:
  s1: "4px"
  s2: "8px"
  s3: "12px"
  s4: "16px"
  s5: "24px"
  s6: "32px"
components:
  button-primary:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.ground}"
    rounded: "{rounded.none}"
    height: "44px"
    padding: "11px 22px"
  button-secondary:
    backgroundColor: "transparent"
    textColor: "{colors.ink}"
    rounded: "{rounded.none}"
    height: "44px"
    padding: "10px 16px"
  talk-button:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.ground}"
    typography: "{typography.action}"
    rounded: "{rounded.none}"
    height: "104px"
  talk-button-talking:
    backgroundColor: "{colors.tx}"
    textColor: "{colors.ground}"
---

# Flippie Talkie design

## Overview

The web app is a working radio console for Flipper owners at a desk or out with a phone. Its idea comes from a record sleeve: matte black, one white plot, very little else. The plot is the point, and it is always real audio.

The visual language lends type, palette, density and one signature move, the pulse field. Layout and controls stay standard, so nothing has to be learned.

## Colors

White is the ink. Everything else is neutral, apart from three coded hues that carry state and nothing else:

- **Link blue** means the Flipper is connected.
- **Receive yellow** means audible received audio.
- **Transmit vermilion** means you are on the air right now. It is reserved for that. `tx-past` is its dim record, for transmissions that have finished, and `tx-pressed` is the pale talk button while the channel opens.

Grey marks a signal below squelch. Focus rings are white, not a coded hue. State never relies on colour alone: every state also has text.

## Typography

One family, Archivo, self-hosted with its width axis. Large type runs narrow (62–75% width) and is tall rather than wide. The channel numeral is the only headline on the page. It scales with the screen height to fill the rail and stays far larger than anything else, so section headings stay small. Readings use tabular figures.

## Layout

The desk reads by column: the air on the left, the radio on the right, with Recordings and Repeat beneath. Rules are 1 px hairlines. There are no cards. On phones everything stacks in that order.

**The talk dock is fixed to the viewport edge and sized in advance.** Its button sits in the bottom-right corner on desks and along the bottom edge on phones, with the microphone control beside it and the transmit safeguard line always visible. It must keep the same position and size in every state, so status text, notices and scrolling can never move it. `tools/web_talk_button_check.mjs` enforces this.

## Elevation & Depth

Flat. No shadows, gradients as decoration, or layered surfaces. Depth appears only in the pulse field, where each line hides the ones behind it.

## Shapes

Square corners everywhere, including inputs and audio controls.

## Components

- **Pulse field:** stacked spectra, newest at the front, drawn from `AirHistory`. The front line takes the live hue and double weight. Rows taper at the top edge so that peaks are never cut.
- **Waveform lanes and air strip:** the same audio over 30 seconds and 3 minutes. A block with a recording is underlined and has a 44 px target.
- **Signal scale:** the meter and the squelch slider are one control on one scale.
- **Talk button:** never transforms or resizes. Pale when pressed, a real 500 ms fill while the channel opens, vermilion when talking, with a live voice trace and a bar for the 60-second limit.
- **Audio bar:** square Play, seek and time controls in place of the browser's own.

## Do's and Don'ts

- Do draw only real data. If there is nothing to show, show flat lines.
- Do keep vermilion for transmitting.
- Do add new controls as standard web controls.
- Don't add cards, rounded corners or shadows.
- Don't put a second large element next to the numeral.
- Don't let anything reflow or animate the talk button's box.
