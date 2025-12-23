import os
from dataclasses import dataclass

import numpy as np
from scipy import signal

HEAD_BLOCK_LEN = 128
TAIL_BLOCK_LEN = 256
HEAD_FFT_LEN = HEAD_BLOCK_LEN * 2
TAIL_FFT_LEN = TAIL_BLOCK_LEN * 2
MAX_TAIL_PARTS = 1
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
    max_phase_len = HEAD_BLOCK_LEN + (TAIL_BLOCK_LEN * MAX_TAIL_PARTS)
    max_taps = spec.up_ratio * max_phase_len
    taps_guess = min(taps_guess, max_taps)

    best = None
    for taps_count in range(taps_guess, max_taps + 1, spec.up_ratio):
        taps = signal.firwin(taps_count, spec.f_pass, window=("kaiser", beta), fs=spec.fs_out)
        taps *= spec.up_ratio
        stop_att = measure_stop_atten_db(taps, spec.fs_out, spec.f_stop)
        if best is None or stop_att > best[2]:
            best = (taps, beta, stop_att)
        if stop_att >= atten_db:
            return taps, beta, stop_att

    return best


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


def build_head_tail_fft(taps: np.ndarray, up_ratio: int) -> tuple:
    phase_len = taps.shape[0] // up_ratio
    if phase_len <= 0:
        raise ValueError("phase length must be positive")
    tail_len = max(0, phase_len - HEAD_BLOCK_LEN)
    tail_parts = int(np.ceil((HEAD_BLOCK_LEN + tail_len) / TAIL_BLOCK_LEN)) if tail_len > 0 else 0

    head_h_fft = np.zeros((up_ratio, 1, HEAD_FFT_LEN), dtype=np.float32)
    tail_h_fft = np.zeros((up_ratio, max(1, tail_parts), TAIL_FFT_LEN), dtype=np.float32)

    for phase in range(up_ratio):
        phase_taps = taps[phase::up_ratio]
        if phase_taps.shape[0] != phase_len:
            raise ValueError("phase length mismatch")
        head_taps = phase_taps[:HEAD_BLOCK_LEN]
        head_part, _ = build_partition_fft(head_taps, HEAD_BLOCK_LEN, HEAD_FFT_LEN)
        head_h_fft[phase, 0, :] = head_part[0]

        if tail_len > 0:
            tail_taps = phase_taps[HEAD_BLOCK_LEN:]
            if tail_taps.shape[0] != tail_len:
                raise ValueError("tail length mismatch")
            tail_taps = np.concatenate([np.zeros(HEAD_BLOCK_LEN, dtype=np.float32), tail_taps.astype(np.float32)])
            tail_part, parts = build_partition_fft(tail_taps, TAIL_BLOCK_LEN, TAIL_FFT_LEN)
            if parts != tail_parts:
                raise ValueError("tail parts mismatch")
            tail_h_fft[phase, :parts, :] = tail_part

    return head_h_fft, tail_h_fft, tail_parts


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


