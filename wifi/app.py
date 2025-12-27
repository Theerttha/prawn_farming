import requests
from datetime import datetime, timedelta
import random


URL = f"https://hallo-esp-default-rtdb.firebaseio.com/sensorLogs/device1.json"

def generate_sample_row(ts):
    return {
        "timestamp": ts.strftime("%Y-%m-%d %H:%M:%S"),
        "temperature": round(random.uniform(27.0, 31.5), 2),
        "tds": round(random.uniform(350, 520), 3),
        "ph": round(random.uniform(7.0, 8.3), 2),
        "orp": round(random.uniform(180, 320), 2)
    }

def upload(row):
    r = requests.post(URL, json=row, timeout=10)
    if r.status_code in (200, 201):
        print("Uploaded:", row)
    else:
        print("FAILED:", r.status_code, r.text)

if __name__ == "__main__":
    base_time = datetime(2025, 12, 27, 14, 55, 0)

    for i in range(30):       # simulate 30 samples
        ts = base_time + timedelta(seconds=i * 10)
        upload(generate_sample_row(ts))
