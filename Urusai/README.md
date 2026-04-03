# 2HPico Urusai

Urusai is a firmware sketch for the 2HPico hardware platform (RP2040 / RP2350). Adapted directly from the Ataraxic VCV Rack module, it packs seven distinct flavors of noise into a single 2HP Eurorack module.

Every noise output algorithm has been equalized to match the perceived level (RMS) of standard white noise, allowing you to freely sweep through different noise colors without needing to drastically adjust your mixer or VCA levels.

## Hardware Mapping

Since the 2HPico hardware provides a specialized I/O layout, Urusai is laid out as follows:

*   **Top Jack:** Unused
*   **Pot 1 (Top / Noise Select):** Sweeps sequentially through the 7 different noise types.
*   **Pot 2:** Unused
*   **Pot 3:** Unused
*   **Pot 4 (Tone):** Controls the overall tone or clock speed of the noise.
*   **Middle Jack (Tone CV):** Additive CV input for the Tone parameter.
*   **Bottom Jack:** Main Noise Output.

## Features

### Seven Selectable Noises
Use **Pot 1** to sweep through the underlying algorithms. The module's NeoPixel LED will change color to provide visual feedback:

1.  **White Noise (White LED):** Standard uniform random noise with equal power across all frequencies.
2.  **Pink Noise (Pinkish-Violet LED):** Filtered for a -3dB/octave slope. Equal power per octave, sounding balanced and natural.
3.  **Blue Noise (Blue LED):** Filtered for a +3dB/octave slope, emphasizing high frequencies without being overwhelmingly bright.
4.  **Violet Noise (Deep Violet LED):** Filtered for a +6dB/octave slope. Extremely bright and hissy.
5.  **Velvet Noise (Yellow LED):** Sparse, random impulses of audio mixed with silence. Creates unique textures perfect for exciting resonators or simulating vinyl crackle.
6.  **CMOS Noise (Red LED):** Classic shift-register (LFSR) style chunky digital noise. Raw, full-amplitude signals scaled for audio use.
7.  **8-Bit Noise (Green LED):** A slower-clocked variant of CMOS noise reminiscent of vintage 8-bit video game sound chips.

### Global Tone & Frequency Control
**Pot 4** and the **Middle Jack (CV)** simultaneously shape the spectral density of the active noise algorithm across two different methods depending on your selection:
*   **Analog Noises (White, Pink, Blue, Violet):** Acts as a DJ-style tilt filter. Ranges smoothly from a heavy lowpass filter to a brilliant highpass filter.
*   **Digital Noises (Velvet, CMOS, 8-Bit):** Directly alters the fundamental clock rate and density of the pseudo-random grain triggers over an immense range (from clicky 10Hz rumbles to fully ultrasonic shift-register whine).

## Compilation & Installation

1. Select your target board corresponding to your 2HPico (e.g. Raspberry Pi Pico setup).
2. Load the `Urusai.ino` sketch using the Arduino IDE.
3. Compile and upload to your hardware. 
4. Assure that your 2HPico jumpers are configured correctly on the back (Middle jack as Input, Bottom jack as Output).
