# Request from archesk: let COUNT show the newest seq of any event

Written 2026-11-23 on the archesk side, for whoever works on the ShotPuck firmware next. Richard decided this belongs in the firmware, not the app, so treat it as decided, not as a proposal.

## The request

Make the newest seq the puck has given to **any** event (accepted shot, reject or recording) readable in COUNT. The app uses COUNT to decide where its next REPLAY starts.

## Why

Since archesk 2.0.2 (commit e6a5fbc, done for the 0.3.20 request), the app writes REPLAY after every reconnect, even when COUNT shows nothing new. It starts at the highest seq it has stored + 1. It only counts stored seqs up to COUNT `last_seq`, though. That cap guards against the seq starting over after a full-chip erase by cable: rows stored before the erase sit far above the new `last_seq` and must not block the replay.

`last_seq` only moves when COUNT is published. In `main.c`, `publish_count()` runs on accepted shots (inside `if (e->accepted)` in `handle_detection`), on RESET_COUNT and at boot. It does not run for rejects or recordings, although `st.seq` advances for them too (`.seq = ++st.seq` in `handle_detection` and `issue_recording`).

So every reject after the last accepted shot is above the cap, and the app asks for it again on every reconnect. Rows are keyed by `(device_id, seq)`, so nothing is duplicated or broken; it costs airtime and replay time on every sync. Rejects cluster right after shooting, when the bow is handled and packed away, which is exactly the stretch after the last accepted shot. For scale: F313's synced data has 178 rejects against 175 accepted shots.

## Two ways to do it (pick one)

**A. `last_seq` follows `st.seq` for every event.** Refresh `v_count` whenever `st.seq` advances (`publish_count()`, or a refresh without a notification). This needs no app change: the app's cap then covers the rejects.
- Either notify or not. The app reads COUNT on every connect, and its COUNT notification handler only updates the totals. Notifying on every reject means one notification per handling bump.
- It changes what `last_seq` means, from "last counted shot" to "newest event". Update the 0.3.20 note in `PROTOCOL.md` ("COUNT `last_seq` moves only on accepted shots") and the comment in `protocol.h`.
- The app uses `last_seq` in three places, and all of them are fine with A:
  - The pairing floor (`last_seq + 1`) then also skips rejects from before the pairing.
  - The replay cap, which is the point of this change.
  - The "caught up" check for firmware without the replay end marker (older than 0.3.16 only).

**B. Append `u32 newest_seq` at offset 12** (COUNT grows from 12 to 16 bytes) and leave `last_seq` as it is. Current apps ignore the extra bytes (`parsePuckCount` reads `total`, `last_seq`, and `lifetime` only when there are 12 bytes or more), so this is safe. But the app then needs a small change to cap at `newest_seq` when it is present.

archesk prefers **A**, because the fix then needs nothing from the app. B is fine if you'd rather keep `last_seq`'s meaning; say so in the handover back and the app will read the new field.

**Check:** after a session that ends with rejects, a reconnect should write REPLAY from the newest seq + 1 and get only the 4-byte end marker back.

## For your information: app changes made today (no firmware change asked)

1. **Updates now pipeline two SMP requests.** archesk sends firmware images with mcumgr `pipelineDepth` 2. That makes Nordic's library use its windowed uploader: two requests in flight, and a chunk whose reply is late is resent (up to 5 times) instead of failing the whole update on one 2.5 s timeout.
   - The puck currently has `CONFIG_MCUMGR_TRANSPORT_NETBUF_COUNT=4` and `NETBUF_SIZE=384`. These are the Zephyr defaults: `prj.conf` doesn't set them, and I read them from a build's `.config`.
   - Please don't go below 4 buffers. If you change the SMP buffer size, keep it a multiple of 4; the app uploads with 4-byte alignment.
   - This hasn't run on hardware yet; the next update is the first test.
2. **The upload's link speed is still the puck's choice.** The app's update sheet asks for a high-priority connection on its own Bluetooth connection, but closes that connection before Nordic's library opens the upload connection.
   - Android drops the link when the last app disconnects, so the upload most likely runs on a fresh connection.
   - Its speed then comes from the puck's own request 5 s after connecting (15–30 ms on the charger, since 0.3.15/0.3.19).
   - This is inferred from the code and Android's behaviour, not yet seen in a log. If you change the charger fast-link logic, it still directly affects update reliability.
