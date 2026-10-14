import argparse
import os
import re
import sys
from dataclasses import dataclass

import numpy as np
from scipy import signal


# Block partition presets for FFT convolution (phase taps are processed in blocks).
# head block: first block evaluated with a short FFT, tail: remaining blocks.
@dataclass(frozen=True)
class BlockConfig:
    head_block_len: int
    tail_block_len: int
    max_tail_parts: int


# u>=4 style filters: long phase, split 128 head + one 256 tail block
BLOCK_LARGE = BlockConfig(head_block_len=128, tail_block_len=256, max_tail_parts=1)
# u==2 style filters: short phase, split 64 head + up to two 64 tail blocks
BLOCK_SMALL = BlockConfig(head_block_len=64, tail_block_len=64, max_tail_parts=2)

# Default attenuation targets (hp = high power mode, lp = low power mode)
DEFAULT_PASS_HZ = 20000.0
DEFAULT_ATTEN_HP_DB = 145.0
DEFAULT_ATTEN_LP_DB = 120.0

FREQZ_POINTS = 65536

# Equiripple (Parks-McClellan) design search parameters.
# Optimal designs need ~0.6x the Kaiser window order, so the tap-count
# search starts below the Kaiser estimate and grows the count from there.
REMEZ_TAP_FACTOR = 0.60
REMEZ_TAP_STEP_RATIO = 1.08
REMEZ_MAX_PASS_RIPPLE_DB = 0.05

# Core1 filter specs
CORE1_PASS_HZ = 20000.0
CORE1_STOP_RATIO = 0.49
CORE1_ATTEN_HP_DB = 140.0
CORE1_ATTEN_LP_DB = 110.0
CORE1_POLY_TAPS_MAX = 48

# Halfband FIR filter specs
# need to set "passband < fs_out/4 < stopband"
HB_44_PASS_HZ = 20000.0
HB_44_STOP_HZ = 132300.0
HB_48_PASS_HZ = 20000.0
HB_48_STOP_HZ = 144000.0
HB_44_HI_PASS_HZ = 20000.0
HB_44_HI_STOP_HZ = 264600.0
HB_48_HI_PASS_HZ = 20000.0
HB_48_HI_STOP_HZ = 288000.0
HB_STOP_ATTEN_DB = 145.0


@dataclass
class Spec:
    fs_out: float
    up_ratio: int
    f_stop: float
    f_pass: float = DEFAULT_PASS_HZ
    block: BlockConfig = BLOCK_LARGE
    atten_hp_db: float = DEFAULT_ATTEN_HP_DB
    atten_lp_db: float = DEFAULT_ATTEN_LP_DB
    tag: str = ""
    name: str = ""

    def __post_init__(self):
        if not self.name:
            fs_in = int(round(self.fs_out / self.up_ratio))
            self.name = f"in{fs_in}_to_{int(self.fs_out)}_u{self.up_ratio}"
            if self.tag:
                self.name += f"_tag{self.tag}"

    @property
    def head_block_len(self) -> int:
        return self.block.head_block_len

    @property
    def tail_block_len(self) -> int:
        return self.block.tail_block_len

    @property
    def max_tail_parts(self) -> int:
        return self.block.max_tail_parts


# Core0 upsampling filters.
# The filter name (and thus the exported C symbol fft_fir_profile_<name>_<hp|lp>,
# referenced from src/fft_fir_core0.c) is derived from the filter *type* only:
#   in<fs_in>_to_<fs_out>_u<up_ratio>[_tag<tag>]
# so changing pass/stop bands or attenuation never breaks the firmware sources.
#   fs_out   : output sample rate [Hz]
#   up_ratio : interpolation factor
#   f_stop   : stopband edge [Hz]  (passband edge uses DEFAULT_PASS_HZ)
#   block    : BLOCK_LARGE / BLOCK_SMALL (see presets above)
#   tag      : optional suffix to keep two same-type filters apart
CORE0_SPECS = [
    Spec(fs_out=176400.0, up_ratio=4, f_stop=28000.0, block=BLOCK_LARGE),
    Spec(fs_out=176400.0, up_ratio=2, f_stop=44100.0, block=BLOCK_SMALL),
    Spec(fs_out=192000.0, up_ratio=4, f_stop=28000.0, block=BLOCK_LARGE),
    Spec(fs_out=192000.0, up_ratio=2, f_stop=48000.0, block=BLOCK_SMALL),
    Spec(fs_out=352800.0, up_ratio=8, f_stop=28000.0, block=BLOCK_LARGE),
    Spec(fs_out=352800.0, up_ratio=4, f_stop=44100.0, block=BLOCK_LARGE, tag="22050"),
    Spec(fs_out=352800.0, up_ratio=4, f_stop=48000.0, block=BLOCK_LARGE, tag="24000"),
    Spec(fs_out=352800.0, up_ratio=2, f_stop=96000.0, block=BLOCK_SMALL),
    Spec(fs_out=384000.0, up_ratio=8, f_stop=28000.0, block=BLOCK_LARGE),
    Spec(fs_out=384000.0, up_ratio=4, f_stop=48000.0, block=BLOCK_LARGE),
    Spec(fs_out=384000.0, up_ratio=2, f_stop=96000.0, block=BLOCK_SMALL),
]


def core1_stop(fs_out: float) -> float:
    return fs_out * CORE1_STOP_RATIO


def core1_name(fs_out: float, up_ratio: int) -> str:
    fs_in = int(round(fs_out / up_ratio))
    return f"in{fs_in}_to_{int(fs_out)}_u{up_ratio}"


def make_core1_spec(fs_out: float, up_ratio: int, block: BlockConfig) -> Spec:
    return Spec(
        name=core1_name(fs_out, up_ratio),
        fs_out=fs_out,
        up_ratio=up_ratio,
        f_stop=core1_stop(fs_out),
        f_pass=CORE1_PASS_HZ,
        block=block,
        atten_hp_db=CORE1_ATTEN_HP_DB,
        atten_lp_db=CORE1_ATTEN_LP_DB,
    )


