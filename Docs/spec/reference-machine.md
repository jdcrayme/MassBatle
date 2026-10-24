# Reference Machine

The named reference machine for all MassBattle performance baselines [roadmap R10]. All Phase 0+ perf numbers are recorded against this hardware.

| Field | Value |
|-------|-------|
| CPU | Intel Core i7-9700F @ 3.00 GHz (8 cores / 8 threads, no hyperthreading) |
| RAM | 16 GB (16,320 MB total physical) |
| GPU | NVIDIA GeForce GTX 1660 Ti (~4 GB VRAM as reported by WMI) |
| OS | Microsoft Windows 10 Home, 10.0.19045 (Build 19045), x64 |
| Motherboard / BIOS | ASUS "System Product Name", American Megatrends 1201 (2019-07-15) |
| Engine install | UE 5.8.4 (changelist 58902052, `++UE5+Release-5.8`, promoted build) at `E:\Unreal\UE_5.8` |

Recorded 2026-10-23 during pre-phase-0 setup (BRIEF.md Task 0 step 5). Probe method: `systeminfo` + `wmic cpu|win32_VideoController|ComputerSystem`.
