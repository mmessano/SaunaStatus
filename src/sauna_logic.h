#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>

namespace { constexpr float _snan = std::numeric_limits<float>::quiet_NaN(); }

inline float c2f(float c) { return c * 9.0f / 5.0f + 32.0f; }
inline float f2c(float f) { return (f - 32.0f) * 5.0f / 9.0f; }

inline void fmtVal(char *out, size_t len, float v) {
    if (std::isnan(v)) snprintf(out, len, "null");
    else snprintf(out, len, "%.1f", v);
}

// Stale if: last_ok_ms==0 (never read) OR age > threshold_ms.
// threshold_ms==0 disables stale checking.
inline bool isSensorStale(unsigned long last_ok_ms, unsigned long now_ms, unsigned long threshold_ms) {
    if (threshold_ms == 0) return false;
    if (last_ok_ms == 0) return true;
    return (now_ms - last_ok_ms) > threshold_ms;
}

struct SaunaConfig {
    float ceiling_setpoint_f = 160.0f;
    float bench_setpoint_f   = 120.0f;
    bool  ceiling_pid_en     = false;
    bool  bench_pid_en       = false;
};

// One optional-override layer. Fields are only applied when has_* is true.
struct ConfigLayer {
    float ceiling_setpoint_f = 0.0f;
    float bench_setpoint_f   = 0.0f;
    bool  ceiling_pid_en     = false;
    bool  bench_pid_en       = false;
    bool  has_ceiling_sp     = false;
    bool  has_bench_sp       = false;
    bool  has_ceiling_en     = false;
    bool  has_bench_en       = false;
};

// Merge one ConfigLayer into cfg. Range validation: setpoints must be 32-300 degrees F.
inline void mergeConfigLayer(SaunaConfig &cfg, const ConfigLayer &layer) {
    if (layer.has_ceiling_sp && layer.ceiling_setpoint_f >= 32.0f && layer.ceiling_setpoint_f <= 300.0f)
        cfg.ceiling_setpoint_f = layer.ceiling_setpoint_f;
    if (layer.has_bench_sp && layer.bench_setpoint_f >= 32.0f && layer.bench_setpoint_f <= 300.0f)
        cfg.bench_setpoint_f = layer.bench_setpoint_f;
    if (layer.has_ceiling_en) cfg.ceiling_pid_en = layer.ceiling_pid_en;
    if (layer.has_bench_en)   cfg.bench_pid_en   = layer.bench_pid_en;
}

struct SensorValues {
    float ceiling_temp    = _snan;
    float ceiling_hum     = _snan;
    float bench_temp      = _snan;
    float bench_hum       = _snan;
    float stove_temp      = _snan;
    float pwr_bus_V       = _snan;
    float pwr_current_mA  = _snan;
    float pwr_mW          = _snan;
    unsigned long ceiling_last_ok_ms = 0;
    unsigned long bench_last_ok_ms   = 0;
    unsigned long stale_threshold_ms = 10000UL;
};

struct MotorState {
    unsigned short outflow_pos = 0;
    int            outflow_dir = 0;
    unsigned short inflow_pos  = 0;
    int            inflow_dir  = 0;
};

struct PIDState {
    float ceiling_output  = 0.0f;
    float bench_output    = 0.0f;
    bool  c_cons_mode     = false;
    bool  b_cons_mode     = false;
    bool  ceiling_pid_en  = false;
    bool  bench_pid_en    = false;
    float Ceilingpoint    = 0.0f;  // degrees C
    float Benchpoint      = 0.0f;  // degrees C
    bool  overheat_alarm  = false;
};

// Build WebSocket JSON. Stale readings become null; cst/bst flags added.
// Pass now_ms=0 with stale_threshold_ms=0 to disable stale detection.
inline void buildJsonFull(const SensorValues &sv, const MotorState &ms,
                          const PIDState &ps, unsigned long now_ms,
                          char *buf, size_t len)
{
    bool c_stale = isSensorStale(sv.ceiling_last_ok_ms, now_ms, sv.stale_threshold_ms);
    bool b_stale = isSensorStale(sv.bench_last_ok_ms,   now_ms, sv.stale_threshold_ms);

    float ct = c_stale ? _snan : sv.ceiling_temp;
    float ch = c_stale ? _snan : sv.ceiling_hum;
    float bt = b_stale ? _snan : sv.bench_temp;
    float bh = b_stale ? _snan : sv.bench_hum;

    char clt[8], clh[8], d5t[8], d5h[8], tct[8];
    char csp[8], cop[8], bsp[8], bop[8];
    char pvolt[10], pcurr[10], pmw[10];

    fmtVal(clt,   sizeof(clt),   c2f(ct));
    fmtVal(clh,   sizeof(clh),   ch);
    fmtVal(d5t,   sizeof(d5t),   c2f(bt));
    fmtVal(d5h,   sizeof(d5h),   bh);
    fmtVal(tct,   sizeof(tct),   c2f(sv.stove_temp));
    fmtVal(csp,   sizeof(csp),   c2f(ps.Ceilingpoint));
    fmtVal(cop,   sizeof(cop),   ps.ceiling_output);
    fmtVal(bsp,   sizeof(bsp),   c2f(ps.Benchpoint));
    fmtVal(bop,   sizeof(bop),   ps.bench_output);
    fmtVal(pvolt, sizeof(pvolt), sv.pwr_bus_V);
    fmtVal(pcurr, sizeof(pcurr), sv.pwr_current_mA);
    fmtVal(pmw,   sizeof(pmw),   sv.pwr_mW);

    snprintf(buf, len,
             "{\"clt\":%s,\"clh\":%s,\"d5t\":%s,\"d5h\":%s,\"tct\":%s"
             ",\"ofs\":%u,\"ofd\":%d,\"ifs\":%u,\"ifd\":%d"
             ",\"csp\":%s,\"cop\":%s,\"ctm\":%d,\"cen\":%d"
             ",\"bsp\":%s,\"bop\":%s,\"btm\":%d,\"ben\":%d"
             ",\"pvolt\":%s,\"pcurr\":%s,\"pmw\":%s,\"oa\":%d"
             ",\"cst\":%d,\"bst\":%d}",
             clt, clh, d5t, d5h, tct,
             ms.outflow_pos, ms.outflow_dir, ms.inflow_pos, ms.inflow_dir,
             csp, cop, (int)ps.c_cons_mode, (int)ps.ceiling_pid_en,
             bsp, bop, (int)ps.b_cons_mode, (int)ps.bench_pid_en,
             pvolt, pcurr, pmw, (int)ps.overheat_alarm,
             (int)c_stale, (int)b_stale);
}
