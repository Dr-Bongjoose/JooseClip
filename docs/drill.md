# Upstream-vs-branch collision (drill sandbox)

This version of docs/drill.md was added INDEPENDENTLY on the base branch — simulating a real
situation: upstream (or a teammate) adds/fixes the same file your branch is touching. When your
branch rebases onto a base that gained the file, git reports an add/add conflict, and you
resolve it by hand: keep one side, fold in what the other side contributed, re-commit.