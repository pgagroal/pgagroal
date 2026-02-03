\newpage

## Git guide

Here are some links that will help you

* [How to Squash Commits in Git][git_squash]
* [ProGit book][progit]

**Start by forking the repository**

This is done by the "Fork" button on GitHub.

**Clone your repository locally**

This is done by

```sh
git clone git@github.com:<username>/pgagroal.git
```

**Add upstream**

Do

```sh
cd pgagroal
git remote add upstream https://github.com/pgagroal/pgagroal.git
```

**Do a work branch**

```sh
git checkout -b mywork main
```

**Make the changes**

Remember to verify the compile and execution of the code.

Use

```
[#xyz] Description
```

as the commit message where `[#xyz]` is the issue number for the work, and
`Description` is a short description of the issue in the first line

**Multiple commits**

If you have multiple commits on your branch then squash them

``` sh
git rebase -i HEAD~2
```

for example. It is `p` for the first one, then `s` for the rest

**Rebase**

Always rebase

``` sh
git fetch upstream
git rebase -i upstream/main
```

**Force push**

When you are done with your changes force push your branch

``` sh
git push -f origin mywork
```

and then create a pull request for it

**Format source code**

Use the `clang-format.sh` script from the root directory of the project to apply consistent formatting:

``` sh
./clang-format.sh
```

Note that the project uses `clang-format` version 21 (or higher).

**Repeat**

Based on feedback keep making changes, squashing, rebasing and force pushing

**PTAL**

When you are working on a change put it into Draft mode, so we know that you are not
happy with it yet.

Please, send a PTAL to the Committer that were assigned to you once you think that
your change is complete. And, of course, take it out of Draft mode.

**Undo**

Normally you can reset to an earlier commit using `git reset <commit hash> --hard`.

But if you accidentally squashed two or more commits, and you want to undo that, you need to know where to reset to, and the commit seems to have lost after you rebased.

But they are not actually lost - using `git reflog`, you can find every commit the HEAD pointer has ever pointed to. Find the commit you want to reset to, and do `git reset --hard`.