# Core1 upsampling filters (names are referenced from src/fft_fir_core1.c).
CORE1_SPECS = [
    make_core1_spec(176400.0, 4, BLOCK_LARGE),
    make_core1_spec(176400.0, 2, BLOCK_SMALL),
    make_core1_spec(192000.0, 4, BLOCK_LARGE),
    make_core1_spec(192000.0, 2, BLOCK_SMALL),
    make_core1_spec(352800.0, 8, BLOCK_LARGE),
    make_core1_spec(352800.0, 4, BLOCK_LARGE),
    make_core1_spec(352800.0, 2, BLOCK_SMALL),
    make_core1_spec(384000.0, 8, BLOCK_LARGE),
    make_core1_spec(384000.0, 4, BLOCK_LARGE),
    make_core1_spec(384000.0, 2, BLOCK_SMALL),
    make_core1_spec(705600.0, 2, BLOCK_SMALL),
    make_core1_spec(768000.0, 2, BLOCK_SMALL),
]

# Core1 u2 fallback polyphase (non-FFT) filters
# (names are referenced from src/upsampling.c).
CORE1_POLY_SPECS = [
    make_core1_spec(176400.0, 2, BLOCK_SMALL),
    make_core1_spec(192000.0, 2, BLOCK_SMALL),
    make_core1_spec(352800.0, 2, BLOCK_SMALL),
    make_core1_spec(384000.0, 2, BLOCK_SMALL),
    make_core1_spec(705600.0, 2, BLOCK_SMALL),
    make_core1_spec(768000.0, 2, BLOCK_SMALL),
]


def align_to_multiple(value: int, step: int) -> int:
    if value % step == 0:
        return value
    return value + (step - (value % step))


def measure_stop_atten_db(taps: np.ndarray, fs_out: float, f_stop: float) -> float:
    w, h = signal.freqz(taps, worN=FREQZ_POINTS, fs=fs_out)
    stop_mask = w >= f_stop
    if not np.any(stop_mask):
        return 0.0
    stop_mag = np.max(np.abs(h[stop_mask]))
    dc_gain = abs(np.sum(taps))
    if stop_mag <= 0:
        return 999.0
    return 20.0 * np.log10(dc_gain / stop_mag)


def find_half_gain_freq_db(taps: np.ndarray, fs_out: float, f_from: float, f_to: float):
    w, h = signal.freqz(taps, worN=FREQZ_POINTS, fs=fs_out)
    mask = (w >= f_from) & (w <= f_to)
    wm = w[mask]
    hm = np.abs(h[mask])
    half = 0.5 * abs(np.sum(taps))
    below = np.nonzero(hm <= half)[0]
    if below.size == 0:
        return None
    i = int(below[0])
    if i == 0:
        return float(wm[0])
    h0, h1 = hm[i - 1], hm[i]
    if h0 == h1:
        return float(wm[i])
    frac = (h0 - half) / (h0 - h1)
    return float(wm[i - 1] + frac * (wm[i] - wm[i - 1]))


def remez_guard_hz(fs_out: float, edge_hz: float, taps_count: int, band_span: float) -> float:
    # For an equiripple design the band-edge sample sits at half gain and the
    # true ripple floor is only reached at the first extremum inside the band.
    # Skip min(one ripple period, 10% of the band span) past the edge.
    period = (fs_out / 2.0) / max(taps_count, 1)
    return max(0.0, min(0.1 * band_span, period))


def remez_stop_atten_db(taps: np.ndarray, fs_out: float, f_stop: float, taps_count: int) -> float:
    w, h = signal.freqz(taps, worN=FREQZ_POINTS, fs=fs_out)
    span = fs_out / 2.0 - f_stop
    mask = w >= f_stop + remez_guard_hz(fs_out, f_stop, taps_count, max(span, 0.0))
    if not np.any(mask):
        mask = w >= f_stop
    if not np.any(mask):
        return 0.0
    stop_mag = np.max(np.abs(h[mask]))
    dc_gain = abs(np.sum(taps))
    if stop_mag <= 0 or dc_gain <= 0:
        return 999.0
    return 20.0 * np.log10(dc_gain / stop_mag)


def remez_pass_ripple_db(taps: np.ndarray, fs_out: float, f_pass: float, taps_count: int) -> float:
    w, h = signal.freqz(taps, worN=FREQZ_POINTS, fs=fs_out)
    mask = (w >= 0.0) & (w <= f_pass - remez_guard_hz(fs_out, f_pass, taps_count, f_pass))
    if not np.any(mask):
        mask = (w >= 0.0) & (w <= f_pass)
    if not np.any(mask):
        return 0.0
    dc_gain = abs(np.sum(taps))
    if dc_gain <= 0:
        return 999.0
    return float(np.max(np.abs(20.0 * np.log10(np.maximum(np.abs(h[mask]) / dc_gain, 1e-12)))))


def remez_design(fs_out: float, f_pass: float, f_stop: float, up_ratio: int, atten_db: float, max_taps: int, beta):
    # Parks-McClellan optimal design: band edges are taken from the spec
    # exactly, so the transition band lands where it was configured.
    # Passband/stopband deviations are weighted equally (delta_p = delta_s),
    # which minimizes the required tap count.
    width = f_stop - f_pass
    norm_width = width / (fs_out / 2.0)
    kaiser_taps, _ = signal.kaiserord(atten_db, norm_width)
    start = align_to_multiple(max(2, int(round(REMEZ_TAP_FACTOR * kaiser_taps))), up_ratio)
    start = max(2 * up_ratio, min(start, max_taps))
    bands = [0.0, f_pass, f_stop, fs_out / 2.0]
    taps_count = start
    while taps_count <= max_taps:
        try:
            taps = signal.remez(taps_count, bands, [1.0, 0.0], fs=fs_out)
        except Exception:
            next_count = int(np.ceil(max(taps_count + up_ratio, taps_count * REMEZ_TAP_STEP_RATIO)))
            taps_count = align_to_multiple(next_count, up_ratio)
            continue
        stop_att = remez_stop_atten_db(taps, fs_out, f_stop, taps_count)
        ripple_db = remez_pass_ripple_db(taps, fs_out, f_pass, taps_count)
        if stop_att >= atten_db and ripple_db <= REMEZ_MAX_PASS_RIPPLE_DB:
            return taps, stop_att
        next_count = int(np.ceil(max(taps_count + up_ratio, taps_count * REMEZ_TAP_STEP_RATIO)))
        taps_count = align_to_multiple(next_count, up_ratio)
    return None


