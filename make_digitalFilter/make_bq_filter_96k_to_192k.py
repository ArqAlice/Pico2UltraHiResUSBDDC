import numpy as np
from control.matlab import *
from control import tf, c2d
from scipy import signal
from scipy import interpolate
import matplotlib.pyplot as plt

import os
import csv

PI = np.pi

FS = 96000
#N_TAP = 384
#FIR_KAISER_WINDOW = 9000
OVERSAMPLING_RATIO = 2
TS_OVERSAMPLING = 1 / (FS * OVERSAMPLING_RATIO)
#N_FIR_MULTIPLE = 1
#N_IIR_MULTIPLE = 1
N_BQ_MULTIPLE = 1

class FIR_filter:
    def __init__(self, h_array_):
        self.h_array = np.copy(h_array_)
    
    def FIR_filter_time_domain(self, n, x):
        y = x
        for i in range(n):
            y = signal.lfilter(self.h_array, 1.0, y)
        return y
    def FIR_filter_freq_domain(self, n, fs):
        w, h = signal.freqz(self.h_array, 1.0, fs=fs)
        h_out = h ** n
        return w, h_out

class IIR_filter:
    def __init__(self, b_, a_):
        self.b = np.copy(b_)
        self.a = np.copy(a_)
    
    def IIR_filter_time_domain(self, n, x):
        y = x
        for i in range(n):
            y = signal.lfilter(self.b, self.a, y)
        return y
    
    def IIR_filter_freq_domain(self, n, fs):
        w, h = signal.freqz(self.b, self.a, fs=fs)
        h_out = h ** n
        return w, h_out

class BQ_filter:
    def __init__(self, sos_):
        self.sos = np.copy(sos_)
    
    def BQfilter_time_domein(self, n, x):
        y = x
        for i in range(n):
            y = signal.sosfilt(self.sos, x)
        return y
    
    def BQ_filter_freq_domein(self, n, fs):
        w, h = signal.sosfreqz(self.sos, fs=fs)
        h_out = h ** n
        return w, h_out

# C言語の配列定義の形で出力する
def print_filter_params(param_, type):
    param = np.copy(param_)
    if type == 'sos':
        print('\nbqfilter_coef = \n{', end='')
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
        print('\niir filter_coef = \n{', end='')
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
            print('\niir filter_coef = \n{', end='')
            for i in range(param.shape[0]):
                if i != param.shape[0] - 1:
                    print(param[i], end=', ')
                else:
                    print(param[i], end='')
            print('};')
    
            
# FIRフィルタを設計する
'''
nyq = 0.5 * FS * OVERSAMPLING_RATIO
cutoff = FS * 0.51
atten = signal.kaiser_atten(N_TAP,  FIR_KAISER_WINDOW / nyq)
beta = signal.kaiser_beta(atten)

b1 = signal.firwin(N_TAP, cutoff, nyq=nyq, window=('kaiser', beta))
fir = FIR_filter(b1)
'''

# IIRフィルタを設計する
wp1 = FS * 0.22   # 通過域遮断周波数[Hz]
ws1 = FS * 0.57 # 阻止域遮断周波数[Hz]
gpass1 = 0.5     # 通過域最大損失量[dB]
gstop1 = 150     # 阻止域最小減衰量[dB]
ftype = 'cheby2'# chebyshev2
fs = FS * OVERSAMPLING_RATIO

#b2, a2 = signal.iirdesign(wp1, ws1, gpass1, gstop1, output='ba', ftype=ftype, fs=fs)
#iir = IIR_filter(b2, a2)

# BQフィルタを設計する
sos = signal.iirdesign(wp1, ws1, gpass1, gstop1, output='sos', ftype=ftype, fs=fs)
bq = BQ_filter(sos)

# 入力データCSVを読み込み
with open('impulseInput_44.1kHz.csv') as f_input:
    reader = csv.reader(f_input)
    indata_read = np.array([row for row in reader]).T
    indata0 = indata_read.astype('double')

# 入力データをリサンプリング
f2_x = np.arange(0, 0.0024, TS_OVERSAMPLING).T
f2 = interpolate.interp1d(indata0[0], indata0[1], fill_value='extrapolate')
f2_y = f2(f2_x)
f2_y_normalized = f2_y / f2_y.max()
indata = np.array([f2_x, f2_y_normalized])

# 時間軸応答を算出
x = indata[1]
#y = fir.FIR_filter_time_domain(N_FIR_MULTIPLE, x)
#y = iir.IIR_filter_time_domain(N_IIR_MULTIPLE, y)
y = bq.BQfilter_time_domein(N_BQ_MULTIPLE, x)

# 周波数応答を算出
#w, h1 = fir.FIR_filter_freq_domain(N_FIR_MULTIPLE, FS * OVERSAMPLING_RATIO)
#_, h2 = iir.IIR_filter_freq_domain(N_IIR_MULTIPLE, FS * OVERSAMPLING_RATIO)
w, h3 = bq.BQ_filter_freq_domein(N_BQ_MULTIPLE, FS * OVERSAMPLING_RATIO)

#h = h1 * h2
#h = h1
h = h3

amp_dB = 20 * np.log10(np.abs(h))
angles = np.unwrap(np.angle(h))
freq = w

# Figureを作成する。
fig1 =plt.figure()

# Axesを作成する。
ax1 = fig1.add_subplot(111)

ax1.plot(indata[0], y, label="result")

# グリッドを表示する。
ax1.grid(True, "major", linestyle="-", linewidth=.7)
ax1.grid(True, "minor", "x", linestyle="-", linewidth=.3)

# フィルタ係数を出力
print_filter_params(sos, 'sos')

# Figureを作成する。
fig2 = plt.figure()

# Axesを作成する。
ax2 = fig2.add_subplot(2,1,1)
ax2.plot(freq, amp_dB, 'b')

# グリッドを表示する。
ax2.grid(True, "major", linestyle="-", linewidth=.7)
ax2.grid(True, "minor", "x", linestyle="-", linewidth=.3)

# x軸を対数目盛に設定する。
ax2.set_xscale('log')

ax3 = fig2.add_subplot(2,1,2)
ax3.plot(freq, angles, 'r')

# x軸を対数目盛に設定する。
ax3.set_xscale('log')

# グリッドを表示する。
ax3.grid(True, "major", linestyle="-", linewidth=.7)
ax3.grid(True, "minor", "x", linestyle="-", linewidth=.3)

# グラフを表示する。
plt.show()

#plt.semilogx(freq, amp_dB, 'b')
#plt.semilogx(omega, angles, 'g')
#plt.grid()

#plt.show()