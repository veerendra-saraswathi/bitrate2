# Branch audit and working policy

**Audit: 11 November 2026, IST.** Remote refs were inspected directly with `git ls-remote --heads --tags origin` and fetched with full history. No remote tags were returned at the time of inspection.

| Remote branch | Head | Role / observed content | Recommendation |
|---|---|---|---|
| `main` | `f07161bb0469153852d496a4aa4acacf8fd38179` | Current default branch; baseline reports/source | Receive the reviewed documentation-only landing page; preserve existing source/history without force push |
| `week-top3-score-jetson` | `3b644a84b6ea40db13e5a977e4be697625fd150a` | Eight commits after main: scoring harness, synthetic/high-depth corpus, LTIR and two-layer search change | Keep as historical experiment branch for now; tag/backup before any later retirement |
| `video-test` | `f4d5c41152e4881f265349b32857e0179f16ce3a` | Video expectations, Beauty data/LFS and Jetson runners; branch root is `ab31fda` | Use as proposed active video development line; preserve root-history distinction |
| `cleanup/reconcile-2026-11-11` | Local cleanup commit, based on `video-test` | Landing page, evidence/docs index, branch/layout audit, scoped ignore policy, current runner | Push only this branch for review; do not update other branches |

## Important history finding

`video-test` starts at root commit `ab31fda985392dc6baa92b7a8f9759e727b54cea`, with no parent in the complete fetched history. It has **no merge base** with `main` or `week-top3-score-jetson`. Similar file contents do not establish shared Git ancestry. Do not force-push main or run an automatic unrelated-history merge merely to tidy the branch list. The source trees differ in `tools/ewc_layered.cpp` and supporting scripts/data; review those changes separately before any consolidation.

The current cleanup keeps the `video-test` C++ source/build/tests unchanged. It does not claim to reproduce or approve that two-layer source difference.

## Branch visibility and publication

| Question | Policy |
|---|---|
| Can one branch be hidden/private inside this public repo? | GitHub visibility is repository-level, not a per-branch privacy switch. Changing the default branch changes the landing view, not access to other refs/history |
| Should old branches be local-only? | Not as the only backup. Local-only copies can be lost. Keep remote experiment branches until provenance and backups are verified |
| Can we archive a branch? | Use a documented tag/archive naming convention or separate backed-up repository. GitHub's repository archive switch applies to the whole repository |
| Should main be changed now? | The owner requested the simplified landing page at the repository root. Promote reviewed documentation to main without merging unrelated code histories or changing the default branch |
| Future simple model | One reviewed default branch, one active development line, short-lived focused branches, and documented experiment tags after safe consolidation |

References: [repository visibility](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/managing-repository-settings/setting-repository-visibility), [default branch](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-branches-in-your-repository/changing-the-default-branch).

## Large objects and tags

Three tracked Beauty split files are 94,371,840 + 94,371,840 + 18,616,320 = 207,360,000 bytes (about 197.75 MiB). Their `.u16.partaa` suffixes do not match the present `*.u16` LFS pattern. Adding `.gitattributes` later does not retroactively convert these objects or remove their history. This cleanup preserves them and ignores new split/raw inputs by default; migrating/removing historical data needs a separate verified backup and source-identity decision.

No branch/tag deletion, history rewrite, visibility change or default-branch change was performed. A subsequent documentation-only promotion is prepared for `main`: it copies reviewed reports/navigation and the existing data-kit scripts, simplifies the landing page, and leaves C++ source/build/tests and all datasets unchanged.
