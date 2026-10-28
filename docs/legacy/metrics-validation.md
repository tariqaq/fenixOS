# Metrics validation

Run on 2026-10-28 with `python tools/validate_metrics.py 15` (final build, LHM running). The script runs `fenix sample` alongside independent sources over the same 15 s window and compares means. It exits non-zero on any FAIL.

Window: 15 s, 14 fenix samples, 14 typeperf, 15 nvidia-smi, 15 LHM.

| Metric | FenixDeck | Reference | Source | Diff | Tolerance | Result |
|---|---|---|---|---|---|---|
| CPU load (mean) | 9.2% | 9.2% | typeperf % Processor Utility | +0.0 | ±5% | PASS |
| CPU load vs LHM (mean) | 9.2% | 7.6% | LHM CPU Total (time-based) | +1.6 | n/a | INFO |
| CPU package temp (mean) | 56.9°C | 57.0°C | LHM direct | -0.1 | ±3°C | PASS |
| CPU package power (mean) | 13.0 W | 13.6 W | LHM direct | -0.5 | ±3 W | PASS |
| GPU temp (mean) | 50.0°C | 50.3°C | nvidia-smi | -0.3 | ±2°C | PASS |
| GPU power (mean) | 6.3 W | 6.3 W | nvidia-smi | +0.0 | ±3 W | PASS |
| GPU util (mean) | 21.4% | 18.6% | nvidia-smi | +2.8 | ±10% | PASS |
| GPU VRAM used (mean) | 1798.8 MB | 1791.0 MB | nvidia-smi | +7.8 | ±5% | PASS |
| GPU VRAM total | 8188.0 MB | 8188.0 MB | nvidia-smi | +0.0 | ±1 MB | PASS |
| RAM used (mean) | 17940.7 MB | 17964.7 MB | CIM Win32_OperatingSystem | -23.9 | ±3% | PASS |
| RAM total | 32468.0 MB | 32468.5 MB | CIM Win32_OperatingSystem | -0.5 | ±1 MB | PASS |
| Disk C: used | 894.7 GB | 894.7 GB | CIM Win32_LogicalDisk | -0.0 | ±1 GB | PASS |
| Disk C: total | 999.2 GB | 999.2 GB | CIM Win32_LogicalDisk | -0.0 | ±1 GB | PASS |
| Disk D: used | 498.9 GB | 498.9 GB | CIM Win32_LogicalDisk | -0.0 | ±1 GB | PASS |
| Disk D: total | 762.9 GB | 762.9 GB | CIM Win32_LogicalDisk | -0.0 | ±1 GB | PASS |
| Disk read (mean) | 1.4 KB/s | 1.4 KB/s | typeperf PhysicalDisk | +0.0 | ±200 KB/s | PASS |
| Disk write (mean) | 546.4 KB/s | 545.4 KB/s | typeperf PhysicalDisk | +1.0 | ±200 KB/s | PASS |
| Net down (mean) | 2.0 KB/s | 2.0 KB/s | typeperf Network Interface | -0.0 | ±20 KB/s | PASS |
| Net up (mean) | 1.3 KB/s | 1.3 KB/s | typeperf Network Interface | -0.0 | ±20 KB/s | PASS |
| Battery % | 95.0% | 94.0% | CIM Win32_Battery | +1.0 | ±1% | PASS |

19 pass, 0 fail, 1 info

## Notes

- **CPU load matches Task Manager** (`% Processor Utility`: frequency-scaled, so it can exceed the time share under turbo). LHM "CPU Total" is time-based, so it reads lower under turbo by design, and that row is informational.
- **Network double counting fixed:** sysinfo lists NDIS filter drivers (Npcap, VirtualBox, WFP) as extra interfaces with the same traffic and an all-zero MAC. These are skipped; before the fix the value was about 2x.
- **GPU sensors:** with LibreHardwareMonitor running, GPU temp/power/clock/load and VRAM total come from LHM. LHM uses NVML in its own process, so FenixDeck never loads `nvml.dll` (~19 MB private saved). Without LHM, FenixDeck loads NVML itself, and only while the dGPU is in D0.
- **VRAM used** comes from the PDH D3D dedicated counter (what Task Manager shows), which matches `nvidia-smi`. LHM's "GPU Memory Used" includes ~230 MB of driver-reserved memory.
- **CPU temp/power** have no independent Windows source; they are checked against LHM read directly.
- **Battery:** Win32_Battery rounds differently from GetSystemPowerStatus (±1%).
