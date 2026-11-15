# UnknownCheats — League of Legends reversal material

Drop folder for the offset/struct thread and anything else pulled from UnknownCheats.
This is **source material**, not code. It feeds `scripts/offsets.py`.

Thread of record: `league-of-legends/310587-league-legends-reversal-structs-offsets`

---

## Layout

```
posts/     one archive file per thread — what tempermonkey/post-download.js writes
notes/     distilled offsets per patch, human-written
session/   cookies, plus a saved copy of the Cloudflare interstitial for reference
```

## How the archiver actually behaves

`tempermonkey/post-download.js` (v2.1) is a userscript, run from the browser while
logged in. It is not a per-page downloader:

- It fetches a **page range** with a worker pool (1–8 workers, default 3) and a
delay between requests (default 1200 ms).
- It writes **one combined file for the whole thread**, not one file per page.
- Format is `.md` (default) or `.html`, chosen in the panel.
- The filename is derived from the thread title —
`threadTitle.replace(/[^\w\-. ]+/g, '_').slice(0, 80)` — e.g.
`League_of_Legends_Reversal_Structs_Offsets.md`.
- The output header records `Pages saved: <lo>–<hi>` and, if any page failed,
a `Missing pages (n): …` line. Check that line before trusting an archive.
- Resume is an **IndexedDB** cache (`ucarch` / `pages`, keyed per thread) with a
reuse prompt and a clear button. It is per browser profile, not per file.
- Buttons: go / stop / save / clear. `stop` aborts mid-run and still offers save.

### Caveats worth knowing before you rely on an archive

| Caveat | Consequence |
|--------|-------------|
| Filename comes from the title | Two threads with similar titles collide and overwrite silently |
| Re-running a range overwrites | The previous archive is replaced, not appended |
| IndexedDB resume is profile-bound | Clearing site data loses the resume cache |
| `.md` strips most formatting | Quotes, code blocks and links survive; tables and nested markup do not |
| Failures are recorded, not retried forever | Four attempts per page, then the page lands in `Missing pages` |

If you need a faithful copy of a post with tables or formatting, archive that thread
as `.html` instead.

## After scraping

Move the downloaded file into `posts/` and rename it to
`<thread-id>_<slug>_<YYYY-MM-DD>.<md|html>` so the thread id survives even when the
title-derived name is vague:

```
posts/310587_reversal-structs-offsets_2026-11-15.md
```

Then distil into `notes/` using `TEMPLATE.md`. For each offset record name, offset,
module, **chain**, source post and confidence. An offset with no chain and no
confidence is a rumour.

## Naming — notes

```
<YYYY-MM-DD>_<patch>.md

2026-11-15_14.23.md
```

One file per patch. Offsets drift every patch, so a note without a patch number is
almost worthless.

## Reality check on this thread

The thread starts in 2018. Most early pages describe structures rebuilt several times
since. Treat anything older than the current patch as a hint about **shape**, not a value
to hardcode.

Reversal threads also routinely contain:

- offsets valid for exactly one patch, unmarked
- two people posting contradictory values for the same field
- posts that are correct but about a different game mode or the PBE build
- layout encodings that changed when the game rebuilt its object model

So: every offset gets verified against a live process before it reaches a script.

## Cloudflare, and why curl failed here

`cf_clearance` is bound to the browser's TLS fingerprint. Copying the cookie into
`curl` produces a 403 interstitial (`Just a moment...`) even though the cookie is valid
and present — a real browser engine is required. The userscript handles this natively: it
detects the interstitial text, backs off 10 s and retries, and backs off 8 s on 429/503.

`session/cloudflare_challenge_sample.html` is a saved copy of that interstitial, kept so
the detection pattern can be checked without hitting the site.

## Session cookies

`session/cookies.txt` holds `cf_clearance` / `bbuserid` / `bbsessionhash`. All three
expire; when the scrape starts returning the interstitial repeatedly, re-copy them from a
fresh authenticated request. They are live session tokens — disposable, not durable.

## Pipeline

1. Scrape with the userscript → move into `posts/`, renamed with the thread id.
2. Distil into `notes/` with the table in `TEMPLATE.md`.
3. Update `scripts/offsets.py` from the newest note only, and bump its patch stamp.
4. Commit archives and notes together so a value always traces back to its post.
