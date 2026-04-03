#include <Arduino.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include "2HPico.h"

I2S DAC(OUTPUT);
Adafruit_NeoPixel strip(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

const bool DEBUG = false;

#define NOISE_WHITE 0
#define NOISE_PINK 1
#define NOISE_BLUE 2
#define NOISE_VIOLET 3
#define NOISE_VELVET 4
#define NOISE_CMOS 5
#define NOISE_8BIT 6

struct NoiseData {
    uint8_t noiseType;
    float tone;
};

// Parameters are shared across cores through SRAM
volatile NoiseData noiseData;

// Only update the parameters every PARAMETERUPDATE
uint32_t parameterTimer = 0;

void setup() {
    if (DEBUG) {
        Serial.begin(115200);

        // Record the start time
        unsigned long startTime = millis();

        // Wait for Serial OR for 3 seconds to pass
        while (!Serial && (millis() - startTime < 3000)) {
            ; // Do nothing, just wait
        }

        // This will print if connected, or do nothing safely if not
        Serial.println("RP2350 Debugging Started...");
    }

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

// Noise type is controlled by rotating pot 1
inline uint8_t getNoiseType(uint16_t pot1) {
    uint8_t noiseType = (pot1 * 7) / 1024;
    if (noiseType > 6) noiseType = 6;
    return noiseType;
}

inline float getTone(uint16_t pot3, uint16_t cv2) {
    float tone = (float)pot3 / 1023.0f; 
    // TODO: Update for CV when we have the rest of the code working
    // float cv_norm = ((float)cv2 - 512.0f) / 512.0f; 
    // tone += cv_norm;
    if (tone < 0.0f) tone = 0.0f;
    if (tone > 1.0f) tone = 1.0f;
    return tone;
}

uint32_t colors[7] = {
    WHITE,          // White
    0x1f081f,       // Pink (Pinkish)
    BLUE,           // Blue
    VIOLET,         // Violet
    RED,            // Velvet
    GREEN,          // CMOS
    YELLOW          // 8-bit
};

void loop() {
    static uint32_t parameterTimer = millis();
    if ((millis() - parameterTimer) > PARAMETERUPDATE) {
        parameterTimer = millis();

        // Do not update the the parameters too frequently
        samplepots();
        uint16_t cv2 = sampleCV2();

        // Noise type = Pot 1 (pot[0]): 7 types total: 0-6
        noiseData.noiseType = getNoiseType(pot[0]);
        strip.setPixelColor(0, colors[noiseData.noiseType]);
        strip.show();

        // Tone = Pot 3 (pot[2]) + CV2
        noiseData.tone = getTone(pot[2], cv2);

        if (DEBUG) {
            Serial.printf("Noise Type: %d (%d), Tone: %f (%d, %d)\n", noiseData.noiseType, pot[0], noiseData.tone, pot[2], cv2); 
        }
    }
}

// ============================================
// CORE 1: DSP
// ============================================
uint32_t lfsrStateCmos = 0xACE1u;
uint32_t lfsrState8Bit = 0xACE1u;
float cmosPhase = 0.f;
float eightBitPhase = 0.f;

// Q1.31 fixed-point pink filter accumulators
int32_t b0 = 0, b1_p = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
int32_t lastWhite = 0;
int32_t lastPink = 0;

// Q1.31 multiply: (a * b) >> 31, using 64-bit intermediate
static inline int32_t q31_mul(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * b) >> 31);
}

// xoshiro128** PRNG — much better spectral quality for audio noise
uint32_t rng_s[4] = {123456789, 362436069, 521288629, 88675123};

inline int32_t i_random() {
    uint32_t result = rng_s[1] * 5;
    result = (result << 7 | result >> 25) * 9;

    uint32_t t = rng_s[1] << 9;
    rng_s[2] ^= rng_s[0];
    rng_s[3] ^= rng_s[1];
    rng_s[1] ^= rng_s[2];
    rng_s[0] ^= rng_s[3];
    rng_s[2] ^= t;
    rng_s[3] = (rng_s[3] << 11) | (rng_s[3] >> 21);

    return (int32_t)result;
}

inline int32_t nextWhite() {
    return i_random();
}

// Paul Kellet's pink noise filter coefficients in Q1.31
// Feedback coefficients (multiply accumulator)
static const int32_t PK_FB0 = 2145663025;  // 0.99886 * 2^31
static const int32_t PK_FB1 = 2133763072;  // 0.99332 * 2^31
static const int32_t PK_FB2 = 2082504499;  // 0.96900 * 2^31
static const int32_t PK_FB3 = 1861025464;  // 0.86650 * 2^31
static const int32_t PK_FB4 = 1181116006;  // 0.55000 * 2^31
static const int32_t PK_FB5 = -1635778150; // -0.7616 * 2^31
// Input coefficients (multiply white noise)
static const int32_t PK_IN0 = 119246061;   // 0.0555179 * 2^31
static const int32_t PK_IN1 = 161249019;   // 0.0750759 * 2^31
static const int32_t PK_IN2 = 330469744;   // 0.1538520 * 2^31
static const int32_t PK_IN3 = 666871054;   // 0.3104856 * 2^31
static const int32_t PK_IN4 = 1144764160;  // 0.5329522 * 2^31
static const int32_t PK_IN5 = -36285818;   // -0.0168980 * 2^31
static const int32_t PK_IN6 = 248963605;   // 0.115926 * 2^31
static const int32_t PK_SUM = 1151448227;  // 0.5362 * 2^31
static const int32_t PK_GAIN = 236223201;  // 0.11 * 2^31

