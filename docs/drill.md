# GitHub workflow drill

Live drill (2026-10-12): walking the complete branch → commit → push → PR → rebase → merge loop,
end to end, with a real pull request. Throwaway teaching branch — this PR merges into a
sandbox branch and is deleted; nothing ships from here.

## Where each step landed

1. branch cut from the base (`origin/joose-main`)
2. commit with a scoped message, pushed with `-u`
3. PR opened `--base joose/drill-sandbox --head joose/drill-2026-10-12`
4. keep working: this second commit proves the open PR updates itself (no new PR needed)

## The collision these rebases resolved

The base branch independently grew its own version of this file (add/add conflict) — real-life
case: upstream or a teammate touches the same file. Resolution rule: keep one side, fold in the
other's contribution, `git add`, `git rebase --continue`.
