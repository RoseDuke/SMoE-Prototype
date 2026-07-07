# FPGA V0 Test Inputs

The first FPGA V0 smoke tests reuse the repository-level CSV traces in
`../../tests/traces`. The host runner converts those CSV rows directly into the
fixed SmartNIC descriptor ABI before calling the HLS top function.

Useful smoke command:

```bash
bash ../scripts/run_smoke.sh
```
