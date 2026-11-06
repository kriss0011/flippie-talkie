# App and Web 0.8: preserve the start and end of speech

The Motorola previously opened its speaker after the beginning of “Testing” had already been transmitted. App 0.8 keys the carrier with 500 ms of balanced, silent PDM first. It buffers browser/USB microphone audio during this period rather than discarding the opening speech. Stored channel tests use the same lead-in. It is applied once in the firmware; the browser does not add another half-second.

Graceful PTT release drains the remaining lead-in and queued speech before returning to reception. Short messages that finish while transmission is preparing also drain. The larger 8192-sample FIFO holds the initial half-second. Drain has a bounded deadline, and explicit Stop, disconnect and transmission limits still shut off transmission. Web 0.8 waits for the radio's completion status before counting a repeated message or starting its pause. Losing pointer capture after normal button release no longer cancels the tail.

## Validation

- All 37 host tests passed, including actual compiled voice callbacks, exact 500,000 us prefix, preservation of queued PCM, bounded drain, cancellation, stale-status rejection, disconnect/error handling and repeat duration budgeting.
- RogueMaster API 88.23 build and APPCHK passed. Installed binary SHA-256: `071ac4c674ff5e7344da80f8a93eaca77b93641c0a2a201ce9a06ed739d1204c`.
- Device binary readback matched. Settings checksum was unchanged. The previous app remains at `SD/apps_data/pmr_radio/Flippie_Talkie_0.7_before_tx_lead_in.fap.bak`.
- Native channel 9 test completed successfully in 2540 ms, consistent with the original 2039.625 ms voice plus 500 ms lead-in.
- USB remote channel 9 test sent 39 audio blocks with zero underruns, zero dropped blocks and no device error. Bluetooth mode was restored afterward.
- The user confirmed hearing the full opening “Testing channel nine” after these two tests. This confirms the phrase in that test, not new range or receive-quality claims. Live phone PTT still needs a user check.
- Web 0.8 published to the existing public website. Reload the page and check the footer before reconnecting.

Reports are in `validation/tx-lead-in-0.8/`. This is an app update; system firmware, receive tuning and stored user recordings were not changed.
