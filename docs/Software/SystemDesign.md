# System Design Plan - Adaptive Energy Monitoring System

## 1) Program Setup
- [] Confirm scope baseline (CM7 manager + CM4 DAQ engine + shared eMMC)
- [ ] Freeze interface/version baseline for this design cycle
- [ ] Define measurable targets: sample rate, max drop count, transfer throughput, boot time
- [ ] Create issue tracker labels: IPC, DAQ, STORAGE, ETH, RTOS, TEST

## 2) Architecture Baseline (Must Be Stable First)
- [ ] Finalize core ownership matrix (CM7 vs CM4) for every peripheral/module
- [ ] Finalize shared-memory map (mailboxes, status block, chunk buffer, ownership rules)
- [ ] Finalize HSEM allocation and lock policy (`BOOT_INIT`, `FS_GLOBAL`, optional `MMC_HW`, `IPC_CMD`)
- [ ] Finalize cache coherency policy for shared/DMA regions (cacheable vs non-cacheable + clean/invalidate points)
- [ ] Define unsupported operations explicitly (active-file concurrent access, unsafe maintenance ops)

## 3) IPC Design and Contracts
- [ ] Freeze IPC command/response structs (magic, version, sequence, error model)
- [ ] Define command lifecycle and timeout/retry strategy on CM7
- [ ] Define CM4 event handling model for incoming commands
- [ ] Add protocol compatibility rules for future expansion
- [ ] Build IPC validation checklist (sequence monotonicity, stale command detection, timeout behavior)

## 4) CM4 DAQ Engine (Deterministic Path)
- [ ] Implement/confirm CM4 state machine: Idle -> Preparing -> Acquiring -> Stopping -> Finalizing -> Error
- [ ] Finalize DMA strategy (half/full callbacks or double-buffer)
- [ ] Finalize buffer pipeline: DMA buffer -> aggregation buffer -> write queue
- [ ] Define overrun handling (counter, threshold, graceful stop policy)
- [ ] Define periodic status publish fields and cadence

## 5) Storage/eMMC + FatFs Strategy
- [ ] Lock complete filesystem transactions with `FS_GLOBAL` wrapper APIs
- [ ] Define active-file policy (CM4 only writer; CM7 reads finalized files or closed chunks)
- [ ] Define file chunk rotation policy if near-live transfer is required
- [ ] Define write/flush policy (block size, sync interval, stop-time flush)
- [ ] Define maintenance command policy (`mkfs`, delete, rename, stat) and safe operating states

## 6) CM7 Ethernet and Control Plane
- [ ] Freeze host command set (`PING`, `GET_STATUS`, `START_ACQ`, `STOP_ACQ`, `LIST`, `STAT`, `READ`, `DELETE`)
- [ ] Define command validation and error translation rules (CM4/internal -> host-visible)
- [ ] Define transfer task model for chunked file transmission
- [ ] Define backpressure behavior (socket slow, CM4 busy, chunk timeout)
- [ ] Define diagnostics endpoints (health, counters, last-error, active file)

## 7) File Format and Data Contract
- [ ] Freeze DAQ binary file header schema (run metadata + format version)
- [ ] Freeze per-block schema (sequence, sample index/time, length, optional CRC)
- [ ] Define finalization markers for graceful stop vs fault/abort
- [ ] Publish parser contract for host/offline tooling
- [ ] Add forward-compatibility rule for versioned readers

## 8) Reliability and Safety
- [ ] Define watchdog strategy (CM7 task liveness + CM4 loop liveness + IPC timeout monitor)
- [ ] Define fault classes and escalation matrix (recoverable vs terminal)
- [ ] Define safe-stop behavior for every critical fault type
- [ ] Define power-loss behavior and recovery checks for partially written files
- [ ] Define boot-time recovery actions (mount checks, last-run status, optional cleanup)

## 9) Integration Sequence (Execution Plan)
- [ ] Phase 1: IPC bring-up (`PING`, `GET_STATUS`)
- [ ] Phase 2: CM4 RAM-only DAQ (no filesystem writes)
- [ ] Phase 3: CM4 eMMC writes + finalize path
- [ ] Phase 4: CM7 chunked transfer path
- [ ] Phase 5: Controlled CM7 filesystem maintenance operations
- [ ] Phase 6: Throughput tuning + hardening

## 10) Verification and Exit Criteria
- [ ] IPC tests: delivery, timeout, sequence integrity
- [ ] DAQ tests: sample-rate accuracy, no drops under target load, clean stop/finalize
- [ ] Storage tests: file integrity, mount/reboot consistency, concurrency lock correctness
- [ ] Ethernet tests: large transfer stability, chunk correctness, disconnect recovery
- [ ] Fault injection: eMMC write fail, IPC timeout, DMA overrun, mid-run reset
- [ ] Release gate: all critical tests pass and no unresolved high-severity defects

## 11) Delivery Artifacts
- [ ] Shared memory map document
- [ ] IPC API header + message registry
- [ ] CM7 task interaction diagram
- [ ] CM4 DAQ state diagram
- [ ] Filesystem lock wrapper spec
- [ ] DAQ file format spec
- [ ] System integration test report template
