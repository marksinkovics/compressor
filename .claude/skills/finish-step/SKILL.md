---
name: finish-step
description: Update the Compressor handover docs (docs/roadmap.md, CLAUDE.md Status, docs/development.md) when a roadmap step or phase is done, then suggest a commit message. Use at the end of every step without being asked, or when the user asks to update the docs or the handover.
---

# Finishing a step or phase

The user clears the conversation between steps. The next session must be able to continue from `CLAUDE.md`'s Status and `docs/roadmap.md` alone.

## 1. Check it is really done

- Build and run the tests (`ctest`, or the presets once Phase 3 adds them). If something wasn't run or is failing, say so and don't mark the step done.
- Anything left out or found along the way goes into `docs/roadmap.md`: a new known issue, or a note in the relevant phase.

## 2. `docs/roadmap.md`

- Tick the step's checkbox and add `(done)` to its bold label or first words.
- Replace the plan text with what was actually built: the files, types and functions a later reader needs, which test covers it, and anything that turned out differently from the plan.
- Tick the known issues the step fixed.
- When a phase is complete, add `(done)` to its heading and a one- or two-sentence summary under it.

## 3. `CLAUDE.md`

- Status: one line per finished phase, and `Next:` naming the exact next step.
- Update Commands or Architecture only if the step changed them (for example presets in Phase 3, CLI11 in Phase 4). Keep the file lean; detail belongs in `docs/`.

## 4. `docs/development.md`

Update it for changes to how people build, test, run or benchmark the project. It's not a changelog.

## 5. Unfinished work

Only if the work stops in the middle of a step, write `docs/handover.md`: what is done, what is left, open decisions, and any failing test with its output. Delete it once the step is finished.

## 6. Report

Short: the files updated, the test status, and a suggested Angular-style commit message (`type(scope): subject`, for example `test(compressor): add round-trip tests for sample files`). Never commit.
