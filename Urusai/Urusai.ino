#include <Arduino.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include "2HPico.h"

I2S DAC(OUTPUT);
Adafruit_NeoPixel strip(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

void setup() {
    Serial.begin(115200);

    // Initialise UI
    pinMode(BUTTON1, INPUT_PULLUP);
    pinMode(MUXCTL, OUTPUT);

    strip.begin();
    strip.setPixelColor(0, 0); 
    strip.show();

    // Init I2S
    DAC.setBCLK(BCLK);
    DAC.setDATA(I2S_DATA);
    DAC.setBitsPerSample(16);
    DAC.setBuffers(2, 64, 0); 
    DAC.setLSBJFormat();         
    DAC.begin(44100); 

    // Drain FIFO
    while(rp2040.fifo.available()) rp2040.fifo.pop();
}

void loop() {
    samplepots();
    uint16_t cv2 = sampleCV2();

    // Tone = Pot 3 (pot[2]) + CV2
    // Assuming CV2 gives roughly 2048 at 0V. Standard Eurorack CV.
    float tone = (float)pot[2] / 4095.0f;
    float cv_norm = ((float)cv2 - 2048.0f) / 2048.0f; 
    tone += cv_norm;
    if (tone < 0.0f) tone = 0.0f;
    if (tone > 1.0f) tone = 1.0f;

    // Noise type = Pot 1 (pot[0])
    // 7 types total: 0-6
    int noiseType = (pot[0] * 7) / 4096;
    if (noiseType > 6) noiseType = 6;

    uint32_t colors[7] = {
        WHITE,          // White
        0x1f081f,       // Pink (Pinkish)
        BLUE,           // Blue
        VIOLET,         // Violet
        YELLOW,         // Velvet
        RED,            // CMOS
        GREEN           // 8-bit
    };

    strip.setPixelColor(0, colors[noiseType]);
    strip.show();

    // Pack into uint32_t to push through FIFO
    // tone is 0 to 1.0 -> map to 0 to 65535
    uint32_t packedTone = (uint32_t)(tone * 65535.0f);
    uint32_t pushData = (noiseType << 16) | (packedTone & 0xFFFF);
    
    // Only push if changed to avoid filling FIFO
    static uint32_t lastPush = 0xFFFFFFFF; // force push on startup
    if (pushData != lastPush) {
        if (rp2040.fifo.push_nb(pushData)) {
            lastPush = pushData;
        }
    }

    delay(2);
}

// ============================================
// CORE 1: DSP
// ============================================
uint32_t lfsrStateCmos = 0xACE1u;
uint32_t lfsrState8Bit = 0xACE1u;
float cmosPhase = 0.f;
float eightBitPhase = 0.f;

float b0 = 0.f, b1_p = 0.f, b2 = 0.f, b3 = 0.f, b4 = 0.f, b5 = 0.f, b6 = 0.f;
float lastWhite = 0.f;
float lastPink = 0.f;

// fast PRNG
uint32_t rng_state = 123456789;
inline float f_random() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return ((float)rng_state * (1.0f / 4294967296.0f)) * 2.0f - 1.0f; // -1.0 to 1.0
}

float whiteNoiseValue = 0.f;
bool whiteGenerated = false;

inline float nextWhite() {
    if (!whiteGenerated) {
        whiteNoiseValue = f_random();
        whiteGenerated = true;
    }
    return whiteNoiseValue;
}

float pinkNoiseValue = 0.f;
bool pinkGenerated = false;

inline float nextPink() {
    if (!pinkGenerated) {
        float w = nextWhite();
        b0 = 0.99886f * b0 + w * 0.0555179f;
        b1_p = 0.99332f * b1_p + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f;
        b5 = -0.7616f * b5 - w * 0.0168980f;
        pinkNoiseValue = b0 + b1_p + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        pinkNoiseValue *= 0.11f;
        pinkGenerated = true;
    }
    return pinkNoiseValue;
}

inline float nextBlue() {
    float p = nextPink();
    float blue = p - lastPink;
    lastPink = p;
    return blue * 10.f;
}

inline float nextViolet() {
    float w = nextWhite();
    float violet = w - lastWhite;
    lastWhite = w;
    return violet * 0.707f;
}

inline float nextVelvet(float p) {
    if ((f_random() * 0.5f + 0.5f) < p) {
        return f_random() > 0.0f ? 1.0f : -1.0f;
    }
    return 0.f;
}

