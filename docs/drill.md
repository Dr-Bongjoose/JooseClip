# GitHub workflow drill

Live drill on Dr-Bongjoose/JooseClip, 2026-10-12: walking the complete branch → commit
→ push → pull request → rebase → merge loop with a real PR. Throwaway teaching branch —
this PR merges into a throwaway sandbox branch and is deleted; nothing ships from here.

Resolved add/add conflict, 2026-10-12: the base branch independently grew its own version of
this file — real-life case, upstream or a teammate touches the same file you are touching.
Resolution rule: keep one side, fold in the other's contribution, `git add` + rebase --continue.
