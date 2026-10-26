# Examples

Each example is a complete program, not a fragment. `make test` runs each
one and checks the output against the `.expected` file next to it. This
makes sure that the examples continue to work.

Every example makes the repository that it uses under `examples/tmp`. The
examples do not need one of your repositories, an installed git, or a
network. To run one example:

```
CLIPSgit -f2 examples/1-history-as-facts.bat
```

| | |
| --- | --- |
| [1-history-as-facts.bat](1-history-as-facts.bat) | Commits become facts, and rules make the conclusions |
| [2-rules-over-status.bat](2-rules-over-status.bat) | The status of the working directory as facts, and a rule that makes a commit |
| [3-diff-and-blame.bat](3-diff-and-blame.bat) | The changes between two commits, and the person who last changed each line |
| [4-new-repository-to-github.bat](4-new-repository-to-github.bat) | GitHub's recipe for a new repository: init, a first commit, a remote, and a push |
| [5-existing-repository-to-github.bat](5-existing-repository-to-github.bat) | GitHub's recipe for a repository that already has commits: a remote, a rename, and a push |