inline float nextCmos(float rate) {
    cmosPhase += rate * (1.0f/44100.f);
    if (cmosPhase >= 1.f) {
        cmosPhase -= 1.f;
        unsigned bitCmos = ((lfsrStateCmos >> 13) ^ (lfsrStateCmos >> 16)) & 1;
        lfsrStateCmos = (lfsrStateCmos << 1) | bitCmos;
    }
    return ((lfsrStateCmos >> 16) & 1) ? 1.f : -1.f;
}

inline float next8Bit(float rate) {
    eightBitPhase += rate * (1.0f/44100.f);
    if (eightBitPhase >= 1.f) {
        eightBitPhase -= 1.f;
        unsigned bit8 = ((lfsrState8Bit >> 13) ^ (lfsrState8Bit >> 14)) & 1;
        lfsrState8Bit = (lfsrState8Bit << 1) | bit8;
    }
    return ((lfsrState8Bit >> 14) & 1) ? 1.f : -1.f;
}

void setup1() {
    delay(1000); // allow core 0 to init
}

void loop1() {
    static int currentNoiseType = 0;
    static float character = 0.5f;
    
    static float lpCutoff = 1000.f;
    static float hpCutoff = 1000.f;
    static float gLp = 1.0f;
    static float gHp = 1.0f;
    static bool isHp = false;
    
    static float velvetProb = 0.f;
    static float cmosRate = 1000.f;
    static float eightBitRate = 1000.f;

    static float lpState = 0.f;
    static float hpState = 0.f;

    if (rp2040.fifo.available()) {
        uint32_t data = rp2040.fifo.pop();
        currentNoiseType = data >> 16;
        character = (float)(data & 0xFFFF) / 65535.f;

        // Recalculate coefficients
        float lp_char = character / 0.5f;
        if (lp_char > 1.0f) lp_char = 1.0f;
        lpCutoff = pow(10.f, 1.f + 3.3f * lp_char);
        
        float hp_char = (character - 0.5f) / 0.5f;
        if (hp_char < 0.0f) hp_char = 0.0f;
        hpCutoff = pow(10.f, 1.f + 3.3f * hp_char);

        gLp = lpCutoff * (1.0f/44100.f) * 3.14159f;
        if (gLp > 1.0f) gLp = 1.0f;
        
        gHp = hpCutoff * (1.0f/44100.f) * 3.14159f;
        if (gHp > 1.0f) gHp = 1.0f;

        isHp = character >= 0.5f;

        // Velvet rate: 10Hz to 10000Hz based on character
        float velvetRate = pow(10.f, 1.f + 3.f * character);
        velvetProb = velvetRate * (1.0f/44100.f);

        // CMOS rate: 1000Hz to ~63kHz (Nyquist is 22050, but equation says +1.8f) 
        cmosRate = pow(10.f, 3.f + 1.8f * character);

        // 8-bit rate: 10Hz to 10000Hz
        eightBitRate = pow(10.f, 1.f + 3.f * character);
    }

    whiteGenerated = false;
    pinkGenerated = false;

    float sample = 0.f;
    switch(currentNoiseType) {
        case 0: sample = nextWhite() * 5.0f; break;
        case 1: sample = nextPink() * 15.3f; break;
        case 2: sample = nextBlue() * 2.5f; break;
        case 3: sample = nextViolet() * 5.0f; break;
        case 4: sample = nextVelvet(velvetProb) * 11.0f; break;
        case 5: sample = nextCmos(cmosRate) * 2.88f; break;
        case 6: sample = next8Bit(eightBitRate) * 2.88f; break;
    }

    // Apply Filter for White, Pink, Blue, Violet
    // In original code, filtering was applied to White, Pink, Blue, Violet. 
    // Velvet, CMOS, 8-bit don't get filtering, the tone controls their rate.
    if (currentNoiseType <= 3) {
        lpState += gLp * (sample - lpState);
        hpState += gHp * (sample - hpState);
        if (isHp) {
            sample = sample - hpState;
        } else {
            sample = lpState;
        }
    }

    if (isnan(sample) || isinf(sample)) {
        sample = 0.0f;
    }

    // convert to 16-bit
    // VCV uses roughly -5.0 to 5.0 range for audio. Map this to -32768..32767
    int32_t outSample = (int32_t)(sample * 6553.4f); 
    if (outSample > 32767) outSample = 32767;
    if (outSample < -32768) outSample = -32768;

    DAC.write(outSample); 
    DAC.write(outSample); 
}
