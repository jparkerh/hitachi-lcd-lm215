// Host-side timing verification for LM215 PORTA waveforms.
// Mocks all PORTA writes, runs one frame of the drive loop,
// then verifies the resulting signal sequence against the datasheet.
//
// Compile: g++ -std=c++11 -Wall test_timing.cpp -o test_timing
// Run:     ./test_timing

#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// PORTA bit definitions (same as main.cpp)
// ---------------------------------------------------------------------------
#define D1_BIT  0x01  // PA0
#define D2_BIT  0x02  // PA1
#define D3_BIT  0x04  // PA2
#define D4_BIT  0x08  // PA3
#define FLM_BIT 0x10  // PA4
#define M_BIT   0x20  // PA5
#define CL1_BIT 0x40  // PA6
#define CL2_BIT 0x80  // PA7

// ---------------------------------------------------------------------------
// PORTA mock: records every write
// ---------------------------------------------------------------------------
struct Write { uint8_t val; };
static std::vector<Write> g_log;
static uint8_t g_porta = 0;

static inline void PORTA_write(uint8_t v) {
    g_porta = v;
    g_log.push_back({v});
}

// ---------------------------------------------------------------------------
// Replicate the main loop logic verbatim, substituting PORTA_write for PORTA=
// (delays and NOPs are no-ops — we only care about logical transitions)
// ---------------------------------------------------------------------------
static void run_frame(uint8_t &m_state) {
    m_state = !m_state;

    PORTA_write((m_state ? M_BIT : 0) | FLM_BIT);  // frame start: M + FLM

    for (int row = 0; row < 64; row++) {
        uint8_t pixel_data = (row % 2 == 0) ? 0x0F : 0x00;
        uint8_t base = (m_state ? M_BIT : 0) | (row == 0 ? FLM_BIT : 0);

        for (int col = 0; col < 240; col++) {
            uint8_t d = base | pixel_data;
            PORTA_write(d);           // data setup
            PORTA_write(d | CL2_BIT); // CL2 high
            /* nops */
            PORTA_write(d);           // CL2 low
        }

        PORTA_write(base | CL1_BIT); // CL1 high
        /* delayMicroseconds(1) */
        PORTA_write(m_state ? M_BIT : 0); // CL1 low, FLM cleared
    }
}

// ---------------------------------------------------------------------------
// Analysis helpers
// ---------------------------------------------------------------------------
static bool bit(uint8_t v, uint8_t mask) { return (v & mask) != 0; }

static std::string describe(uint8_t v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "0x%02X [%s%s%s%s%s%s%s%s]", v,
        bit(v, CL2_BIT) ? "CL2 " : "",
        bit(v, CL1_BIT) ? "CL1 " : "",
        bit(v, M_BIT)   ? "M "   : "",
        bit(v, FLM_BIT) ? "FLM " : "",
        bit(v, D4_BIT)  ? "D4 "  : "",
        bit(v, D3_BIT)  ? "D3 "  : "",
        bit(v, D2_BIT)  ? "D2 "  : "",
        bit(v, D1_BIT)  ? "D1 "  : "");
    return buf;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
static int pass = 0, fail = 0;
#define EXPECT(cond, msg) do { \
    if (cond) { printf("PASS: %s\n", msg); pass++; } \
    else      { printf("FAIL: %s\n", msg); fail++; } \
} while(0)

