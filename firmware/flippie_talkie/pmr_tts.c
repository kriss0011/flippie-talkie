#include "pmr_tts.h"
#include "pmr_tts_rules.h"
#include <math.h>
#include <string.h>

// Formants this close to Nyquist would add false treble, so the voice runs at
// twice the output rate and is decimated.
#define SYNTH_RATE    12000U
#define FRAME_SAMPLES 60U // 5 ms
#define FRAME_MS      5U
#define PI_F          3.14159265f

#define VOICE_GAIN      1.0f
#define ASPIRATION_GAIN 0.15f
#define FRICATION_GAIN  0.3f
#define OUTPUT_GAIN     1.0f
#define NOTCH_RADIUS    0.94f
#define NOTCH_BASE_HZ   500.0f

typedef enum {
    ClassVowel,
    ClassLiquid,
    ClassNasal,
    ClassFricative,
    ClassAspirate,
    ClassStop,
    ClassAffricate,
    ClassPause,
} PhoneClass;

// Segment parameters. Formants and bandwidths blend slowly; levels quickly.
enum {
    F1,
    F2,
    F3,
    B1,
    B2,
    B3,
    AV,
    AH,
    AF,
    FF,
    FB,
    NZ
};

enum {
    FlagStress = 1,
    FlagWordEnd = 2,
    FlagFinal = 4, // last syllable of a phrase
    FlagQuestion = 8,
    FlagContinue = 16,
};

typedef struct {
    char symbol;
    uint8_t kind;
    uint16_t f1, f2, f3; // target, or start of a diphthong
    uint16_t e1, e2, e3; // end of a diphthong, otherwise 0
    uint8_t inherent, minimum; // duration, in units of 2 ms
    uint8_t voice, frication, aspiration; // percent
    uint16_t noise_hz, noise_width;
} Phone;