def prewarp_firwin(taps_count: int, fs_out: float, f_pass: float, f_stop: float, beta: float) -> np.ndarray:
    # The firwin cutoff is the -6 dB point of the response, i.e. the center of
    # the transition band, while the spec defines the passband/stopband edges.
    # Pre-warp the cutoff with a bisection so that the measured half-gain
    # frequency matches the transition-band center.
    center = 0.5 * (f_pass + f_stop)
    lo, hi = f_pass, f_stop
    c = center
    taps = signal.firwin(taps_count, c, window=("kaiser", beta), fs=fs_out)
    for _ in range(12):
        f6 = find_half_gain_freq_db(taps, fs_out, max(1.0, f_pass * 0.5), min(f_stop * 2.0, fs_out / 2.0))
        if f6 is None:
            break
        if abs(f6 - center) <= fs_out / FREQZ_POINTS:
            break
        if f6 < center:
            lo = c
        else:
            hi = c
        c = 0.5 * (lo + hi)
        taps = signal.firwin(taps_count, c, window=("kaiser", beta), fs=fs_out)
    return taps


def window_design(fs_out: float, f_pass: float, f_stop: float, up_ratio: int, atten_db: float, max_taps: int):
    # Fallback design path: Kaiser window method with pre-warped cutoff,
    # scanning the tap count up to the block-length budget.
    width = f_stop - f_pass
    norm_width = width / (fs_out / 2.0)
    taps_guess, beta = signal.kaiserord(atten_db, norm_width)
    taps_guess = align_to_multiple(max(2, int(taps_guess)), up_ratio)
    taps_guess = min(taps_guess, max_taps)

    best = None
    for taps_count in range(taps_guess, max_taps + 1, up_ratio):
        taps = prewarp_firwin(taps_count, fs_out, f_pass, f_stop, beta)
        taps = taps * up_ratio
        stop_att = measure_stop_atten_db(taps, fs_out, f_stop)
        if best is None or stop_att > best[2]:
            best = (taps, beta, stop_att)
        if stop_att >= atten_db:
            return taps, beta, stop_att

    return best


def design_lowpass(fs_out: float, f_pass: float, f_stop: float, up_ratio: int, atten_db: float, max_taps: int):
    width = f_stop - f_pass
    norm_width = width / (fs_out / 2.0)
    _, beta = signal.kaiserord(atten_db, norm_width)

    result = remez_design(fs_out, f_pass, f_stop, up_ratio, atten_db, max_taps, beta)
    if result is not None:
        taps, stop_att = result
        # Scale so that the DC gain is up_ratio (same convention as firwin-based path).
        return taps * up_ratio, beta, stop_att

    return window_design(fs_out, f_pass, f_stop, up_ratio, atten_db, max_taps)


def design_filter(spec: Spec, atten_db: float):
    max_phase_len = max(spec.head_block_len, spec.tail_block_len * spec.max_tail_parts)
    max_taps = spec.up_ratio * max_phase_len
    return design_lowpass(spec.fs_out, spec.f_pass, spec.f_stop, spec.up_ratio, atten_db, max_taps)


def design_polyphase_filter(spec: Spec, atten_db: float, max_taps: int):
    return design_lowpass(spec.fs_out, spec.f_pass, spec.f_stop, spec.up_ratio, atten_db, max_taps)


def pack_rfft(H: np.ndarray, fft_len: int) -> np.ndarray:
    packed = np.zeros(fft_len, dtype=np.float32)
    packed[0] = H[0].real.astype(np.float32)
    packed[1] = H[-1].real.astype(np.float32)
    half = fft_len // 2
    for k in range(1, half):
        packed[2 * k] = H[k].real.astype(np.float32)
        packed[2 * k + 1] = H[k].imag.astype(np.float32)
    return packed


def build_partition_fft(phase_taps: np.ndarray, block_len: int, fft_len: int) -> tuple:
    parts = int(np.ceil(phase_taps.shape[0] / block_len))
    if parts <= 0:
        parts = 1
    h_fft = np.zeros((parts, fft_len), dtype=np.float32)
    for part in range(parts):
        start = part * block_len
        end = min(start + block_len, phase_taps.shape[0])
        padded = np.zeros(fft_len, dtype=np.float32)
        if end > start:
            padded[: end - start] = phase_taps[start:end].astype(np.float32)
        H = np.fft.rfft(padded)
        h_fft[part, :] = pack_rfft(H, fft_len)
    return h_fft, parts


def build_head_tail_fft(taps: np.ndarray, up_ratio: int, head_block_len: int, tail_block_len: int) -> tuple:
    phase_len = taps.shape[0] // up_ratio
    if phase_len <= 0:
        raise ValueError("phase length must be positive")
    tail_len = max(0, phase_len - head_block_len)
    tail_parts = int(np.ceil(phase_len / tail_block_len)) if tail_len > 0 else 0

    head_fft_len = head_block_len * 2
    tail_fft_len = tail_block_len * 2
    head_h_fft = np.zeros((up_ratio, 1, head_fft_len), dtype=np.float32)
    tail_h_fft = np.zeros((up_ratio, max(1, tail_parts), tail_fft_len), dtype=np.float32)

    for phase in range(up_ratio):
        phase_taps = taps[phase::up_ratio]
        if phase_taps.shape[0] != phase_len:
            raise ValueError("phase length mismatch")
        head_taps = phase_taps[:head_block_len]
        head_part, _ = build_partition_fft(head_taps, head_block_len, head_fft_len)
        head_h_fft[phase, 0, :] = head_part[0]

        if tail_len > 0:
            tail_taps = phase_taps[head_block_len:]
            if tail_taps.shape[0] != tail_len:
                raise ValueError("tail length mismatch")
            tail_taps = np.concatenate([np.zeros(head_block_len, dtype=np.float32), tail_taps.astype(np.float32)])
            tail_part, parts = build_partition_fft(tail_taps, tail_block_len, tail_fft_len)
            if parts != tail_parts:
                raise ValueError("tail parts mismatch")
            tail_h_fft[phase, :parts, :] = tail_part

    return head_h_fft, tail_h_fft, tail_parts


