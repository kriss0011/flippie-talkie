# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Users

Flipper Zero owners who want to use the device as a PMR446 radio. Two equally important situations:

- At a desk on a laptop, experimenting, watching what the radio does and reviewing recordings.
- Out and about with a phone as the handset, using it one-handed next to the Flipper.

## Product Purpose

Listen and talk through a Flipper Zero from a browser, over Bluetooth or USB. Success means connecting without help, knowing at a glance what the radio is doing, and talking with confidence that the channel is open.

## Positioning

An ordinary radio hides its signal. This one streams real received audio and microphone audio through the browser, so the page can show exactly what is on the air, including noise. Audio goes directly between the browser and the Flipper with no server.

## Operating Context

The Flipper runs the Flippie Talkie app in Browser link mode. The page must stay in front while in use. Talking is half-duplex push-to-talk. Reception is experimental and noisy by nature.

## Capabilities and Constraints

- 16 PMR446 channels, Bluetooth or USB, per-channel squelch, browser recordings, repeated recorded messages, help and link details. All stay in the product.
- Data available live: received audio at 3.2 kHz (Bluetooth) or 4 kHz (USB), microphone audio at 6 kHz, and signal strength in status packets about once a second during reception. Visualisations show only this real data.
- Static files with no build step and no dependencies. Web Bluetooth and Web Serial, so Chrome, Edge for USB, or Bluefy on iOS.
- The radio, audio and wire protocol must not change for design reasons.

## Brand Commitments

Name: Flippie Talkie. Plain, direct wording. No use of Flipper Devices branding or its dolphin mascot, because the project is independent.

## Evidence on Hand

Real Flipper screen captures in `docs/images/`. No measured range or audio quality claims exist, and none may be implied.

## Product Principles

- Show what is really on the air, never a decorative imitation of it.
- The talk control is always obvious and within reach.
- Say honestly when something is experimental or noisy.
- Transmitting is a deliberate act: state is unmistakable and safeguards stay visible.

## Accessibility & Inclusion

Keyboard and screen-reader access to all controls, reduced-motion support, and state never carried by colour or animation alone. Visualisations are supplementary to text status.