// Formant targets and durations follow Klatt's published adult male values.
static const Phone phones[] = {
    {'i', ClassVowel, 310, 2020, 2960, 0, 0, 0, 78, 28, 100, 0, 0, 0, 0},
    {'I', ClassVowel, 400, 1800, 2570, 0, 0, 0, 68, 20, 100, 0, 0, 0, 0},
    {'e', ClassVowel, 480, 1720, 2520, 330, 2020, 2600, 95, 50, 100, 0, 0, 0, 0},
    {'E', ClassVowel, 530, 1680, 2500, 0, 0, 0, 75, 35, 100, 0, 0, 0, 0},
    {'a', ClassVowel, 620, 1660, 2430, 0, 0, 0, 115, 40, 100, 0, 0, 0, 0},
    {'A', ClassVowel, 700, 1220, 2600, 0, 0, 0, 120, 50, 100, 0, 0, 0, 0},
    {'O', ClassVowel, 600, 990, 2570, 0, 0, 0, 120, 50, 100, 0, 0, 0, 0},
    {'o', ClassVowel, 540, 1100, 2300, 450, 900, 2300, 110, 40, 100, 0, 0, 0, 0},
    {'U', ClassVowel, 450, 1100, 2350, 0, 0, 0, 80, 30, 100, 0, 0, 0, 0},
    {'u', ClassVowel, 350, 1250, 2200, 320, 900, 2200, 105, 35, 100, 0, 0, 0, 0},
    {'V', ClassVowel, 620, 1220, 2550, 0, 0, 0, 70, 30, 100, 0, 0, 0, 0},
    {'@', ClassVowel, 500, 1400, 2300, 0, 0, 0, 60, 30, 85, 0, 0, 0, 0},
    {'R', ClassVowel, 470, 1270, 1540, 0, 0, 0, 90, 40, 100, 0, 0, 0, 0},
    {'Y', ClassVowel, 660, 1200, 2550, 400, 1880, 2500, 125, 75, 100, 0, 0, 0, 0},
    {'W', ClassVowel, 640, 1230, 2550, 420, 940, 2350, 130, 50, 100, 0, 0, 0, 0},
    {'Q', ClassVowel, 550, 960, 2400, 360, 1820, 2450, 140, 75, 100, 0, 0, 0, 0},
    {'w', ClassLiquid, 290, 610, 2150, 0, 0, 0, 40, 30, 75, 0, 0, 0, 0},
    {'j', ClassLiquid, 260, 2070, 2900, 0, 0, 0, 40, 20, 75, 0, 0, 0, 0},
    {'r', ClassLiquid, 310, 1060, 1380, 0, 0, 0, 40, 15, 75, 0, 0, 0, 0},
    {'l', ClassLiquid, 310, 1050, 2880, 0, 0, 0, 40, 20, 75, 0, 0, 0, 0},
    {'m', ClassNasal, 280, 900, 2200, 0, 0, 0, 35, 25, 60, 0, 0, 0, 0},
    {'n', ClassNasal, 280, 1700, 2600, 0, 0, 0, 30, 15, 60, 0, 0, 0, 0},
    {'N', ClassNasal, 280, 2300, 2750, 0, 0, 0, 48, 30, 60, 0, 0, 0, 0},
    {'f', ClassFricative, 340, 1100, 2080, 0, 0, 0, 50, 40, 0, 45, 0, 1800, 1800},
    {'v', ClassFricative, 220, 1100, 2080, 0, 0, 0, 30, 20, 45, 25, 0, 1800, 1800},
    {'T', ClassFricative, 320, 1290, 2540, 0, 0, 0, 45, 30, 0, 40, 0, 2500, 1500},
    {'D', ClassFricative, 270, 1290, 2540, 0, 0, 0, 25, 15, 50, 20, 0, 2500, 1500},
    {'s', ClassFricative, 320, 1390, 2530, 0, 0, 0, 52, 30, 0, 100, 0, 2750, 500},
    {'z', ClassFricative, 240, 1390, 2530, 0, 0, 0, 38, 20, 35, 70, 0, 2750, 500},
    {'S', ClassFricative, 300, 1840, 2750, 0, 0, 0, 52, 40, 0, 100, 0, 2100, 800},
    {'Z', ClassFricative, 240, 1840, 2750, 0, 0, 0, 35, 20, 35, 70, 0, 2100, 800},
    {'h', ClassAspirate, 500, 1500, 2500, 0, 0, 0, 40, 10, 0, 0, 60, 0, 0},
    {'p', ClassStop, 400, 1100, 2150, 0, 0, 0, 45, 25, 0, 70, 50, 1000, 1500},
    {'t', ClassStop, 400, 1600, 2600, 0, 0, 0, 38, 25, 0, 100, 50, 2700, 800},
    {'k', ClassStop, 300, 1990, 2850, 0, 0, 0, 40, 30, 0, 90, 50, 1900, 800},
    {'b', ClassStop, 200, 1100, 2150, 0, 0, 0, 42, 30, 12, 40, 0, 1000, 1500},
    {'d', ClassStop, 200, 1600, 2600, 0, 0, 0, 38, 25, 12, 50, 0, 2700, 800},
    {'g', ClassStop, 200, 1990, 2850, 0, 0, 0, 40, 30, 12, 50, 0, 1900, 800},
    {'C', ClassAffricate, 350, 1800, 2750, 0, 0, 0, 35, 25, 0, 100, 0, 2100, 800},
    {'J', ClassAffricate, 260, 1800, 2750, 0, 0, 0, 35, 25, 25, 70, 0, 2100, 800},
    {' ', ClassPause, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};
#define PHONE_COUNT  (sizeof(phones) / sizeof(phones[0]))
#define PHONE_PAUSE  (PHONE_COUNT - 1U)
#define PAUSE_COMMA  40U // in frames
#define PAUSE_PERIOD 80U
#define WORD_MAX     32U

static uint32_t phone_code(char symbol) {
    for(uint32_t i = 0; i < PHONE_COUNT; i++)
        if(phones[i].symbol == symbol) return i;
    return PHONE_COUNT;
}
static bool phone_is_vowel(uint32_t code) {
    return code < PHONE_COUNT && phones[code].kind == ClassVowel;
}
static bool upper(char c) {
    return c >= 'A' && c <= 'Z';
}
static bool letter_vowel(char c) {
    return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'Y';
}
static bool letter_consonant(char c) {
    return upper(c) && !letter_vowel(c);
}
static bool letter_in(char c, const char* set) {
    return c && strchr(set, c);
}

// Letters of the word live at w[1..]; w[0] and the end are boundary spaces.
static bool match_left(const char* w, int32_t at, const char* pattern, size_t size) {
    for(int32_t i = (int32_t)size - 1; i >= 0; i--) {
        char p = pattern[i];
        if(p == ' ' || upper(p)) {
            if(at < 0 || w[at] != p) return false;
            at--;
        } else if(p == '#') {
            if(at < 0 || !letter_vowel(w[at])) return false;
            while(at >= 0 && letter_vowel(w[at]))
                at--;
        } else if(p == ':') {
            // ":" runs first here, so "^:" means one or more consonants.
            bool needed = i > 0 && pattern[i - 1] == '^';
            if(needed && (at < 0 || !letter_consonant(w[at]))) return false;
            while(at >= 0 && letter_consonant(w[at]))
                at--;
            if(needed) i--;
        } else if(p == '^') {
            if(at < 0 || !letter_consonant(w[at])) return false;
            at--;
        } else if(p == '.') {
            if(at < 0 || !letter_in(w[at], "BDVGJLMNRWZ")) return false;
            at--;
        } else if(p == '+') {
            if(at < 0 || !letter_in(w[at], "EIY")) return false;
            at--;
        } else if(p == '@') {
            if(at < 0) return false;
            if(w[at] == 'H' && at > 0 && letter_in(w[at - 1], "TCS"))
                at -= 2;
            else if(letter_in(w[at], "TSRDLZNJ"))
                at--;
            else
                return false;
        } else
            return false;
    }
    return true;
}
static bool match_right(const char* w, uint32_t at, const char* pattern, size_t size) {
    for(size_t i = 0; i < size; i++) {
        char p = pattern[i], c = w[at];
        if(p == ' ' || upper(p)) {
            if(c != p) return false;
            at++;
        } else if(p == '#') {
            if(!letter_vowel(c)) return false;
            while(letter_vowel(w[at]))
                at++;
        } else if(p == ':') {
            while(letter_consonant(w[at]))
                at++;
        } else if(p == '^') {
            if(!letter_consonant(c)) return false;
            at++;
        } else if(p == '.') {
            if(!letter_in(c, "BDVGJLMNRWZ")) return false;
            at++;
        } else if(p == '+') {
            if(!letter_in(c, "EIY")) return false;
            at++;
        } else if(p == '%') {
            if(c == 'E') {
                at++;
                if(w[at] == 'L' && w[at + 1] == 'Y')
                    at += 2;
                else if(letter_in(w[at], "RSD"))
                    at++;
            } else if(!memcmp(w + at, "ING", 3))
                at += 3;
            else
                return false;
        } else
            return false;
    }
    return true;
}

// Finds the rule for the letter at w[at]. Returns the matched length.
static uint32_t rule_apply(const char* w, uint32_t at, char* out, size_t size) {
    const char* line = pmr_tts_rules[w[at] - 'A'];
    while(*line) {
        const char* open = strchr(line, '[');
        const char* close = strchr(open, ']');
        const char* equals = strchr(close, '=');
        const char* end = strchr(equals, '\n');
        size_t length = (size_t)(close - open - 1);
        if(!memcmp(open + 1, w + at, length) &&
           match_left(w, (int32_t)at - 1, line, (size_t)(open - line)) &&
           match_right(w, at + (uint32_t)length, close + 1, (size_t)(equals - close - 1))) {
            size_t n = (size_t)(end - equals - 1);
            if(n >= size) n = size - 1;
            memcpy(out, equals + 1, n);
            out[n] = 0;
            return (uint32_t)length;
        }
        line = end + 1;
    }
    out[0] = 0;
    return 1;
}

static const char* table_find(const char* table, const char* word, size_t size) {
    while(*table) {
        const char* equals = strchr(table + 1, '=');
        const char* end = strchr(equals, '\n');
        if((size_t)(equals - table) == size && !memcmp(table, word, size)) return equals + 1;
        table = end + 1;
    }
    return NULL;
}

static void phones_add(PmrTts* t, uint32_t code, uint8_t flags, uint8_t position) {
    if(t->count >= PMR_TTS_PHONES_MAX) return;
    t->phones[t->count++] = (PmrTtsPhone){code, flags, position};
}

// Appends a word from phoneme text, deciding stress when it is not marked.
static bool word_add(PmrTts* t, const char* spelling, size_t size) {
    bool marked = false, weak = false;
    uint32_t vowels = 0, first = t->count;
    if(t->count + size + 1 >= PMR_TTS_PHONES_MAX) return false;
    int32_t stress = -1;
    for(size_t i = 0; i < size; i++) {
        char c = spelling[i];
        if(c == '~')
            weak = true;
        else if(c == '\'')
            marked = true;
        else {
            uint32_t code = phone_code(c);
            if(code >= PHONE_COUNT) continue;
            if(phone_is_vowel(code)) {
                vowels++;
                if(marked) {
                    stress = (int32_t)t->count;
                    marked = false;
                }
            }
            phones_add(t, code, 0, 0);
        }
    }
    if(t->count == first) return true;
    // "-tion" and "-sion" draw the stress to the syllable before them.
    if(stress < 0 && !weak && t->count - first > 3 &&
       phones[t->phones[t->count - 1].code].symbol == 'n' &&
       phones[t->phones[t->count - 2].code].symbol == '@' &&
       (phones[t->phones[t->count - 3].code].symbol == 'S' ||
        phones[t->phones[t->count - 3].code].symbol == 'Z')) {
        for(uint32_t i = t->count - 4; i + 1 > first && stress < 0; i--)
            if(phone_is_vowel(t->phones[i].code)) stress = (int32_t)i;
    }
    if(stress < 0 && !weak) {
        // Stress the first syllable unless it is a weak prefix such as "be-".
        for(uint32_t i = first, seen = 0; i < t->count; i++) {
            uint32_t code = t->phones[i].code;
            if(!phone_is_vowel(code)) continue;
            seen++;
            bool reduced = phones[code].symbol == '@' || phones[code].symbol == 'I';
            if(seen == 1 && reduced && vowels > 1) continue;
            stress = (int32_t)i;
            break;
        }
    }
    if(stress >= 0) t->phones[stress].flags |= FlagStress;
    t->phones[t->count - 1].flags |= FlagWordEnd;
    return true;
}

static bool word_speak(PmrTts* t, const char* word, size_t size) {
    bool has_vowel = false;
    for(size_t i = 0; i < size; i++)
        has_vowel = has_vowel || letter_vowel(word[i]);
    const char* known = table_find(pmr_tts_words, word, size);
    if(known) return word_add(t, known, (size_t)(strchr(known, '\n') - known));
    if(size == 1 || !has_vowel) {
        for(size_t i = 0; i < size; i++) {
            const char* name = pmr_tts_letters[word[i] - 'A'];
            if(!word_add(t, name, strlen(name))) return false;
        }
        return true;
    }
    char padded[WORD_MAX + 3], spelling[WORD_MAX * 3], part[16];
    size_t length = 0;
    padded[0] = ' ';
    memcpy(padded + 1, word, size);
    padded[size + 1] = ' ';
    padded[size + 2] = 0;
    for(uint32_t at = 1; at <= size;) {
        at += rule_apply(padded, at, part, sizeof(part));
        size_t n = strlen(part);
        if(length + n >= sizeof(spelling)) break;
        memcpy(spelling + length, part, n);
        length += n;
    }
    return word_add(t, spelling, length);
}

static void pause_add(PmrTts* t, uint32_t frames, uint8_t flags) {
    // Runs of marks, as in "wait...", need no extra silence.
    if(!t->count || t->phones[t->count - 1].code == PHONE_PAUSE) return;
    uint32_t last = t->count;
    while(last && t->phones[last - 1].code != PHONE_PAUSE) {
        last--;
        if(phone_is_vowel(t->phones[last].code)) {
            t->phones[last].flags |= FlagFinal | flags;
            break;
        }
    }
    phones_add(t, PHONE_PAUSE, 0, (uint8_t)frames);
}

// Each phrase falls in pitch from start to end.
static void phrases_mark(PmrTts* t) {
    uint32_t start = 0;
    for(uint32_t i = 0; i <= t->count; i++) {
        if(i != t->count && t->phones[i].code != PHONE_PAUSE) continue;
        uint32_t size = i - start;
        for(uint32_t k = start; k < i; k++)
            t->phones[k].position = (uint8_t)((k - start) * 255U / (size ? size : 1U));
        start = i + 1;
    }
}

bool pmr_tts_load(PmrTts* t, const char* text, uint32_t speed_percent, uint32_t pitch_hz) {
    t->count = 0;
    t->speed_percent = speed_percent < 50 ? 50 : speed_percent > 200 ? 200 : speed_percent;
    t->pitch_hz = pitch_hz < 60 ? 60 : pitch_hz > 240 ? 240 : pitch_hz;
    char word[WORD_MAX + 1];
    size_t size = 0;
    bool full = false;
    for(size_t i = 0; !full; i++) {
        char c = text[i];
        if(c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if(upper(c)) {
            if(size < WORD_MAX) word[size++] = c;
            continue;
        }
        if(c == '\'') continue;
        if(size) {
            full = !word_speak(t, word, size);
            size = 0;
            if(full) break;
        }
        if(!c) break;
        if(c >= '0' && c <= '9') {
            const char* digit = pmr_tts_digits[c - '0'];
            full = !word_add(t, digit, strlen(digit));
        } else if(c == '.' || c == '!')
            pause_add(t, PAUSE_PERIOD, 0);
        else if(c == '?')
            pause_add(t, PAUSE_PERIOD, FlagQuestion);
        else if(c == ',' || c == ';' || c == ':')
            pause_add(t, PAUSE_COMMA, FlagContinue);
        else {
            char symbol[2] = {c, 0};
            const char* name = table_find(pmr_tts_symbols, symbol, 1);
            if(name) full = !word_add(t, name, (size_t)(strchr(name, '\n') - name));
        }
    }
    if(!t->count) return false;
    pause_add(t, PAUSE_PERIOD, 0);
    phrases_mark(t);
    pmr_tts_rewind(t);
    return true;
}

static uint32_t frames_for_ms(const PmrTts* t, uint32_t ms) {
    uint32_t frames = ms * 100U / t->speed_percent / FRAME_MS;
    return frames ? frames : 1;
}
static uint32_t phone_ms(const PmrTts* t, uint32_t index) {
    const PmrTtsPhone* item = &t->phones[index];
    const Phone* p = &phones[item->code];
    uint32_t inherent = p->inherent * 2U, minimum = p->minimum * 2U, percent = 100;
    if(p->kind == ClassVowel) {
        if(!(item->flags & FlagStress)) percent = 55;
        if(item->flags & FlagFinal) percent = percent * 150U / 100U;
        // Vowels shorten before voiceless consonants.
        if(index + 1 < t->count) {
            uint32_t next = t->phones[index + 1].code;
            char s = phones[next].symbol;
            if(s == 'p' || s == 't' || s == 'k' || s == 'f' || s == 's' || s == 'S' || s == 'T')
                percent = percent * 75U / 100U;
        }
    } else if(!(item->flags & FlagWordEnd) && index && !phone_is_vowel(t->phones[index - 1].code))
        percent = 80; // inside a consonant cluster
    return minimum + (inherent - minimum) * percent / 100U;
}

static void targets_set(PmrTtsSegment* s, float f1, float f2, float f3) {
    s->p[F1] = f1;
    s->p[F2] = f2;
    s->p[F3] = f3;
}

// Next vowel-like target, for phonemes that take their shape from what follows.
static const Phone* phone_following(const PmrTts* t, uint32_t index) {
    for(uint32_t i = index + 1; i < t->count && i <= index + 2; i++) {
        const Phone* p = &phones[t->phones[i].code];
        if(p->kind == ClassVowel || p->kind == ClassLiquid) return p;
        if(p->kind == ClassPause) break;
    }
    return NULL;
}

static float pitch_at(const PmrTts* t, const PmrTtsPhone* item, float through) {
    float base = (float)t->pitch_hz;
    float position = ((float)item->position + through * 12.0f) / 255.0f;
    float f0 = base * (1.14f - 0.26f * position);
    if(item->flags & FlagStress) f0 *= 1.0f + 0.14f * (1.0f - through * 0.6f);
    if(item->flags & FlagFinal) {
        if(item->flags & FlagQuestion)
            f0 *= 1.0f + 0.45f * through;
        else if(item->flags & FlagContinue)
            f0 *= 1.0f + 0.12f * through;
        else
            f0 *= 1.0f - 0.16f * through;
    }
    return f0;
}

// Expands the current phoneme into its next segment. Stops and diphthongs need
// several; the return value is true when this was the last for that phoneme.
static bool segment_fill(const PmrTts* t, uint32_t index, uint32_t sub, PmrTtsSegment* s) {
    const PmrTtsPhone* item = &t->phones[index];
    const Phone* p = &phones[item->code];
    const Phone* after = phone_following(t, index);
    memset(s, 0, sizeof(*s));
    s->p[B1] = 70;
    s->p[B2] = 100;
    s->p[B3] = 160;
    s->f0_start = s->f0_end = pitch_at(t, item, 0);
    s->formant_blend = 8;
    s->level_blend = 3;
    uint32_t ms = phone_ms(t, index);
    bool last = true;
    targets_set(s, p->f1, p->f2, p->f3);
    switch(p->kind) {
    case ClassPause:
        s->frames = item->position ? item->position : 1;
        s->frames = (uint16_t)(s->frames * 100U / t->speed_percent);
        s->p[F1] = 0; // keeps the previous shape; see segment_inherit
        s->level_blend = 6;
        s->f0_start = s->f0_end = (float)t->pitch_hz * 0.9f;
        break;
    case ClassVowel: {
        s->p[AV] = (float)p->voice / 100.0f;
        s->f0_end = pitch_at(t, item, 1.0f);
        if(p->e1) {
            uint32_t first = ms * 55U / 100U;
            if(sub == 0) {
                s->frames = frames_for_ms(t, first);
                last = false;
            } else {
                s->frames = frames_for_ms(t, ms - first);
                targets_set(s, p->e1, p->e2, p->e3);
                s->formant_blend = (uint8_t)(s->frames * 2U);
            }
        } else
            s->frames = frames_for_ms(t, ms);
        break;
    }
    case ClassLiquid:
        s->p[AV] = (float)p->voice / 100.0f;
        s->frames = frames_for_ms(t, ms);
        s->p[B1] = 90;
        break;
    case ClassNasal:
        s->p[AV] = (float)p->voice / 100.0f;
        s->p[B1] = 60;
        s->p[B2] = 320;
        s->p[B3] = 420;
        s->p[NZ] = 1.0f;
        s->frames = frames_for_ms(t, ms);
        s->formant_blend = 7;
        s->level_blend = 1;
        break;
    case ClassFricative:
        s->p[AV] = (float)p->voice / 100.0f;
        s->p[AF] = (float)p->frication / 100.0f;
        s->p[FF] = p->noise_hz;
        s->p[FB] = p->noise_width;
        s->frames = frames_for_ms(t, ms);
        s->formant_blend = 6;
        break;
    case ClassAspirate:
        if(after) targets_set(s, after->f1, after->f2, after->f3);
        s->p[AH] = (float)p->aspiration / 100.0f;
        s->p[B1] = 200;
        s->frames = frames_for_ms(t, ms);
        s->formant_blend = 2;
        break;
    case ClassStop:
    case ClassAffricate: {
        bool velar = p->symbol == 'k' || p->symbol == 'g';
        // Velars sit further forward before front vowels.
        if(velar && after && after->f2 < 1500) s->p[F2] = 1500;
        if(velar && after && after->f2 >= 1500) s->p[F2] = 2300;
        bool voiced = p->voice != 0, aspirate = p->aspiration != 0 && after;
        // English /p t k/ have no puff of air after /s/, as in "speak".
        bool after_s = index && phones[t->phones[index - 1].code].symbol == 's';
        if(after_s) aspirate = false;
        if(sub == 0) {
            s->p[AV] = (float)p->voice / 100.0f;
            s->frames = frames_for_ms(t, ms);
            if(after_s) s->frames = frames_for_ms(t, ms * 6U / 10U);
            s->formant_blend = 10;
            s->level_blend = 2;
            last = false;
        } else if(sub == 1) {
            s->p[AV] = voiced ? (float)p->voice / 100.0f : 0;
            s->p[AF] = (float)p->frication / 100.0f;
            s->p[FF] = p->noise_hz;
            s->p[FB] = p->noise_width;
            s->level_blend = 1;
            s->formant_blend = 2;
            if(p->kind == ClassAffricate)
                s->frames = frames_for_ms(t, 70);
            else {
                s->frames = voiced ? 2 : 3;
                // Final stops release weakly.
                if(item->flags & FlagWordEnd && !after) s->p[AF] *= 0.5f;
                last = !aspirate;
            }
        } else {
            s->p[AH] = (float)p->aspiration / 100.0f;
            s->p[B1] = 200;
            s->frames = frames_for_ms(t, 35);
            s->level_blend = 1;
            s->formant_blend = 2;
        }
        break;
    }
    default:
        break;
    }
    return last;
}

static bool segment_next(PmrTts* t, PmrTtsSegment* s) {
    if(t->phone >= t->count) return false;
    bool last = segment_fill(t, t->phone, t->sub, s);
    if(last) {
        t->phone++;
        t->sub = 0;
    } else
        t->sub++;
    return true;
}
// Silence has no shape of its own: it holds the shape next to it.
static void segment_inherit(PmrTtsSegment* s, const PmrTtsSegment* from) {
    if(s->p[F1] != 0) return;
    s->p[F1] = from->p[F1] ? from->p[F1] : 500;
    s->p[F2] = from->p[F2] ? from->p[F2] : 1500;
    s->p[F3] = from->p[F3] ? from->p[F3] : 2500;
    s->p[B1] = from->p[B1];
    s->p[B2] = from->p[B2];
    s->p[B3] = from->p[B3];
}

void pmr_tts_rewind(PmrTts* t) {
    uint32_t speed = t->speed_percent, pitch = t->pitch_hz, count = t->count;
    size_t skip = offsetof(PmrTts, phone);
    memset((uint8_t*)t + skip, 0, sizeof(*t) - skip);
    t->count = count;
    t->speed_percent = speed;
    t->pitch_hz = pitch;
    t->random = 0x2F6E2B1U;
    t->phase = 0;
    segment_next(t, &t->current);
    if(!segment_next(t, &t->next)) t->next = t->current;
    t->previous = t->current;
    t->previous.p[AV] = t->previous.p[AH] = t->previous.p[AF] = 0;
    segment_inherit(&t->current, &t->next);
    segment_inherit(&t->previous, &t->current);
    segment_inherit(&t->next, &t->current);
}

static void resonator_set(PmrTtsResonator* r, float hz, float width) {
    float radius = expf(-PI_F * width / (float)SYNTH_RATE);
    r->c = -radius * radius;
    r->b = 2.0f * radius * cosf(2.0f * PI_F * hz / (float)SYNTH_RATE);
    r->a = 1.0f - r->b - r->c;
}
static float resonator_run(PmrTtsResonator* r, float x) {
    float y = r->a * x + r->b * r->y1 + r->c * r->y2;
    r->y2 = r->y1;
    r->y1 = y;
    return y;
}

// Crosses each boundary symmetrically. Both sides use the same width, so a
// parameter meets at the midpoint and never jumps at the boundary.
static float blend(
    float before,
    float value,
    float after,
    uint32_t frame,
    uint32_t frames,
    uint32_t width_in,
    uint32_t width_out) {
    float at = (float)frame + 0.5f, left = (float)frames - at;
    if(at < (float)width_in * 0.5f)
        return before + (value - before) * (0.5f + at / (float)width_in);
    if(left < (float)width_out * 0.5f)
        return value + (after - value) * (0.5f - left / (float)width_out);
    return value;
}
static uint32_t width_limit(uint32_t width, uint32_t a, uint32_t b) {
    uint32_t limit = a < b ? a : b;
    return width > limit ? limit : width ? width : 1;
}

static bool frame_next(PmrTts* t) {
    if(t->segment_frame >= t->current.frames) {
        if(t->finished) return false;
        t->previous = t->current;
        t->current = t->next;
        if(!segment_next(t, &t->next)) {
            memset(&t->next, 0, sizeof(t->next));
            t->next.frames = 1;
            t->next.formant_blend = t->next.level_blend = 1;
            t->finished = true;
        }
        segment_inherit(&t->current, &t->previous);
        segment_inherit(&t->next, &t->current);
        t->segment_frame = 0;
    }
    const PmrTtsSegment* a = &t->previous;
    const PmrTtsSegment* s = &t->current;
    const PmrTtsSegment* b = &t->next;
    uint32_t n = t->segment_frame, count = s->frames;
    uint32_t form_in = width_limit(s->formant_blend, a->frames, count);
    uint32_t form_out = width_limit(b->formant_blend, count, b->frames);
    uint32_t level_in = width_limit(s->level_blend, a->frames, count);
    uint32_t level_out = width_limit(b->level_blend, count, b->frames);
    float p[PMR_TTS_PARAMS];
    for(uint32_t i = 0; i < PMR_TTS_PARAMS; i++) {
        bool level = i >= AV;
        if(i == FF || i == FB)
            p[i] = s->p[i];
        else
            p[i] = blend(
                a->p[i],
                s->p[i],
                b->p[i],
                n,
                count,
                level ? level_in : form_in,
                level ? level_out : form_out);
    }
    for(uint32_t i = 0; i < 3; i++)
        resonator_set(&t->formant[i], p[F1 + i], p[B1 + i]);
    if(p[FF] > 0) {
        float radius = expf(-PI_F * p[FB] / (float)SYNTH_RATE);
        t->noise.c = -radius * radius;
        t->noise.b = 2.0f * radius * cosf(2.0f * PI_F * p[FF] / (float)SYNTH_RATE);
        t->noise.a = (1.0f - radius * radius) * 0.5f;
    }
    t->notch_depth = p[NZ];
    t->notch_c = 2.0f * NOTCH_RADIUS *
                 cosf(2.0f * PI_F * (p[F2] * 0.5f + NOTCH_BASE_HZ) / (float)SYNTH_RATE);
    float through = count > 1 ? (float)n / (float)(count - 1) : 0;
    t->f0 = s->f0_start + (s->f0_end - s->f0_start) * through;
    t->voice_step = (p[AV] - t->voice) / (float)FRAME_SAMPLES;
    t->aspiration_step = (p[AH] - t->aspiration) / (float)FRAME_SAMPLES;
    t->frication_step = (p[AF] - t->frication) / (float)FRAME_SAMPLES;
    t->frame_samples = FRAME_SAMPLES;
    t->segment_frame++;
    return true;
}

// Glottal flow derivative, averaged over two sub-samples to limit aliasing.
static float glottal_at(float phase) {
    const float open = 0.62f;
    if(phase >= open) return 0;
    float x = phase / open;
    return (2.0f - 3.0f * x) * x;
}
static float voice_next(PmrTts* t) {
    float step = t->f0 / (float)SYNTH_RATE;
    float pulse = glottal_at(t->phase);
    t->phase += step * 0.5f;
    if(t->phase >= 1.0f) t->phase -= 1.0f;
    pulse = (pulse + glottal_at(t->phase)) * 0.5f;
    t->phase += step * 0.5f;
    if(t->phase >= 1.0f) t->phase -= 1.0f;
    t->tilt += 0.55f * (pulse - t->tilt);

    t->random = t->random * 1664525U + 1013904223U;
    float noise = (float)(int32_t)t->random / 2147483648.0f;

    float x = t->tilt * t->voice * VOICE_GAIN + noise * t->aspiration * ASPIRATION_GAIN;
    for(uint32_t i = 0; i < 3; i++)
        x = resonator_run(&t->formant[i], x);

    float bandpass =
        t->noise.a * (noise - t->noise_x2) + t->noise.b * t->noise.y1 + t->noise.c * t->noise.y2;
    t->noise.y2 = t->noise.y1;
    t->noise.y1 = bandpass;
    t->noise_x2 = t->noise_x1;
    t->noise_x1 = noise;
    x += bandpass * t->frication * FRICATION_GAIN;

    // Nasals have a spectral zero that separates them from /l/ and /r/.
    float notched = (x - t->notch_c * t->notch_x1 + NOTCH_RADIUS * NOTCH_RADIUS * t->notch_x2) /
                    (1.0f - t->notch_c + NOTCH_RADIUS * NOTCH_RADIUS);
    t->notch_x2 = t->notch_x1;
    t->notch_x1 = x;
    x += (notched - x) * t->notch_depth;

    t->voice += t->voice_step;
    t->aspiration += t->aspiration_step;
    t->frication += t->frication_step;
    t->frame_samples--;
    return x;
}

// 12 kHz to 6 kHz low-pass, 2750 Hz cutoff, 31 symmetric taps.
static const float decimate_taps[16] = {
    0.000121f,
    0.000831f,
    -0.000231f,
    -0.003167f,
    -0.000680f,
    0.007799f,
    0.004588f,
    -0.014933f,
    -0.014763f,
    0.023798f,
    0.036960f,
    -0.032642f,
    -0.087760f,
    0.039223f,
    0.311722f,
    0.458267f,
};

static float decimate_next(PmrTts* t, float a, float b) {
    uint32_t at = t->history_at;
    t->history[at] = a;
    t->history[(at + 1) & 31U] = b;
    t->history_at = (at + 2) & 31U;
    uint32_t newest = (at + 1) & 31U;
    float y = decimate_taps[15] * t->history[(newest - 15U) & 31U];
    for(uint32_t i = 0; i < 15; i++)
        y += decimate_taps[i] *
             (t->history[(newest - i) & 31U] + t->history[(newest - 30U + i) & 31U]);
    return y;
}

// Removes what the radio cannot pass, then applies the same 700--3000 Hz
// pre-emphasis as the stored channel prompts.
static float output_next(PmrTts* t, float x) {
    float high = 0.9f * (t->highpass_y + x - t->highpass_x);
    t->highpass_x = x;
    t->highpass_y = high;
    const float k = 2.0f * (float)PMR_TTS_RATE;
    const float zero = k / (2.0f * PI_F * 700.0f), pole = k / (2.0f * PI_F * 3000.0f);
    float y = ((zero + 1.0f) * high + (1.0f - zero) * t->shelf_x - (1.0f - pole) * t->shelf_y) /
              (pole + 1.0f);
    t->shelf_x = high;
    t->shelf_y = y;
    return y;
}

size_t pmr_tts_render(PmrTts* t, int16_t* out, size_t count) {
    size_t done = 0;
    while(done < count) {
        if(!t->frame_samples && !frame_next(t)) break;
        float a = voice_next(t);
        if(!t->frame_samples && !frame_next(t)) break;
        float b = voice_next(t);
        // A soft limit keeps peaks at the 0.85 used by the stored prompts.
        float x = output_next(t, decimate_next(t, a, b)) * OUTPUT_GAIN, size = fabsf(x);
        if(size > 0.6f) size = 0.6f + 0.25f * tanhf((size - 0.6f) / 0.25f);
        out[done++] = (int16_t)((x < 0 ? -size : size) * 32767.0f);
    }
    return done;
}

void pmr_tts_describe(const PmrTts* t, char* out, size_t size) {
    size_t n = 0;
    for(uint32_t i = 0; i < t->count && n + 3 < size; i++) {
        if(t->phones[i].flags & FlagStress) out[n++] = '\'';
        out[n++] = phones[t->phones[i].code].symbol;
        if(t->phones[i].flags & FlagWordEnd && phones[t->phones[i].code].kind != ClassPause)
            out[n++] = ' ';
    }
    if(size) out[n] = 0;
}
