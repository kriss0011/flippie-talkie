# Web 0.8: complete outgoing messages

Use the existing public [Flippie Talkie website](https://flippie-talkie.kriss0011.chatgpt.site), reload it and verify the footer says Web 0.8. The installed native App 0.8 provides a 500 ms silent carrier before all stored tests and browser/USB speech, buffering the opening audio. Web 0.8 adds no second prefix.

The PTT button shows Opening channel and Finishing message while the radio starts or drains buffered speech. Release finishes the message; emergency Stop, backgrounding or disconnect cancels immediately. Recorded-message repeats wait for the radio's completion status rather than a fixed 220 ms sleep, and timed runs account for the added lead-in. Normal pointer release no longer aborts the queued tail.

Host tests cover completion, cancellation, disconnects, errors, stale status and repeat budgeting. Device native/USB tests completed, and the user confirmed the full channel 9 test phrase. This does not establish a new phone PTT or RF range result. Browser recordings and per-channel squelch settings retain their existing local storage.
