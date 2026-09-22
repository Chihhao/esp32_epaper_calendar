#!/usr/bin/env python3
"""PC 端 BLE 校時常駐程式。

板子 (EPD-CAL) 每天 09:00:30 醒來廣播 30 秒；這支程式一直掃描，
掃到就連上、把現在的 UTC epoch 秒數寫進特徵值，板子收到後自行設定時間。

用法:
  python ble_time_sync.py          # 常駐，無限迴圈
  python ble_time_sync.py --once   # 只做一次 (測試用)，成功 exit 0

需求: Python 3.8+，pip install bleak；Windows 10 以上並有 BLE (藍牙 4.0+) 模組。
"""
import asyncio
import sys
import time
from datetime import datetime

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "EPD-CAL"
CHR_UUID = "a7c1f0e0-1d2b-4c3d-8e9f-0000c0ffee02"
SCAN_TIMEOUT = 10      # 每輪掃描最多等幾秒
AFTER_SYNC_PAUSE = 60  # 寫入成功後休息幾秒 (板子已經去睡了，不用一直掃)
AFTER_FAIL_PAUSE = 3


def log(msg: str) -> None:
    print(f"{datetime.now():%Y-%m-%d %H:%M:%S} {msg}", flush=True)


async def sync_once() -> bool:
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=SCAN_TIMEOUT)
    if dev is None:
        return False
    log(f"found {DEVICE_NAME} ({dev.address}), connecting")
    async with BleakClient(dev, timeout=15) as client:
        epoch = int(time.time())
        await client.write_gatt_char(CHR_UUID, str(epoch).encode(), response=True)
        log(f"wrote epoch {epoch} ({datetime.fromtimestamp(epoch):%Y-%m-%d %H:%M:%S} local)")
    return True


async def main() -> int:
    once = "--once" in sys.argv
    log(f"start, scanning for {DEVICE_NAME}")
    while True:
        try:
            ok = await sync_once()
        except Exception as e:  # 藍牙暫時出錯就等一下再掃
            log(f"error: {e!r}")
            ok = False
        if once:
            return 0 if ok else 1
        await asyncio.sleep(AFTER_SYNC_PAUSE if ok else AFTER_FAIL_PAUSE)


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
