rewind.history_seconds = 6
rewind.record_hz = 60 -- 30/60/120; 120 uses actual 120 Hz simulation substeps
rewind.enabled = true
rewind.fast_rate = 5
rewind.trace_rate = 1

-- Fast rewind alone stops near damaging bullets; values are snapshot state.
rewind.fast_stop_near_bullets = true
rewind.fast_bullet_margin = 2 -- logical pixels, 0..64; never changes bullet hitR
