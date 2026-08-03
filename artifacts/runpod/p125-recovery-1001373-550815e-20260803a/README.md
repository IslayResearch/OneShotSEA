# Interrupted sequential recovery duplicate

This run retried only curve index `1001373` with one effective compute thread
and trace cap 4096 while independent batched recovery configurations were
evaluated.  It produced no durable curve record, checkpoint, or certificate.

The process was deliberately sent `SIGTERM` at `2026-08-03T04:59:53Z` after
`p125-recovery-1001373-batch15x1024-550815e-20260803a` had already completed
the same curve soundly and advanced its authenticated cursor to `1001374`.
The launcher recorded status 143.  This interrupted duplicate contributes no
coverage and is not admitted by the strict search auditor; its quiescent raw
files are retained under `ohfo3hbov7ot8v/` with `SHA256SUMS` for operational
transparency only.

Stopping this redundant run freed the final pod core before the fresh dual-8
suffix `[1001374,1001551)` was launched.
