import os
from dataclasses import dataclass

import numpy as np
from scipy import signal

FFT_N = 512
TARGET_ATTEN_DB = 145.0
FREQZ_POINTS = 65536


@dataclass
class Spec:
    name: str
    fs_out: float
    f_pass: float
    f_stop: float
    up_ratio: int
    atten_hp_db: float = TARGET_ATTEN_DB
    atten_lp_db: float = 110.0

    @property
    def max_taps(self) -> int:
        return self.up_ratio * FFT_N


SPECS = [
    Spec("176400_22050_u4", 176400.0, 20000.0, 28000.0, 4, 140.0, 120.0),
    Spec("176400_24000_u2", 176400.0, 20000.0, 28000.0, 2, 140.0, 120.0),
    Spec("192000_24000_u4", 192000.0, 20000.0, 28000.0, 4, 140.0, 120.0),
    Spec("192000_24000_u2", 192000.0, 20000.0, 28000.0, 2, 140.0, 120.0),
    Spec("352800_22050_u8", 352800.0, 20000.0, 28000.0, 8, 140.0, 120.0),
    Spec("352800_22050_u4", 352800.0, 20000.0, 28000.0, 4, 140.0, 120.0),
    Spec("352800_24000_u4", 352800.0, 20000.0, 28000.0, 4, 140.0, 120.0),
    Spec("352800_24000_u2", 352800.0, 20000.0, 28000.0, 2, 140.0, 120.0),
    Spec("384000_24000_u8", 384000.0, 20000.0, 28000.0, 8, 140.0, 120.0),
    Spec("384000_24000_u4", 384000.0, 20000.0, 28000.0, 4, 140.0, 120.0),
    Spec("384000_24000_u2", 384000.0, 20000.0, 28000.0, 2, 140.0, 120.0),
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


def design_filter(spec: Spec, atten_db: float):
    width = spec.f_stop - spec.f_pass
    norm_width = width / (spec.fs_out / 2.0)
    taps_guess, beta = signal.kaiserord(atten_db, norm_width)
    taps_guess = max(2, int(taps_guess))
    taps_guess = align_to_multiple(taps_guess, spec.up_ratio)
    taps_guess = min(taps_guess, spec.max_taps)

    best = None
    for taps_count in range(taps_guess, spec.max_taps + 1, spec.up_ratio):
        taps = signal.firwin(taps_count, spec.f_pass, window=("kaiser", beta), fs=spec.fs_out)
        taps *= spec.up_ratio
        stop_att = measure_stop_atten_db(taps, spec.fs_out, spec.f_stop)
        if best is None or stop_att > best[2]:
            best = (taps, beta, stop_att)
        if stop_att >= atten_db:
            return taps, beta, stop_att

    return best


def build_h_fft(taps: np.ndarray, up_ratio: int) -> np.ndarray:
    phase_len = taps.shape[0] // up_ratio
    h_fft = np.zeros((up_ratio, FFT_N * 2), dtype=np.float32)
    for phase in range(up_ratio):
        phase_taps = taps[phase::up_ratio]
        if phase_taps.shape[0] != phase_len:
            raise ValueError("phase length mismatch")
        padded = np.zeros(FFT_N, dtype=np.float32)
        padded[:phase_len] = phase_taps.astype(np.float32)
        H = np.fft.fft(padded)
        h_fft[phase, 0::2] = H.real.astype(np.float32)
        h_fft[phase, 1::2] = H.imag.astype(np.float32)
    return h_fft


def calc_dc_gain(h_fft: np.ndarray) -> float:
    dc_vals = h_fft[:, 0]
    return float(np.mean(dc_vals))


def format_c_array(values: np.ndarray, indent: str = "    ", per_line: int = 4) -> str:
    flat = values.flatten()
    lines = []
    for i in range(0, flat.shape[0], per_line):
        chunk = flat[i : i + per_line]
        line = indent + ", ".join(f"{v:.9e}f" for v in chunk)
        lines.append(line)
    return ",\n".join(lines)


def main():
    out_dir = os.path.join(os.path.dirname(__file__), "..", "src")
    os.makedirs(out_dir, exist_ok=True)
    header_path = os.path.join(out_dir, "fft_fir_coef.h")
    source_path = os.path.join(out_dir, "fft_fir_coef.c")

    profiles = []
    max_phase_len = 0
    max_input_len = 0
    max_output_len = 0

    for spec in SPECS:
        for suffix, atten in (("hp", spec.atten_hp_db), ("lp", spec.atten_lp_db)):
            result = design_filter(spec, atten)
            if result is None:
                raise RuntimeError(f"Failed to design filter for {spec.name}_{suffix}")
            taps, beta, stop_att = result
            taps_count = taps.shape[0]
            if taps_count % spec.up_ratio != 0:
                raise RuntimeError("tap count not divisible by up_ratio")
            phase_len = taps_count // spec.up_ratio
            input_len = FFT_N - (phase_len - 1)
            if input_len <= 0:
                raise RuntimeError("input_len must be positive")
            h_fft = build_h_fft(taps, spec.up_ratio)
            dc_gain = calc_dc_gain(h_fft)
            gain_ratio = 1.0 / dc_gain if abs(dc_gain) > 1e-9 else 1.0
            profiles.append(
                {
                    "spec": spec,
                    "suffix": suffix,
                    "taps_count": taps_count,
                    "phase_len": phase_len,
                    "input_len": input_len,
                    "stop_att": stop_att,
                    "h_fft": h_fft,
                    "dc_gain": dc_gain,
                    "gain_ratio": gain_ratio,
                }
            )
            max_phase_len = max(max_phase_len, phase_len)
            max_input_len = max(max_input_len, input_len)
            max_output_len = max(max_output_len, input_len * spec.up_ratio)

    with open(header_path, "w", encoding="utf-8") as hf:
        hf.write("/*\n")
        hf.write(" * Auto-generated by make_digitalFilter/make_fft_fir_coef.py\n")
        hf.write(" */\n\n")
        hf.write("#ifndef _FFT_FIR_COEF_H_\n")
        hf.write("#define _FFT_FIR_COEF_H_\n\n")
        hf.write("#include <stdint.h>\n\n")
        hf.write(f"#define FFT_FIR_N ({FFT_N})\n")
        hf.write(f"#define FFT_FIR_MAX_PHASE_LEN ({max_phase_len})\n")
        hf.write(f"#define FFT_FIR_MAX_INPUT ({max_input_len})\n")
        hf.write(f"#define FFT_FIR_MAX_OUTPUT ({max_output_len})\n\n")
        hf.write("typedef struct\n{\n")
        hf.write("    uint32_t fs_out_hz;\n")
        hf.write("    uint32_t passband_hz;\n")
        hf.write("    uint32_t stopband_hz;\n")
        hf.write("    uint16_t up_ratio;\n")
        hf.write("    uint16_t phase_len;\n")
        hf.write("    uint16_t input_len;\n")
        hf.write("    uint16_t taps;\n")
        hf.write("    float dc_gain;\n")
        hf.write("    float gain_ratio;\n")
        hf.write("    const float *h_fft;\n")
        hf.write("} FFT_FIR_PROFILE;\n\n")

        for profile in profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            hf.write(f"extern const float fft_fir_h_{spec.name}_{suffix}[];\n")
        hf.write("\n")
        for profile in profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            hf.write(f"extern const FFT_FIR_PROFILE fft_fir_profile_{spec.name}_{suffix};\n")
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
            h_fft = profile["h_fft"]
            cf.write(f"/* {spec.name}_{suffix}: taps={taps_count}, phase_len={phase_len}, input_len={input_len}, stop_att={stop_att:.2f} dB */\n")
            cf.write(f"const float fft_fir_h_{spec.name}_{suffix}[] = {{\n")
            cf.write(format_c_array(h_fft))
            cf.write("\n};\n\n")

        for profile in profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            taps_count = profile["taps_count"]
            phase_len = profile["phase_len"]
            input_len = profile["input_len"]
            cf.write(f"const FFT_FIR_PROFILE fft_fir_profile_{spec.name}_{suffix} = {{\n")
            cf.write(f"    .fs_out_hz = {int(spec.fs_out)},\n")
            cf.write(f"    .passband_hz = {int(spec.f_pass)},\n")
            cf.write(f"    .stopband_hz = {int(spec.f_stop)},\n")
            cf.write(f"    .up_ratio = {spec.up_ratio},\n")
            cf.write(f"    .phase_len = {phase_len},\n")
            cf.write(f"    .input_len = {input_len},\n")
            cf.write(f"    .taps = {taps_count},\n")
            cf.write(f"    .dc_gain = {profile['dc_gain']:.9e}f,\n")
            cf.write(f"    .gain_ratio = {profile['gain_ratio']:.9e}f,\n")
            cf.write(f"    .h_fft = fft_fir_h_{spec.name}_{suffix},\n")
            cf.write("};\n\n")


if __name__ == "__main__":
    main()