def main():
    out_dir = os.path.join(os.path.dirname(__file__), "..", "src")
    os.makedirs(out_dir, exist_ok=True)
    header_path = os.path.join(out_dir, "fft_fir_coef.h")
    source_path = os.path.join(out_dir, "fft_fir_coef.c")

    profiles = []
    max_phase_len = 0
    max_head_parts = 0
    max_tail_parts = 0
    max_up_ratio = 0

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
            input_len = HEAD_BLOCK_LEN
            head_h_fft, tail_h_fft, tail_parts = build_head_tail_fft(taps, spec.up_ratio)
            dc_gain = calc_dc_gain(head_h_fft, tail_h_fft, tail_parts)
            gain_ratio = 1.0 / dc_gain if abs(dc_gain) > 1e-9 else 1.0
            profiles.append(
                {
                    "spec": spec,
                    "suffix": suffix,
                    "head_fft_len": HEAD_FFT_LEN,
                    "head_block_len": HEAD_BLOCK_LEN,
                    "head_parts": 1,
                    "tail_fft_len": TAIL_FFT_LEN,
                    "tail_block_len": TAIL_BLOCK_LEN,
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
            max_phase_len = max(max_phase_len, phase_len)
            max_head_parts = max(max_head_parts, 1)
            max_tail_parts = max(max_tail_parts, tail_parts)
            max_up_ratio = max(max_up_ratio, spec.up_ratio)

    with open(header_path, "w", encoding="utf-8") as hf:
        hf.write("/*\n")
        hf.write(" * Auto-generated by make_digitalFilter/make_fft_fir_coef.py\n")
        hf.write(" */\n\n")
        hf.write("#ifndef _FFT_FIR_COEF_H_\n")
        hf.write("#define _FFT_FIR_COEF_H_\n\n")
        hf.write("#include <stdint.h>\n\n")
        hf.write(f"#define FFT_FIR_HEAD_BLOCK_LEN ({HEAD_BLOCK_LEN})\n")
        hf.write(f"#define FFT_FIR_HEAD_FFT_LEN ({HEAD_FFT_LEN})\n")
        hf.write(f"#define FFT_FIR_TAIL_BLOCK_LEN ({TAIL_BLOCK_LEN})\n")
        hf.write(f"#define FFT_FIR_TAIL_FFT_LEN ({TAIL_FFT_LEN})\n")
        hf.write("#define FFT_FIR_MAX_FFT_LEN (FFT_FIR_TAIL_FFT_LEN)\n")
        hf.write("#define FFT_FIR_MAX_PACKED_LEN (FFT_FIR_MAX_FFT_LEN)\n")
        hf.write(f"#define FFT_FIR_MAX_HEAD_PARTS ({max(1, max_head_parts)})\n")
        hf.write(f"#define FFT_FIR_MAX_TAIL_PARTS ({max(1, max_tail_parts)})\n")
        hf.write(f"#define FFT_FIR_MAX_UP_RATIO ({max_up_ratio})\n")
        hf.write(f"#define FFT_FIR_MAX_PHASE_LEN ({max_phase_len})\n")
        hf.write("#define FFT_FIR_MAX_INPUT (FFT_FIR_HEAD_BLOCK_LEN)\n")
        hf.write("#define FFT_FIR_MAX_OUTPUT (FFT_FIR_HEAD_BLOCK_LEN * FFT_FIR_MAX_UP_RATIO)\n\n")
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

        for profile in profiles:
            spec = profile["spec"]
            suffix = profile["suffix"]
            hf.write(f"extern const float fft_fir_head_{spec.name}_{suffix}[];\n")
            hf.write(f"extern const float fft_fir_tail_{spec.name}_{suffix}[];\n")
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
            head_h_fft = profile["head_h_fft"]
            tail_h_fft = profile["tail_h_fft"]
            cf.write(
                f"/* {spec.name}_{suffix}: taps={taps_count}, phase_len={phase_len}, "
                f"head_block={profile['head_block_len']}, tail_block={profile['tail_block_len']}, "
                f"tail_parts={profile['tail_parts']}, stop_att={stop_att:.2f} dB */\n"
            )
            cf.write(f"const float fft_fir_head_{spec.name}_{suffix}[] = {{\n")
            cf.write(format_c_array(head_h_fft))
            cf.write("\n};\n\n")
            cf.write(f"const float fft_fir_tail_{spec.name}_{suffix}[] = {{\n")
            cf.write(format_c_array(tail_h_fft))
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
            cf.write(f"    .h_head_fft = fft_fir_head_{spec.name}_{suffix},\n")
            cf.write(f"    .h_tail_fft = fft_fir_tail_{spec.name}_{suffix},\n")
            cf.write("};\n\n")


if __name__ == "__main__":
    main()
