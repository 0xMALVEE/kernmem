# UnknownCheats — League of Legends reversal material

Drop folder for the offset/struct thread and anything else pulled from UnknownCheats.
This is **source material**, not code. It feeds `scripts/offsets.py`.

Thread of record: `league-of-legends/310587-league-legends-reversal-structs-offsets`
(the thread is paginated and long — the tail is the only part that is current).

---

## Layout

```
pages/     raw saved HTML, one file per page
notes/     distilled offsets per patch, human-written
session/   cookies used for the authenticated scrape
```

## Naming — pages

```
<thread-id>_p<NN>_<YYYY-MM-DD>.html

310587_p01_2026-11-15.html
310587_p42_2026-11-15.html
```

Zero-pad the page number so lexical sort is page order. Keep the thread id first so
two threads never collide. Never rename to something descriptive only — the id is what
makes a page traceable back to its post.

## Naming — notes

```
<YYYY-MM-DD>_<patch>.md

2026-11-15_14.23.md
```

One file per patch. **Offsets drift every patch**, so a note without a patch number is
almost worthless.

---

## What is worth saving

Save the whole page. Do not cherry-pick while scraping — you cannot tell which post
matters until you have read it, and the surrounding replies are what tell you whether an
offset was confirmed or corrected two posts later.

When distilling into `notes/`, record for each offset:

| Field | Why |
|-------|-----|
| name | what it is (`LocalPlayer`, `HeroList`, `Camera`) |
| offset | the value, as given |
| module | which module it is relative to — usually `League of Legends.exe` |
| chain | the dereference path, not just the final offset |
| post | link or post number it came from |
| confidence | confirmed / unconfirmed / disputed |

An offset with no chain and no confidence is a rumour.

---

## Reality check on this thread

The thread starts in 2018. Most of the early pages describe structures that have been
rebuilt several times since. Treat anything older than the current patch as a **hint about
shape**, not as a value to hardcode.

Reversal threads also routinely contain:

- offsets valid for exactly one patch, unmarked
- two people posting contradictory values for the same field
- encodings that changed (the game moved object layout repeatedly)
- posts that are correct but about a *different* game mode or the PBE build

So: every offset gets verified against a live process before it reaches a script.

---

## Session cookies

`session/cookies.txt` holds the `cf_clearance` / `bbuserid` / `bbsessionhash` triple.

- `cf_clearance` is bound to the browser's TLS fingerprint, which is why `curl` with
  copied cookies gets a 403 while the real browser does not. Scrape from the browser.
- All three expire. When the scrape starts returning the Cloudflare interstitial
  (`Just a moment...`), re-copy them from a fresh authenticated request.
- The account is not privileged, so nothing here is secret, but they are live session
  tokens — treat them as disposable rather than durable.

---

## After scraping

1. Distil into `notes/` with the table above.
2. Update `scripts/offsets.py` from the newest note only.
3. Bump the patch stamp in that file — the script warns when it is stale.
4. Commit. Raw pages and notes go in together so a value can always be traced back.
