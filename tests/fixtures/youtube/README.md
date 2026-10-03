# YouTube live chat fixtures

Example responses for `mxbmrp3/core/youtube_chat.h`, read by
`tests/unit/test_youtube_chat.cpp`.

These are **reconstructed, not captured**. When they were written, the build
environment could not reach youtube.com. Their shape follows the maintained
open-source readers' source code as of September 2026: YouTube.js
(its livechat parser classes) and yt-dlp (its YouTube extractor and live-chat downloader).
The older pytchat, masterchat and chat-downloader describe the same fields.
Everything the parser does not read is trimmed away.

| File | What it stands for |
|------|--------------------|
| `live_page.html` | `/@handle/live` while live. It carries `ytcfg`, `ytInitialPlayerResponse` (`isLive: true`) and `ytInitialData` with the chat continuations. A title with braces and escaped quotes tests the brace matching. |
| `chat_page.html` | `/live_chat?is_popout=1&v=ID`, the chat frame's page. It carries the continuation the first poll sends, with `visitorData` only in the `responseContext` and no `VISITOR_DATA`. |
| `not_live_page.html` | `/@handle/live` for a channel that is not live: a channel page with no video. |
| `upcoming_page.html` | A scheduled stream's watch page: a video that is not live yet. |
| `home_page.html` | What an unknown handle can redirect to: YouTube's home feed (`FEwhat_to_watch`). |
| `changed_page.html` | A page with no `ytInitialData`, standing in for a format change. |
| `chat_response.json` | One `get_live_chat` poll. It holds an owner, a moderator, a member and a verified channel, a super chat, a sticker, a new member, a deleted message, a ban, and kinds the reader skips. |
| `chat_ended.json` | A poll after the stream ended: no `continuationContents`. |
| `chat_changed.json` | Messages in a shape the reader does not know, which must read as "unavailable". |

When YouTube changes its format, capture real responses, replace these files,
and let the unit test show what moved.
