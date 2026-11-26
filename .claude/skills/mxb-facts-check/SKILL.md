---
name: mxb-facts-check
description: Before releasing this MX Bikes or GP Bikes plugin, app or mod, check it against the MXB Facts questions and list every answer that wouldn't be Good, with its evidence. Use when asked for the MXB Facts check, or before tagging a release.
---

# MXB Facts check

MXB Facts answers the same questions about every MX Bikes and GP Bikes tool, with evidence for each answer. This check answers them for this repository before a release, so the author sees what a card would say while there is still time to change things. It is the author's own check, not a card.

1. Get the rules at a fixed version, in a folder outside this repository:
   `git clone https://github.com/thomas4f/mxb-facts <folder>` and `git -C <folder> checkout 29dd417f8e673b247fdf9c0b5076ecffe27c63d2`.
   This copy of the check was made at that commit, so its result is what a card made then would say. The rules can change
   since: if the `commit` in https://thomas4f.github.io/mxb-facts/api/v1/index.json differs, tell the author that a newer
   version of the rules exists (they can download this check again from https://thomas4f.github.io/mxb-facts/authors/), and
   ask which to use before going on.
   Read its `CLAUDE.md`, `rules/template.md`, `rules/questions.md` and `prompts/analyse-tool.md` in full.
2. Follow steps 1 to 5 of the prompt in `prompts/analyse-tool.md` ("The prompt") for this repository as it will be released: the release files are the ones its build makes, or the files the author is about to upload. The real-game run is `lab/real-game/` and the plugin test host is `lab/plugin-test-host/` in that folder. Keep the evidence in that folder; don't commit or push anything there.
3. Skip steps 6 to 10: no card file, no build, no pull request. Look up hashes on VirusTotal only if the author gives you their own key, and upload nothing; otherwise P1 is "Not checked".
4. Answer every question in `rules/questions.md` with one of its verdicts, the mark its table gives, and the evidence: the file and lines in this repository, or the test you ran.
5. Report to the author: first every answer that isn't Good (Concerns, then Notes, then Not checked), each with its question ID, verdict, mark and evidence, and for each Concern the points it would cost in its area (the cost table in `rules/scores.md`); then the Good answers, one line each.
6. Don't change any code on your own. What to change, if anything, is the author's call: a Note or a Concern can be the honest answer for what the tool is for. Offer to go through the answers with the author.

The result belongs to the author. It isn't an MXB Facts card, so don't publish it as one. If the author sends it to MXB Facts, it counts as a claim to check, like a README.
