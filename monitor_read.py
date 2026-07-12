import serial
import sys
import time

port = 'COM5'
baud = 115200
timeout = 45

try:
    ser = serial.Serial(port, baud, timeout=1)
except Exception as e:
    print(f"Failed to open {port}: {e}")
    sys.exit(1)

ser.dtr = False
ser.rts = False
time.sleep(0.2)

print(f"--- Reading from {port} for {timeout}s ---", flush=True)
start = time.time()
while time.time() - start < timeout:
    try:
        data = ser.read(ser.in_waiting or 1)
        if data:
            sys.stdout.buffer.write(data)
            sys.stdout.flush()
    except Exception as e:
        print(f"\nRead error: {e}")
        break

ser.close()
print("\n--- done ---", flush=True)
