#!/usr/bin/env python3
import serial
import time
import io

PORT = '/dev/ttyACM1'
BAUDRATE = 5000000

ser = serial.Serial(PORT, BAUDRATE, timeout=5)
time.sleep(2)

ser.reset_input_buffer()
ser.write(b'get_bmp\n')
print("Command sent")

# Ждем BMP_START
while True:
    line = ser.readline()
    if line:
        try:
            text = line.decode().strip()
            if text.startswith('BMP_START:'):
                size = int(text.split(':')[1])
                print(f"Size: {size} bytes ({size/1024/1024:.1f} MB)")
                break
        except:
            pass

# Читаем данные в буфер
print("Receiving...")
data = bytearray(size)  # Предвыделяем память
received = 0

while received < size:
    if ser.in_waiting:
        chunk = ser.read(min(ser.in_waiting, size - received))
        data[received:received + len(chunk)] = chunk
        received += len(chunk)
        
        percent = (received * 100) // size
        if percent % 10 == 0:
            print(f"Progress: {percent}% ({received}/{size})", end='\r')
    else:
        time.sleep(0.001)

print(f"\nReceived {received} bytes")

# Быстрое сохранение
with open('photo.bmp', 'wb') as f:
    f.write(data)
print("✅ Saved photo.bmp")

ser.close()