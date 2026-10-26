# Embedded reliability review

This review targets the single-core i.MX RT1062 implementation on main. The RP2350 fixes in VoltCyclone/Hurra PR #21 cannot be copied directly.

| Area | Applicability and result |
| --- | --- |
| Timer rollover and parser integer limits | Same defect class; fixed rollover-safe deadlines, deadline-zero scheduling, and INT32_MIN parsing. |
| Protocol receive timeout | Fixed TinyFrame timeout advancing per poll rather than per elapsed millisecond. |
| UART TX ownership/admission | Fixed partial framed-reply enqueue; 512-byte queue admits the current maximum TinyFrame callback. Deferred baud changes wait for UART TC as well as DMA/ring idle. |
| DMA RX complete laps and active-copy overwrite | Fixed using a 16384-byte hardware count interval independent of the 1024-byte destination modulo, coherent snapshots, copied batches, and post-copy validation. |
| Injected state after RX loss | Parser reset, pending synthetic motion and timed releases canceled, injected held keys/buttons released. Physical input masks remain configured. |
| Keyboard duplicate insertion and modifier release | Fixed search-before-insertion and authoritative timed release, including modifiers and full release-table fallback. |
| Position arithmetic | Saturates cumulative position and minimum signed inversion instead of overflowing. |
| DMA cache ownership | Existing non-cacheable OCRAM DMA section, alignment, MPU mapping and linker size assertion are already appropriate; retained. |
| Core 1, RP hardware spinlocks, TFT ISR rendering | Not applicable to this single-core implementation. |
| USB descriptors/report IDs/halt recovery; click scheduler | PR #9 addresses these independently. Preserved while reconciling PR #9 with the reliability fixes. |

## RX guarantee and recovery limits

CITER samples are usable only before another full 16384-byte count interval could pass. The guard uses both GPT microseconds and milliseconds, with a conservative 21 ms limit derived from the maximum supported 6 Mbaud and an eight-bit minimum byte duration plus FIFO margin. It does not rely on counting major-loop interrupts, which may coalesce. A sample phase mismatch is retried; a copy is discarded if a fresh snapshot cannot validate it or if the ring was overwritten during copying.

A prolonged polling gap fails closed: data and parser state are discarded, injected actions are released, and reception waits for at least 1 ms without progress and an empty FIFO. The discarded-byte counter is a lower bound when complete hardware-count intervals may have elapsed. This is bounded recovery, not lossless reception under arbitrary stalls. Traffic must pause for recovery after a loss. Microsecond/millisecond clocks and hardware register semantics still require board validation.

## Validation

`make test test-reliability` runs existing motion/humanization/cadence tests and new AddressSanitizer/UndefinedBehaviorSanitizer tests for both protocol implementations and UART helpers. The kmbox adapter compiles extracted production routines against register mocks and tests complete/multiple laps, incoherent snapshots, overwrite during copying, idle recovery, timed modifier release, and UART TC gating.

`make PROTOCOL=hurra` and `make PROTOCOL=ferrum` cannot link locally: the configured arm-none-eabi-gcc installation is absent. PR CI provides the full firmware build. No physical board, DMA/cache stress, USB enumeration or signal/baud validation was performed. TinyFrame emits an existing harmless uint8 length-versus-256 comparison warning in the host suite.

`make test-all` additionally builds both firmware protocols. PR #9 integration retains the click scheduler and removes stale legacy release fields; click deadlines now handle timer rollover. The pre-existing 912 MHz overclock (rated maximum 600 MHz) is unchanged and remains a hardware stability/lifetime risk.
