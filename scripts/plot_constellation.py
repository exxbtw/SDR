import numpy as np
import matplotlib.pyplot as plt

NAME = "/Users/user/VSCODE/SDR/dev/build/txdata.pcm"
SPS = 10 # отсчетов на символ
MTU = 1920 # размер буфера

CHUNK = MTU # комплексных отсчетов одного буфера в файле (без заголовка)

d = np.fromfile(NAME, dtype="<i2")
d = d[: len(d) // 2 * 2]
i_all, q_all = d[0::2].astype(float), d[1::2].astype(float)

n_buffers = len(i_all) // CHUNK
print(f"Файл: {NAME}")
print(f"Отсчетов: {len(i_all)}, буферов: {n_buffers}, макс |I| = {np.abs(i_all).max():.0f}, макс |Q| = {np.abs(q_all).max():.0f}")
print("Модуляция:", "BPSK (Q = 0)" if np.abs(q_all).max() == 0 else "QPSK (Q != 0)")

# весь файл целиком, обрезаем только неполный буфер в конце
i = i_all[:n_buffers * CHUNK]
q = q_all[:n_buffers * CHUNK]

n_sym = len(i) // SPS
idx = np.arange(n_sym) * SPS + SPS // 2
sym_i, sym_q = i[idx], q[idx]

fig = plt.figure(figsize=(12, 9), layout="constrained")
gs = fig.add_gridspec(2, 2, height_ratios=[1.3, 1.6])
ax1 = fig.add_subplot(gs[0, :]) # сигнал по времени
ax2 = fig.add_subplot(gs[1, 0]) # scatter всех отсчетов
ax3 = fig.add_subplot(gs[1, 1]) # созвездие (по одному отсчету на символ)

amp = max(np.abs(i).max(), np.abs(q).max(), 1)
lim = amp * 1.5

#сигнал по времени (весь файл)
x = np.arange(len(i))
ax1.plot(x, i, color="blue", lw=0.4, label="I")
ax1.plot(x, q, color="red", lw=0.4, label="Q")
ax1.set_ylim(-amp * 1.3, amp * 1.3)
ax1.set_xlim(0, len(i))
ax1.set_title(f"Сигнал: весь файл ({n_buffers} буферов, {len(i)} отсчетов)")
ax1.set_xlabel("отсчет")
ax1.set_ylabel("амплитуда")
ax1.grid(True, alpha=0.3)
ax1.legend(loc="upper right")

def setup_iq(ax, title):
    ax.set_xlim(-lim, lim)
    ax.set_ylim(-lim, lim)
    ax.set_aspect("equal")
    ax.axhline(0, color="black", lw=0.8)
    ax.axvline(0, color="black", lw=0.8)
    ax.set_title(title)
    ax.set_xlabel("I")
    ax.set_ylabel("Q")
    ax.grid(True, alpha=0.3)

#plt.scatter(real, imag)
ax2.scatter(i, q, s=1, alpha=0.05, color="blue", rasterized=True)
setup_iq(ax2, f"scatter(real, imag): все отсчеты ({len(i)})")

#созвездие: один отсчет из середины каждого символа
ax3.scatter(sym_i, sym_q, s=2, alpha=0.1, color="green", rasterized=True)
setup_iq(ax3, f"Созвездие: по символам ({n_sym})")

plt.show()