def design_halfband(fs_out: float, f_pass: float, f_stop: float, atten_db: float, max_taps: int = 1023):
    if f_pass <= 0.0 or f_stop <= 0.0:
        raise ValueError("halfband pass/stop must be positive")
    if f_pass >= f_stop:
        raise ValueError("halfband pass must be below stop")
    if f_pass >= (fs_out / 4.0) or f_stop < (fs_out / 4.0):
        raise ValueError("halfband pass/stop must straddle fs_out/4")
    # The cutoff is pinned to fs_out/4 (that is what makes the odd/even tap
    # halfband structure work), so the measured attenuation is verified and
    # the tap count / beta are grown until the configured stop attenuation is
    # actually achieved.
    best = None
    taps_guess = signal.kaiserord(atten_db, (f_stop - f_pass) / (fs_out / 2.0))[0]
    taps = max(7, int(taps_guess))
    if taps % 2 == 0:
        taps += 1
    mod = taps % 4
    if mod != 3:
        taps += (3 - mod) % 4
    beta = signal.kaiserord(atten_db, (f_stop - f_pass) / (fs_out / 2.0))[1]
    while taps <= max_taps:
        taps_fir = signal.firwin(taps, fs_out / 4.0, window=("kaiser", beta), fs=fs_out)
        taps_fir *= 2.0
        stop_att = measure_stop_atten_db(taps_fir, fs_out, f_stop)
        if best is None or stop_att > best["stop_att"]:
            best = {
                "taps_fir": taps_fir,
                "taps": taps,
                "beta": beta,
                "stop_att": stop_att,
            }
        if stop_att >= atten_db:
            break
        taps += 4
    taps_fir = best["taps_fir"]
    taps = best["taps"]
    beta = best["beta"]
    stop_att = best["stop_att"]
    even_taps = taps_fir[::2].astype(np.float32)
    center = float(taps_fir[taps // 2])
    center_index = (taps - 3) // 4
    return even_taps, center, center_index, taps, beta, stop_att, taps_fir


def calc_dc_gain(head_h_fft: np.ndarray, tail_h_fft: np.ndarray, tail_parts: int) -> float:
    head_dc = np.sum(head_h_fft[:, :, 0], axis=1)
    tail_dc = np.zeros_like(head_dc)
    if tail_parts > 0:
        tail_dc = np.sum(tail_h_fft[:, :tail_parts, 0], axis=1)
    return float(np.mean(head_dc + tail_dc))


def format_c_array(values: np.ndarray, indent: str = "    ", per_line: int = 4) -> str:
    flat = values.flatten()
    lines = []
    for i in range(0, flat.shape[0], per_line):
        chunk = flat[i : i + per_line]
        line = indent + ", ".join(f"{v:.9e}f" for v in chunk)
        lines.append(line)
    return ",\n".join(lines)


def add_profiles(specs: list, name_prefix: str, profiles: list, maxima: dict):
    for spec in specs:
        for suffix, atten in (("hp", spec.atten_hp_db), ("lp", spec.atten_lp_db)):
            result = design_filter(spec, atten)
            if result is None:
                raise RuntimeError(f"Failed to design filter for {spec.name}_{suffix}")
            taps, beta, stop_att = result
            taps_count = taps.shape[0]
            if taps_count % spec.up_ratio != 0:
                raise RuntimeError("tap count not divisible by up_ratio")
            phase_len = taps_count // spec.up_ratio
            head_fft_len = spec.head_block_len * 2
            tail_fft_len = spec.tail_block_len * 2
            input_len = spec.head_block_len
            head_h_fft, tail_h_fft, tail_parts = build_head_tail_fft(
                taps, spec.up_ratio, spec.head_block_len, spec.tail_block_len
            )
            dc_gain = calc_dc_gain(head_h_fft, tail_h_fft, tail_parts)
            gain_ratio = 1.0 / dc_gain if abs(dc_gain) > 1e-9 else 1.0
            profiles.append(
                {
                    "spec": spec,
                    "suffix": suffix,
                    "name_prefix": name_prefix,
                    "taps": taps,
                    "head_fft_len": head_fft_len,
                    "head_block_len": spec.head_block_len,
                    "head_parts": 1,
                    "tail_fft_len": tail_fft_len,
                    "tail_block_len": spec.tail_block_len,
                    "tail_parts": tail_parts,
                    "taps_count": taps_count,
                    "phase_len": phase_len,
                    "input_len": input_len,
                    "stop_att": stop_att,
                    "head_h_fft": head_h_fft,
                    "tail_h_fft": tail_h_fft,
                    "dc_gain": dc_gain,
                    "gain_ratio": gain_ratio,
                }
            )
            maxima["phase_len"] = max(maxima["phase_len"], phase_len)
            maxima["head_parts"] = max(maxima["head_parts"], 1)
            maxima["tail_parts"] = max(maxima["tail_parts"], tail_parts)
            maxima["up_ratio"] = max(maxima["up_ratio"], spec.up_ratio)
            maxima["head_block_len"] = max(maxima["head_block_len"], spec.head_block_len)
            maxima["tail_block_len"] = max(maxima["tail_block_len"], spec.tail_block_len)
            maxima["head_fft_len"] = max(maxima["head_fft_len"], head_fft_len)
            maxima["tail_fft_len"] = max(maxima["tail_fft_len"], tail_fft_len)


def add_poly_profiles(specs: list, poly_profiles: list, maxima: dict):
    for spec in specs:
        for suffix, atten in (("hp", spec.atten_hp_db), ("lp", spec.atten_lp_db)):
            result = design_polyphase_filter(spec, atten, CORE1_POLY_TAPS_MAX)
            if result is None:
                raise RuntimeError(f"Failed to design polyphase filter for {spec.name}_{suffix}")
            taps, beta, stop_att = result
            taps_count = taps.shape[0]
            if taps_count % spec.up_ratio != 0:
                raise RuntimeError("polyphase tap count not divisible by up_ratio")
            phase_len = taps_count // spec.up_ratio
            even_taps = taps[::2].astype(np.float32)
            odd_taps = taps[1::2].astype(np.float32)
            if even_taps.shape[0] != phase_len or odd_taps.shape[0] != phase_len:
                raise RuntimeError("polyphase tap length mismatch")
            dc_gain = float(np.sum(taps))
            gain_ratio = 1.0 / dc_gain if abs(dc_gain) > 1e-9 else 1.0
            poly_profiles.append(
                {
                    "spec": spec,
                    "suffix": suffix,
                    "taps": taps,
                    "taps_count": taps_count,
                    "phase_len": phase_len,
                    "stop_att": stop_att,
                    "even_taps": even_taps,
                    "odd_taps": odd_taps,
                    "dc_gain": dc_gain,
                    "gain_ratio": gain_ratio,
                }
            )
            maxima["poly_taps"] = max(maxima["poly_taps"], taps_count)
            maxima["poly_phase_len"] = max(maxima["poly_phase_len"], phase_len)


# ---------------------------------------------------------------------------
# Frequency response plots
# ---------------------------------------------------------------------------


def collect_plot_groups(profiles: list, poly_profiles: list, halfbands: dict) -> list:
    groups = {}
    order = []
    for p in profiles:
        spec = p["spec"]
        label = f"{p['name_prefix']}{spec.name}"
        key = ("fft", label)
        if key not in groups:
            groups[key] = {
                "label": label,
                "fs_out": spec.fs_out,
                "f_pass": spec.f_pass,
                "f_stop": spec.f_stop,
                "variants": {},
            }
            order.append(key)
        groups[key]["variants"][p["suffix"]] = p["taps"]
    for p in poly_profiles:
        spec = p["spec"]
        label = f"core1_poly_{spec.name}"
        key = ("poly", label)
        if key not in groups:
            groups[key] = {
                "label": label,
                "fs_out": spec.fs_out,
                "f_pass": spec.f_pass,
                "f_stop": spec.f_stop,
                "variants": {},
            }
            order.append(key)
        groups[key]["variants"][p["suffix"]] = p["taps"]
    for label, hb in halfbands.items():
        key = ("halfband", label)
        groups[key] = {
            "label": label,
            "fs_out": hb["fs_out"],
            "f_pass": hb["f_pass"],
            "f_stop": hb["f_stop"],
            "variants": {"hb": hb["taps"]},
        }
        order.append(key)
    return [groups[k] for k in order]


def filter_plot_groups(groups: list, patterns: list) -> list:
    if not patterns:
        return groups
    return [g for g in groups if any(pat in g["label"] for pat in patterns)]


def plot_response_group(group: dict, plt):
    fig, axes = plt.subplots(2, 2, figsize=(12, 7), sharex="col", tight_layout=True)
    ax_gain, ax_gain_zoom = axes[0, 0], axes[0, 1]
    ax_phase, ax_phase_zoom = axes[1, 0], axes[1, 1]
    fs = group["fs_out"]
    nyq = fs / 2.0
    f_stop = group["f_stop"]
    zoom_stop = min(nyq, max(f_stop * 3.0, group["f_pass"] * 3.0))

    for variant, taps in group["variants"].items():
        w, h = signal.freqz(taps, worN=FREQZ_POINTS, fs=fs)
        pass_mask = w <= group["f_pass"]
        ref = np.max(np.abs(h[pass_mask])) if np.any(pass_mask) else 1.0
        ref = ref if ref > 0 else 1.0
        gain_db = 20.0 * np.log10(np.maximum(np.abs(h) / ref, 1e-12))
        phase_deg = np.degrees(np.unwrap(np.angle(h)))
        color = "C0" if variant == "hp" else ("C1" if variant == "lp" else "C2")
        style = "-" if variant == "hp" else ("--" if variant == "lp" else "-")
        for ax in (ax_gain, ax_phase):
            ax.axvline(group["f_pass"], color="g", lw=0.8, ls=":")
            ax.axvline(f_stop, color="r", lw=0.8, ls=":")
        ax_gain.plot(w, gain_db, style, color=color, label=variant)
        ax_phase.plot(w, phase_deg, style, color=color, label=variant)
        zoom_mask = w <= zoom_stop
        ax_gain_zoom.plot(w[zoom_mask], gain_db[zoom_mask], style, color=color, label=variant)
        ax_phase_zoom.plot(w[zoom_mask], phase_deg[zoom_mask], style, color=color, label=variant)

    for ax in (ax_gain, ax_gain_zoom):
        ax.set_ylim(-200, 10)
        ax.set_ylabel("Gain [dB]")
        ax.grid(True, which="both", lw=0.3)
    for ax in (ax_phase, ax_phase_zoom):
        ax.set_ylabel("Phase [deg]")
        ax.set_xlabel("Frequency [Hz]")
        ax.grid(True, which="both", lw=0.3)
    for ax in (ax_gain_zoom, ax_phase_zoom):
        ax.set_xlim(0, zoom_stop)

    ax_gain.set_title(f"{group['label']} (fs={int(fs)} Hz)")
    ax_gain_zoom.set_title(f"zoom 0..{int(zoom_stop)} Hz  (green=pass {int(group['f_pass'])} Hz, red=stop {int(f_stop)} Hz)")
    for ax in (ax_gain, ax_gain_zoom):
        ax.legend(loc="lower left", fontsize=8)

    return fig


def sanitize_filename(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.+-]", "_", name)


def plot_filters(profiles: list, poly_profiles: list, halfbands: dict, args) -> None:
    try:
        import matplotlib

        if not args.plot:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is not installed; skipping plots (pip install matplotlib)", file=sys.stderr)
        return

    groups = collect_plot_groups(profiles, poly_profiles, halfbands)
    groups = filter_plot_groups(groups, args.filter)
    if not groups:
        print("no filter matched --filter patterns", file=sys.stderr)
        return

    if args.save_fig:
        os.makedirs(args.save_fig, exist_ok=True)
    for i, group in enumerate(groups):
        fig = plot_response_group(group, plt)
        if args.save_fig:
            path = os.path.join(args.save_fig, sanitize_filename(group["label"]) + ".png")
            fig.savefig(path, dpi=150)
            print(f"saved {path}")
        if not args.plot:
            plt.close(fig)
    if args.plot:
        plt.show()


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate FFT-based polyphase FIR coefficients (src/fft_fir_coef.[ch])."
    )
    parser.add_argument("--plot", action="store_true", help="show frequency response (gain/phase) figures interactively")
    parser.add_argument("--save-fig", metavar="DIR", help="save frequency response figures as PNG into DIR")
    parser.add_argument("--plot-only", action="store_true", help="do not write src/fft_fir_coef.[ch]")
    parser.add_argument(
        "--filter",
        action="append",
        default=[],
        metavar="SUBSTR",
        help="only plot filters whose name contains SUBSTR (repeatable, e.g. --filter out176400)",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)

    out_dir = os.path.join(os.path.dirname(__file__), "..", "src")
    os.makedirs(out_dir, exist_ok=True)
    header_path = os.path.join(out_dir, "fft_fir_coef.h")
    source_path = os.path.join(out_dir, "fft_fir_coef.c")

    profiles = []
    maxima = {
        "phase_len": 0,
        "head_parts": 0,
        "tail_parts": 0,
        "up_ratio": 0,
        "head_block_len": 0,
        "tail_block_len": 0,
        "head_fft_len": 0,
        "tail_fft_len": 0,
        "poly_taps": 0,
        "poly_phase_len": 0,
    }

    (
        hb44_even,
        hb44_center,
        hb44_center_index,
        hb44_taps,
        hb44_beta,
        hb44_stop_att,
        hb44_fir,
    ) = design_halfband(352800.0, HB_44_PASS_HZ, HB_44_STOP_HZ, HB_STOP_ATTEN_DB)
    (
        hb48_even,
        hb48_center,
        hb48_center_index,
        hb48_taps,
        hb48_beta,
        hb48_stop_att,
        hb48_fir,
    ) = design_halfband(384000.0, HB_48_PASS_HZ, HB_48_STOP_HZ, HB_STOP_ATTEN_DB)
    (
        hb44_hi_even,
        hb44_hi_center,
        hb44_hi_center_index,
        hb44_hi_taps,
        hb44_hi_beta,
        hb44_hi_stop_att,
        hb44_hi_fir,
    ) = design_halfband(705600.0, HB_44_HI_PASS_HZ, HB_44_HI_STOP_HZ, HB_STOP_ATTEN_DB)
    (
        hb48_hi_even,
        hb48_hi_center,
        hb48_hi_center_index,
        hb48_hi_taps,
        hb48_hi_beta,
        hb48_hi_stop_att,
        hb48_hi_fir,
    ) = design_halfband(768000.0, HB_48_HI_PASS_HZ, HB_48_HI_STOP_HZ, HB_STOP_ATTEN_DB)

    halfbands = {
        "halfband_44": {
            "fs_out": 352800.0,
            "f_pass": HB_44_PASS_HZ,
            "f_stop": HB_44_STOP_HZ,
            "taps": hb44_fir,
        },
        "halfband_48": {
            "fs_out": 384000.0,
            "f_pass": HB_48_PASS_HZ,
            "f_stop": HB_48_STOP_HZ,
            "taps": hb48_fir,
        },
        "halfband_44_hi": {
            "fs_out": 705600.0,
            "f_pass": HB_44_HI_PASS_HZ,
            "f_stop": HB_44_HI_STOP_HZ,
            "taps": hb44_hi_fir,
        },
        "halfband_48_hi": {
            "fs_out": 768000.0,
            "f_pass": HB_48_HI_PASS_HZ,
            "f_stop": HB_48_HI_STOP_HZ,
            "taps": hb48_hi_fir,
        },
    }

    add_profiles(CORE0_SPECS, "", profiles, maxima)
    add_profiles(CORE1_SPECS, "core1_", profiles, maxima)
    poly_profiles = []
    add_poly_profiles(CORE1_POLY_SPECS, poly_profiles, maxima)

    if not args.plot_only:
        write_coefficient_files(
            header_path,
            source_path,
            profiles,
            poly_profiles,
            maxima,
            hb44_even,
            hb44_center,
            hb44_center_index,
            hb44_taps,
            hb44_beta,
            hb44_stop_att,
            hb48_even,
            hb48_center,
            hb48_center_index,
            hb48_taps,
            hb48_beta,
            hb48_stop_att,
            hb44_hi_even,
            hb44_hi_center,
            hb44_hi_center_index,
            hb44_hi_taps,
            hb44_hi_beta,
            hb44_hi_stop_att,
            hb48_hi_even,
            hb48_hi_center,
            hb48_hi_center_index,
            hb48_hi_taps,
            hb48_hi_beta,
            hb48_hi_stop_att,
        )

    if args.plot or args.save_fig:
        plot_filters(profiles, poly_profiles, halfbands, args)


def write_coefficient_files(
    header_path,
    source_path,
    profiles,
    poly_profiles,
    maxima,
    hb44_even,
    hb44_center,
    hb44_center_index,
    hb44_taps,
    hb44_beta,
    hb44_stop_att,
    hb48_even,
    hb48_center,
    hb48_center_index,
    hb48_taps,
    hb48_beta,
    hb48_stop_att,
    hb44_hi_even,
    hb44_hi_center,
    hb44_hi_center_index,
    hb44_hi_taps,
    hb44_hi_beta,
    hb44_hi_stop_att,
    hb48_hi_even,
    hb48_hi_center,
    hb48_hi_center_index,
    hb48_hi_taps,
    hb48_hi_beta,
    hb48_hi_stop_att,
):
    max_fft_len = max(maxima["head_fft_len"], maxima["tail_fft_len"])
    with open(header_path, "w", encoding="utf-8") as hf:
        hf.write("/*\n")
        hf.write(" * Auto-generated by make_digitalFilter/make_fft_fir_coef.py\n")
        hf.write(" */\n\n")
        hf.write("#ifndef _FFT_FIR_COEF_H_\n")
        hf.write("#define _FFT_FIR_COEF_H_\n\n")
        hf.write("#include <stdint.h>\n\n")
        hf.write(f"#define FFT_FIR_HEAD_BLOCK_LEN ({maxima['head_block_len']})\n")
        hf.write(f"#define FFT_FIR_HEAD_FFT_LEN ({maxima['head_fft_len']})\n")
        hf.write(f"#define FFT_FIR_TAIL_BLOCK_LEN ({maxima['tail_block_len']})\n")
        hf.write(f"#define FFT_FIR_TAIL_FFT_LEN ({maxima['tail_fft_len']})\n")
        hf.write(f"#define FFT_FIR_MAX_FFT_LEN ({max_fft_len})\n")
        hf.write("#define FFT_FIR_MAX_PACKED_LEN (FFT_FIR_MAX_FFT_LEN)\n")
        hf.write(f"#define FFT_FIR_MAX_HEAD_PARTS ({max(1, maxima['head_parts'])})\n")
        hf.write(f"#define FFT_FIR_MAX_TAIL_PARTS ({max(1, maxima['tail_parts'])})\n")
        hf.write(f"#define FFT_FIR_MAX_UP_RATIO ({maxima['up_ratio']})\n")
        hf.write(f"#define FFT_FIR_MAX_PHASE_LEN ({maxima['phase_len']})\n")
        hf.write("#define FFT_FIR_MAX_INPUT (FFT_FIR_HEAD_BLOCK_LEN)\n")
        hf.write("#define FFT_FIR_MAX_OUTPUT (FFT_FIR_HEAD_BLOCK_LEN * FFT_FIR_MAX_UP_RATIO)\n\n")
        hf.write(f"#define CORE1_POLY_TAPS_MAX ({maxima['poly_taps']})\n")
        hf.write(f"#define CORE1_POLY_PHASE_LEN_MAX ({maxima['poly_phase_len']})\n\n")
        hf.write(f"#define FFT_FIR_HALF_BAND_EVEN_TAPS_44 ({hb44_even.shape[0]})\n")
        hf.write(f"#define FFT_FIR_HALF_BAND_CENTER_INDEX_44 ({hb44_center_index})\n")
        hf.write(f"#define FFT_FIR_HALF_BAND_EVEN_TAPS_48 ({hb48_even.shape[0]})\n")
        hf.write(f"#define FFT_FIR_HALF_BAND_CENTER_INDEX_48 ({hb48_center_index})\n")
        hf.write(f"#define FFT_FIR_HALF_BAND_EVEN_TAPS_44_HI ({hb44_hi_even.shape[0]})\n")
        hf.write(f"#define FFT_FIR_HALF_BAND_CENTER_INDEX_44_HI ({hb44_hi_center_index})\n")
        hf.write(f"#define FFT_FIR_HALF_BAND_EVEN_TAPS_48_HI ({hb48_hi_even.shape[0]})\n")
        hf.write(f"#define FFT_FIR_HALF_BAND_CENTER_INDEX_48_HI ({hb48_hi_center_index})\n\n")
        hf.write("typedef struct\n{\n")
        hf.write("    uint32_t fs_out_hz;\n")
        hf.write("    uint32_t passband_hz;\n")
        hf.write("    uint32_t stopband_hz;\n")
        hf.write("    uint16_t head_fft_len;\n")
        hf.write("    uint16_t head_block_len;\n")
        hf.write("    uint16_t head_parts;\n")
        hf.write("    uint16_t tail_fft_len;\n")
        hf.write("    uint16_t tail_block_len;\n")
        hf.write("    uint16_t tail_parts;\n")
        hf.write("    uint16_t up_ratio;\n")
        hf.write("    uint16_t phase_len;\n")
        hf.write("    uint16_t input_len;\n")
        hf.write("    uint16_t taps;\n")
        hf.write("    float dc_gain;\n")
        hf.write("    float gain_ratio;\n")
        hf.write("    const float *h_head_fft;\n")
        hf.write("    const float *h_tail_fft;\n")
        hf.write("} FFT_FIR_PROFILE;\n\n")

        hf.write("typedef struct\n{\n")
        hf.write("    uint32_t fs_out_hz;\n")
        hf.write("    uint32_t passband_hz;\n")
        hf.write("    uint32_t stopband_hz;\n")
        hf.write("    uint16_t up_ratio;\n")
        hf.write("    uint16_t taps;\n")
        hf.write("    uint16_t phase_len;\n")
        hf.write("    float dc_gain;\n")
        hf.write("    float gain_ratio;\n")
        hf.write("    const float *even_taps;\n")
        hf.write("    const float *odd_taps;\n")
        hf.write("} CORE1_POLY_PROFILE;\n\n")

        for profile in profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            name_id = f"{profile['name_prefix']}{spec.name}_{suffix}"
            hf.write(f"extern const float fft_fir_head_{name_id}[];\n")
            hf.write(f"extern const float fft_fir_tail_{name_id}[];\n")
        hf.write("\n")
        hf.write("extern const float fft_fir_halfband_even_44[];\n")
        hf.write("extern const float fft_fir_halfband_even_48[];\n")
        hf.write("extern const float fft_fir_halfband_center_44;\n")
        hf.write("extern const float fft_fir_halfband_center_48;\n")
        hf.write("extern const float fft_fir_halfband_even_44_hi[];\n")
        hf.write("extern const float fft_fir_halfband_even_48_hi[];\n")
        hf.write("extern const float fft_fir_halfband_center_44_hi;\n")
        hf.write("extern const float fft_fir_halfband_center_48_hi;\n")
        hf.write("\n")
        for profile in poly_profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            name_id = f"core1_poly_{spec.name}_{suffix}"
            hf.write(f"extern const float {name_id}_even[];\n")
            hf.write(f"extern const float {name_id}_odd[];\n")
        hf.write("\n")
        for profile in profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            name_id = f"{profile['name_prefix']}{spec.name}_{suffix}"
            hf.write(f"extern const FFT_FIR_PROFILE fft_fir_profile_{name_id};\n")
        for profile in poly_profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            name_id = f"core1_poly_{spec.name}_{suffix}"
            hf.write(f"extern const CORE1_POLY_PROFILE {name_id};\n")
        hf.write("\n#endif\n")

    with open(source_path, "w", encoding="utf-8") as cf:
        cf.write("/*\n")
        cf.write(" * Auto-generated by make_digitalFilter/make_fft_fir_coef.py\n")
        cf.write(" */\n\n")
        cf.write('#include "fft_fir_coef.h"\n\n')
        for profile in profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            taps_count = profile["taps_count"]
            phase_len = profile["phase_len"]
            input_len = profile["input_len"]
            stop_att = profile["stop_att"]
            head_h_fft = profile["head_h_fft"]
            tail_h_fft = profile["tail_h_fft"]
            name_id = f"{profile['name_prefix']}{spec.name}_{suffix}"
            cf.write(
                f"/* {spec.name}_{suffix}: taps={taps_count}, phase_len={phase_len}, "
                f"head_block={profile['head_block_len']}, tail_block={profile['tail_block_len']}, "
                f"tail_parts={profile['tail_parts']}, stop_att={stop_att:.2f} dB */\n"
            )
            cf.write(f"const float fft_fir_head_{name_id}[] = {{\n")
            cf.write(format_c_array(head_h_fft))
            cf.write("\n};\n\n")
            cf.write(f"const float fft_fir_tail_{name_id}[] = {{\n")
            cf.write(format_c_array(tail_h_fft))
            cf.write("\n};\n\n")

        cf.write(
            f"/* halfband_44: taps={hb44_taps}, beta={hb44_beta:.3f}, stop_att={hb44_stop_att:.2f} dB */\n"
        )
        cf.write("const float fft_fir_halfband_even_44[] = {\n")
        cf.write(format_c_array(hb44_even))
        cf.write("\n};\n\n")
        cf.write(f"const float fft_fir_halfband_center_44 = {hb44_center:.9e}f;\n\n")

        cf.write(
            f"/* halfband_48: taps={hb48_taps}, beta={hb48_beta:.3f}, stop_att={hb48_stop_att:.2f} dB */\n"
        )
        cf.write("const float fft_fir_halfband_even_48[] = {\n")
        cf.write(format_c_array(hb48_even))
        cf.write("\n};\n\n")
        cf.write(f"const float fft_fir_halfband_center_48 = {hb48_center:.9e}f;\n\n")

        cf.write(
            f"/* halfband_44_hi: taps={hb44_hi_taps}, beta={hb44_hi_beta:.3f}, stop_att={hb44_hi_stop_att:.2f} dB */\n"
        )
        cf.write("const float fft_fir_halfband_even_44_hi[] = {\n")
        cf.write(format_c_array(hb44_hi_even))
        cf.write("\n};\n\n")
        cf.write(f"const float fft_fir_halfband_center_44_hi = {hb44_hi_center:.9e}f;\n\n")

        cf.write(
            f"/* halfband_48_hi: taps={hb48_hi_taps}, beta={hb48_hi_beta:.3f}, stop_att={hb48_hi_stop_att:.2f} dB */\n"
        )
        cf.write("const float fft_fir_halfband_even_48_hi[] = {\n")
        cf.write(format_c_array(hb48_hi_even))
        cf.write("\n};\n\n")
        cf.write(f"const float fft_fir_halfband_center_48_hi = {hb48_hi_center:.9e}f;\n\n")

        for profile in poly_profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            taps_count = profile["taps_count"]
            phase_len = profile["phase_len"]
            stop_att = profile["stop_att"]
            name_id = f"core1_poly_{spec.name}_{suffix}"
            cf.write(
                f"/* {name_id}: taps={taps_count}, phase_len={phase_len}, stop_att={stop_att:.2f} dB */\n"
            )
            cf.write(f"const float {name_id}_even[] = {{\n")
            cf.write(format_c_array(profile['even_taps']))
            cf.write("\n};\n\n")
            cf.write(f"const float {name_id}_odd[] = {{\n")
            cf.write(format_c_array(profile['odd_taps']))
            cf.write("\n};\n\n")

        for profile in profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            taps_count = profile["taps_count"]
            phase_len = profile["phase_len"]
            input_len = profile["input_len"]
            name_id = f"{profile['name_prefix']}{spec.name}_{suffix}"
            cf.write(f"const FFT_FIR_PROFILE fft_fir_profile_{name_id} = {{\n")
            cf.write(f"    .fs_out_hz = {int(spec.fs_out)},\n")
            cf.write(f"    .passband_hz = {int(spec.f_pass)},\n")
            cf.write(f"    .stopband_hz = {int(spec.f_stop)},\n")
            cf.write(f"    .head_fft_len = {profile['head_fft_len']},\n")
            cf.write(f"    .head_block_len = {profile['head_block_len']},\n")
            cf.write(f"    .head_parts = {profile['head_parts']},\n")
            cf.write(f"    .tail_fft_len = {profile['tail_fft_len']},\n")
            cf.write(f"    .tail_block_len = {profile['tail_block_len']},\n")
            cf.write(f"    .tail_parts = {profile['tail_parts']},\n")
            cf.write(f"    .up_ratio = {spec.up_ratio},\n")
            cf.write(f"    .phase_len = {phase_len},\n")
            cf.write(f"    .input_len = {input_len},\n")
            cf.write(f"    .taps = {taps_count},\n")
            cf.write(f"    .dc_gain = {profile['dc_gain']:.9e}f,\n")
            cf.write(f"    .gain_ratio = {profile['gain_ratio']:.9e}f,\n")
            cf.write(f"    .h_head_fft = fft_fir_head_{name_id},\n")
            cf.write(f"    .h_tail_fft = fft_fir_tail_{name_id},\n")
            cf.write("};\n\n")

        for profile in poly_profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            taps_count = profile["taps_count"]
            phase_len = profile["phase_len"]
            name_id = f"core1_poly_{spec.name}_{suffix}"
            cf.write(f"const CORE1_POLY_PROFILE {name_id} = {{\n")
            cf.write(f"    .fs_out_hz = {int(spec.fs_out)},\n")
            cf.write(f"    .passband_hz = {int(spec.f_pass)},\n")
            cf.write(f"    .stopband_hz = {int(spec.f_stop)},\n")
            cf.write(f"    .up_ratio = {spec.up_ratio},\n")
            cf.write(f"    .taps = {taps_count},\n")
            cf.write(f"    .phase_len = {phase_len},\n")
            cf.write(f"    .dc_gain = {profile['dc_gain']:.9e}f,\n")
            cf.write(f"    .gain_ratio = {profile['gain_ratio']:.9e}f,\n")
            cf.write(f"    .even_taps = {name_id}_even,\n")
            cf.write(f"    .odd_taps = {name_id}_odd,\n")
            cf.write("};\n\n")


if __name__ == "__main__":
    main()