int main() {
    uint8_t m_state = 0;
    run_frame(m_state);

    printf("Total PORTA writes this frame: %zu\n\n", g_log.size());
    // Expected: 1 (FLM setup) + 64 rows × (240×3 writes + 2 CL1 writes) = 1 + 64×722 = 46209
    // 240 cols × 3 writes each = 720, + CL1-high + CL1-low = 722 per row
    EXPECT(g_log.size() == 46209, "Total write count == 46209");

    // --- Walk log and collect per-row stats ---
    struct RowStats {
        int cl2_rising;
        int cl1_rising;
        bool flm_at_cl2_0;   // FLM high on first CL2 rising edge
        bool flm_at_cl1;     // FLM high when CL1 rises
        bool flm_after_cl1;  // FLM cleared after CL1 falls
        bool m_stable;       // M doesn't change mid-row
        uint8_t data_0;      // data bits on first CL2
    };

    std::vector<RowStats> rows;
    RowStats cur = {};
    cur.m_stable = true;
    uint8_t m_seen = g_log[0].val & M_BIT;

    for (size_t i = 1; i < g_log.size(); i++) {
        uint8_t v = g_log[i].val;
        uint8_t p = g_log[i-1].val;

        bool cl2_rose = !bit(p, CL2_BIT) &&  bit(v, CL2_BIT);
        bool cl1_rose = !bit(p, CL1_BIT) &&  bit(v, CL1_BIT);
        bool cl1_fell = !bit(v, CL1_BIT) &&  bit(p, CL1_BIT);

        if (cl2_rose) {
            cur.cl2_rising++;
            if (cur.cl2_rising == 1) {
                cur.flm_at_cl2_0 = bit(v, FLM_BIT);
                cur.data_0 = v & 0x0F;
            }
            if ((v & M_BIT) != m_seen) cur.m_stable = false;
        }
        if (cl1_rose) {
            cur.cl1_rising++;
            cur.flm_at_cl1 = bit(v, FLM_BIT);
        }
        if (cl1_fell) {
            cur.flm_after_cl1 = !bit(v, FLM_BIT);
            rows.push_back(cur);
            cur = {};
            cur.m_stable = true;
            m_seen = v & M_BIT;
        }
    }

    printf("\n--- Per-row signal checks (%zu rows detected) ---\n", rows.size());
    EXPECT(rows.size() == 64, "Exactly 64 rows per frame");

    // CL2 pulses per row
    bool all_cl2_ok = true;
    for (size_t r = 0; r < rows.size(); r++)
        if (rows[r].cl2_rising != 240) { all_cl2_ok = false; break; }
    EXPECT(all_cl2_ok, "Every row has exactly 240 CL2 pulses");

    // CL1 pulses per row
    bool all_cl1_ok = true;
    for (size_t r = 0; r < rows.size(); r++)
        if (rows[r].cl1_rising != 1) { all_cl1_ok = false; break; }
    EXPECT(all_cl1_ok, "Every row has exactly 1 CL1 pulse");

    // FLM: high on row 0 CL2 and CL1, low everywhere else
    EXPECT(rows.size() > 0 && rows[0].flm_at_cl2_0, "FLM high at row 0 first CL2");
    EXPECT(rows.size() > 0 && rows[0].flm_at_cl1,   "FLM high at row 0 CL1 latch");
    EXPECT(rows.size() > 0 && rows[0].flm_after_cl1,"FLM cleared after row 0 CL1");
    bool no_flm_after_row0 = true;
    for (size_t r = 1; r < rows.size(); r++)
        if (rows[r].flm_at_cl2_0 || rows[r].flm_at_cl1) { no_flm_after_row0 = false; break; }
    EXPECT(no_flm_after_row0, "FLM low on all rows 1-63");

    // M stable throughout frame
    bool m_stable_all = true;
    for (size_t r = 0; r < rows.size(); r++)
        if (!rows[r].m_stable) { m_stable_all = false; break; }
    EXPECT(m_stable_all, "M signal stable within every row");

    // Data alternates per row
    EXPECT(rows.size() >= 2 && rows[0].data_0 == 0x0F, "Row 0 data = 0x0F (all on)");
    EXPECT(rows.size() >= 2 && rows[1].data_0 == 0x00, "Row 1 data = 0x00 (all off)");

    // Print first-row sample
    printf("\n--- First 10 writes (frame setup + row 0 start) ---\n");
    for (int i = 0; i < 10 && i < (int)g_log.size(); i++)
        printf("  [%d] %s\n", i, describe(g_log[i].val).c_str());
    printf("\n--- Row 0 CL1 latch region ---\n");
    // find first CL1 rise
    for (size_t i = 1; i < g_log.size(); i++) {
        if (!bit(g_log[i-1].val, CL1_BIT) && bit(g_log[i].val, CL1_BIT)) {
            for (int j = -1; j <= 2; j++)
                printf("  [%zu] %s\n", i+j, describe(g_log[i+j].val).c_str());
            break;
        }
    }

    printf("\n%d/%d checks passed\n", pass, pass+fail);
    return fail ? 1 : 0;
}
