# ライブラリのインポート
import numpy as np
from control.matlab import *
from control import tf, c2d
from scipy import signal
from scipy import interpolate
import matplotlib.pyplot as plt

# フィルタの基本条件
FS = 96000                  # アップサンプリング前の周波数[Hz]
UPSAMPLING_RATIO = 2        # アップサンプリング倍率

# フィルタのパラメータ
N_TAP = 48                 # FIRフィルタのタップ数
FIR_KAISER_WINDOW = 40000   # カイザー窓の大きさ
FREQ_CUTOFF = 24000         # カットオフ周波数[Hz]

# シミュレーション条件
TIME_IMPULSE_SIMU = 0.0012  # インパルス応答解析時間[s]

# C言語の配列定義の形で出力する
def print_filter_params(param_, type):
    param = np.copy(param_)
    if type == 'sos':
        print('\nbqfilter_coef[] = \n{', end='')
        for i in range(param.shape[0]):
            print('{', end='')
            for j in range(param.shape[1]):
                if j != param.shape[1] - 1:
                    print(param[i][j], end=', ')
                else:
                    print(param[i][j], end='')

            if i != param.shape[0] - 1:
                print('},')
            else:
                print('}', end='')
        print('};')
    
    if type == 'iir':
        print('\niir filter_coef[] = \n{', end='')
        for i in range(param.shape[0]):
            print('{', end='')
            for j in range(param.shape[1]):
                if j != param.shape[1] - 1:
                    print(param[i][j], end=', ')
                else:
                    print(param[i][j], end='')

            if i != param.shape[0] - 1:
                print('},')
            else:
                print('}', end='')
        print('};')

    if type == 'fir':
            count = 0
            print('\niir filter_coef[] = \n{', end='')
            for i in range(param.shape[0]):
                if i != param.shape[0] - 1:
                    print(param[i], end=', ')
                else:
                    print(param[i], end='')
                count += 1
                if (count > 5) & (i != param.shape[0] - 1):
                    print('\n', end='')
                    count = 0
            print('};')

# FIRフィルタを設計する
nyq = 0.5 * FS * UPSAMPLING_RATIO
cutoff = FREQ_CUTOFF
atten = signal.kaiser_atten(N_TAP,  FIR_KAISER_WINDOW / nyq)
beta = signal.kaiser_beta(atten)

b1 = signal.firwin(N_TAP, cutoff, window=('kaiser', beta), fs=FS * UPSAMPLING_RATIO)

# インパルス応答用の入力データを作成
inpulse_t = np.arange(0, TIME_IMPULSE_SIMU, 1/FS)
impulse_y = signal.unit_impulse(inpulse_t.shape)
impulse_y_mormalized = impulse_y / impulse_y.max()

# インパルスデータをアップサンプリング用に補間処理
interp_func = interpolate.interp1d(inpulse_t, impulse_y_mormalized, kind='previous', fill_value='extrapolate')
impulse_t_interp = np.arange(0, TIME_IMPULSE_SIMU, 1/FS/UPSAMPLING_RATIO)
impulse_y_interp = interp_func(impulse_t_interp)
indata = np.array([impulse_t_interp, impulse_y_interp])

# インパルス応答を計算
x = indata[1]
y = signal.lfilter(b1, 1.0, x)

# 周波数応答を算出
w, h = signal.freqz(b1, 1.0, fs=FS * UPSAMPLING_RATIO)

amp_dB = 20 * np.log10(np.abs(h))
angles = np.unwrap(np.angle(h))
freq = w

# Figureを作成する(インパルス応答)
fig1 =plt.figure()

# Axesを作成する
ax1 = fig1.add_subplot(111)
ax1.plot(indata[0], y, label="result")

# グリッドを表示する
ax1.grid(True, "major", linestyle="-", linewidth=.7)
ax1.grid(True, "minor", "x", linestyle="-", linewidth=.3)

# フィルタ係数を出力 （CMSISが期待する並び）
print_filter_params(b1, 'fir')

# Figureを作成する(周波数応答)
fig2 = plt.figure()

# Axesを作成する
ax2 = fig2.add_subplot(2,1,1)
ax2.plot(freq, amp_dB, 'b')

# グリッドを表示する
ax2.grid(True, "major", linestyle="-", linewidth=.7)
ax2.grid(True, "minor", "x", linestyle="-", linewidth=.3)

# x軸を対数目盛に設定する
ax2.set_xscale('log')

ax3 = fig2.add_subplot(2,1,2)
ax3.plot(freq, angles, 'r')

# x軸を対数目盛に設定する
ax3.set_xscale('log')

# グリッドを表示する
ax3.grid(True, "major", linestyle="-", linewidth=.7)
ax3.grid(True, "minor", "x", linestyle="-", linewidth=.3)

# グラフを表示する
plt.show()