inline int32_t nextPink() {
    int32_t w = nextWhite();
    b0 = q31_mul(PK_FB0, b0) + q31_mul(PK_IN0, w);
    b1_p = q31_mul(PK_FB1, b1_p) + q31_mul(PK_IN1, w);
    b2 = q31_mul(PK_FB2, b2) + q31_mul(PK_IN2, w);
    b3 = q31_mul(PK_FB3, b3) + q31_mul(PK_IN3, w);
    b4 = q31_mul(PK_FB4, b4) + q31_mul(PK_IN4, w);
    b5 = q31_mul(PK_FB5, b5) + q31_mul(PK_IN5, w);
    // Sum all filter bands + direct path
    int32_t pink = (b0 >> 3) + (b1_p >> 3) + (b2 >> 3) + (b3 >> 3)
                 + (b4 >> 3) + (b5 >> 3) + (b6 >> 3) + q31_mul(PK_SUM, w >> 3);
    b6 = q31_mul(PK_IN6, w);
    return q31_mul(PK_GAIN, pink);
}

inline int32_t nextBlue() {
    int32_t p = nextPink();
    int32_t blue = p - lastPink;
    lastPink = p;
    return blue;  // differentiation already boosts highs
}

inline int32_t nextViolet() {
    int32_t w = nextWhite();
    int32_t violet = w - lastWhite;
    lastWhite = w;
    return violet >> 1;  // ~0.707 approximated as >>1 (0.5), keeps levels safe
}

inline int32_t nextVelvet(uint32_t thresh) {
    // thresh is pre-scaled probability in 0..UINT32_MAX range
    uint32_t r = (uint32_t)i_random();
    if (r < thresh) {
        return i_random() >= 0 ? INT32_MAX : INT32_MIN;
    }
    return 0;
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

    static float tone = 0.5f;
    
    // Filter coefficients in Q1.31 (computed from tone, updated each sample)
    static int32_t gLp_q31 = 0;
    static int32_t gHp_q31 = 0;
    static bool isHp = false;
    
    // Velvet threshold in uint32_t range
    static uint32_t velvetThresh = 0;
    static float cmosRate = 1000.f;
    static float eightBitRate = 1000.f;

    // LP/HP filter state in Q1.31
    static int32_t lpState = 0;
    static int32_t hpState = 0;

    tone = noiseData.tone;

    // Recalculate filter coefficients (float here is fine — runs once per sample,
    // not in a tight inner loop, and pow() is inherently float)
    float lp_tone = tone / 0.5f;
    if (lp_tone > 1.0f) lp_tone = 1.0f;
    float lpCutoff = pow(10.f, 1.f + 3.3f * lp_tone);
    
    float hp_tone = (tone - 0.5f) / 0.5f;
    if (hp_tone < 0.0f) hp_tone = 0.0f;
    float hpCutoff = pow(10.f, 1.f + 3.3f * hp_tone);

    float gLp_f = lpCutoff * (1.0f/44100.f) * 3.14159f;
    if (gLp_f > 1.0f) gLp_f = 1.0f;
    gLp_q31 = (int32_t)(gLp_f * 2147483647.0f);
    
    float gHp_f = hpCutoff * (1.0f/44100.f) * 3.14159f;
    if (gHp_f > 1.0f) gHp_f = 1.0f;
    gHp_q31 = (int32_t)(gHp_f * 2147483647.0f);

    isHp = tone >= 0.5f;

    int32_t outSample = 0;

    switch(noiseData.noiseType) {
        case NOISE_WHITE: outSample = nextWhite(); break;
        case NOISE_PINK:  outSample = nextPink(); break;
        case NOISE_BLUE:  outSample = nextBlue(); break;
        case NOISE_VIOLET: outSample = nextViolet(); break;
        case NOISE_VELVET: {
            // Velvet rate: 10Hz to 10000Hz based on character
            float velvetRate = pow(10.f, 1.f + 3.f * tone);
            velvetThresh = (uint32_t)(velvetRate * (1.0f/44100.f) * 4294967296.0f);
            outSample = nextVelvet(velvetThresh);
            break;
        }
        case NOISE_CMOS: {
            // CMOS rate: 1000Hz to ~63kHz
            cmosRate = pow(10.f, 3.f + 1.8f * tone);
            outSample = (int32_t)(nextCmos(cmosRate) * 2147483647.0f);
            break;
        }
        case NOISE_8BIT: {
            // 8-bit rate: 10Hz to 10000Hz
            eightBitRate = pow(10.f, 1.f + 3.f * tone);
            outSample = (int32_t)(next8Bit(eightBitRate) * 2147483647.0f);
            break;
        }
    }

    // Apply Q1.31 LP/HP filter for White, Pink, Blue, Violet
    // Velvet, CMOS, 8-bit don't get filtering — tone controls their rate.
    if (noiseData.noiseType <= NOISE_VIOLET) {
        lpState += q31_mul(gLp_q31, outSample - lpState);
        hpState += q31_mul(gHp_q31, outSample - hpState);
        if (isHp) {
            outSample = outSample - hpState;
        } else {
            outSample = lpState;
        }
    }

    // Convert Q1.31 to 16-bit DAC output: take upper 16 bits
    int16_t dacOut = (int16_t)(outSample >> 16);

    // Write twice because the DAC is stereo
    DAC.write(dacOut); 
    DAC.write(dacOut); 
}
