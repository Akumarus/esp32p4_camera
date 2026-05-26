# import numpy as np
# from PIL import Image

# WIDTH = 800
# HEIGHT = 1280

# with open('frame.bin', 'rb') as f:
#     data = f.read()

# print(f"File size: {len(data)} bytes")

# # Пробуем как RGB565 с разным порядком байт
# for name, order in [('LE', 'little'), ('BE', 'big')]:
#     pixels = []
#     for i in range(0, len(data), 2):
#         if order == 'little':
#             rgb565 = data[i] | (data[i+1] << 8)
#         else:
#             rgb565 = (data[i] << 8) | data[i+1]
        
#         r = (rgb565 >> 11) & 0x1F
#         g = (rgb565 >> 5) & 0x3F
#         b = rgb565 & 0x1F
        
#         r = (r << 3) | (r >> 2)
#         g = (g << 2) | (g >> 4)
#         b = (b << 3) | (b >> 2)
        
#         pixels.extend([r, g, b])
    
#     img_array = np.array(pixels, dtype=np.uint8).reshape(HEIGHT, WIDTH, 3)
#     img = Image.fromarray(img_array)
#     img.save(f'frame_{name}.jpg')
#     print(f"Saved frame_{name}.jpg")

# # Показываем первые байты
# print(f"First 32 bytes: {' '.join(f'{b:02x}' for b in data[:32])}")

import numpy as np
import cv2
from PIL import Image

WIDTH = 800
HEIGHT = 1280

with open('frame.bin', 'rb') as f:
    data = f.read()

print(f"File size: {len(data)} bytes")

# Конвертация 16-bit LE -> 10-bit RAW
raw_10bit = []
for i in range(0, len(data), 2):
    if i + 1 < len(data):
        val = data[i] | (data[i+1] << 8)
        # IMX219: младшие 10 бит значащие
        raw_10bit.append((val & 0x3FF) >> 2)  # 10->8 bit
    else:
        raw_10bit.append(0)

# Создаем байеровскую матрицу
bayer = np.array(raw_10bit, dtype=np.uint8).reshape(HEIGHT, WIDTH)

# Сохраняем сырой байер (должен выглядеть как зеленоватое монохромное)
Image.fromarray(bayer, mode='L').save('bayer_raw.jpg')
print("Saved bayer_raw.jpg - должно быть монохромным с текстурой")

# Для IMX219 правильный порядок Bayer - RGGB
# Пробуем оба варианта (RGGB и BGGR)
bayer_patterns = {
    'RGGB': cv2.COLOR_BayerRG2RGB,
    'BGGR': cv2.COLOR_BayerBG2RGB,
}

for pattern_name, code in bayer_patterns.items():
    # Демазайсинг
    rgb = cv2.cvtColor(bayer, code)
    
    # Баланс белого для Sony IMX219 (примерные коэффициенты)
    # Убираем сильный зеленый фон байера
    r, g, b = cv2.split(rgb)
    
    # Автоматическая коррекция баланса белого
    r_mean = np.mean(r[r > 10])
    g_mean = np.mean(g[g > 10])
    b_mean = np.mean(b[b > 10])
    
    # Применяем коррекцию
    r_corrected = np.clip(r * (g_mean / r_mean), 0, 255).astype(np.uint8)
    g_corrected = g
    b_corrected = np.clip(b * (g_mean / b_mean), 0, 255).astype(np.uint8)
    
    rgb_corrected = cv2.merge([r_corrected, g_corrected, b_corrected])
    
    # Сохраняем
    cv2.imwrite(f'frame_{pattern_name}.jpg', rgb_corrected)
    print(f"Saved frame_{pattern_name}.jpg")
    
    # Контраст и яркость (если слишком темно)
    # Повышаем яркость если нужно
    if rgb_corrected.mean() < 100:
        enhanced = cv2.convertScaleAbs(rgb_corrected, alpha=1.5, beta=30)
        cv2.imwrite(f'frame_{pattern_name}_enhanced.jpg', enhanced)
        print(f"Saved enhanced version frame_{pattern_name}_enhanced.jpg")

# Альтернативный способ через rawpy (лучшее качество)
try:
    import rawpy
    import imageio
    
    # Сохраняем как 10-bit packed (для rawpy)
    # Упаковываем 10-битные данные в 16 бит
    raw_16bit = np.zeros(HEIGHT * WIDTH, dtype=np.uint16)
    for i, val in enumerate(raw_10bit):
        raw_16bit[i] = val << 2  # 8-bit -> 10-bit
    
    # rawpy ожидает 16-bit данные
    raw_16bit = raw_16bit.reshape(HEIGHT, WIDTH)
    
    # Создаем временный raw файл
    with open('temp.raw', 'wb') as f:
        f.write(raw_16bit.tobytes())
    
    # Обработка
    raw = rawpy.imread('temp.raw', raw_width=WIDTH, raw_height=HEIGHT)
    rgb_rawpy = raw.postprocess(
        use_camera_wb=True,
        output_bps=8,
        demosaic_algorithm=rawpy.DemosaicAlgorithm.AHD,
        gamma=(1.0, 1.0),
        no_auto_bright=False,
        bright=1.0
    )
    imageio.imwrite('frame_rawpy.jpg', rgb_rawpy)
    print("Saved frame_rawpy.jpg with rawpy (best quality)")
except ImportError:
    print("rawpy not installed. Install: pip install rawpy")
except Exception as e:
    print(f"rawpy error: {e}")