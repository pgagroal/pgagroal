# Contributing guide

**Want to contribute? Great!** 

All contributions are more than welcome ! This includes bug reports, bug fixes, enhancements, features, questions, ideas,
and documentation.

This document will hopefully help you contribute to pgagroal.

* [Legal](#legal)
* [Reporting an issue](#reporting-an-issue)
* [Setup your build environment](#setup-your-build-environment)
* [Building the master branch](#building-the-master-branch)
* [Before you contribute](#before-you-contribute)
* [Code reviews](#code-reviews)
* [Coding Guidelines](#coding-guidelines)
* [Discuss a Feature](#discuss-a-feature)
* [Development](#development)
* [Code Style](#code-style)

## Legal

All contributions to pgagroal are licensed under the [The 3-Clause BSD License](https://opensource.org/licenses/BSD-3-Clause).

## Reporting an issue

This project uses GitHub issues to manage the issues. Open an issue directly in GitHub.

If you believe you found a bug, and it's likely possible, please indicate a way to reproduce it, what you are seeing and what you would expect to see.
Don't forget to indicate your pgagroal version. 

## Setup your build environment

You can use the follow command, if you are using a [Fedora](https://getfedora.org/) based platform:

```
dnf install git gcc cmake make liburing liburing-devel openssl openssl-devel systemd systemd-devel python3-docutils libasan libasan-static binutils clang clang-analyzer clang-tools-extra
```

in order to get the necessary dependencies.

## Building the master branch

To build the `master` branch:

```
git clone https://github.com/pgagroal/pgagroal.git
cd pgagroal
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
cd src
cp ../../doc/etc/*.conf .
./pgagroal -c pgagroal.conf -a pgagroal_hba.conf
```

and you will have a running instance.

## Before you contribute

To contribute, use GitHub Pull Requests, from your **own** fork.

Also, make sure you have set up your Git authorship correctly:

```
git config --global user.name "Your Full Name"
git config --global user.email your.email@example.com
```

We use this information to acknowledge your contributions in release announcements.

## Code reviews

GitHub pull requests can be reviewed by all such that input can be given to the author(s).

See [GitHub Pull Request Review Process](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/reviewing-changes-in-pull-requests/about-pull-request-reviews)
for more information.

## Coding Guidelines

* Discuss the feature
* Do development
  + Follow the code style
* Commits should be atomic and semantic. Therefore, squash your pull request before submission and keep it rebased until merged
  + If your feature has independent parts submit those as separate pull requests

## Discuss a Feature

You can discuss bug reports, enhancements and features in our [forum](https://github.com/pgagroal/pgagroal/discussions).

Once there is an agreement on the development plan you can open an issue that will used for reference in the pull request.

## Development

You can follow this workflow for your development.

Add your repository

```
git clone git@github.com:yourname/pgagroal.git
cd pgagroal
git remote add upstream https://github.com/pgagroal/pgagroal.git
```

Create a work branch

```
git checkout -b mywork master
```

During development

```
git commit -a -m "[#issue] My feature"
git push -f origin mywork
```

If you have more commits then squash them

```
git rebase -i HEAD~2
git push -f origin mywork
```

If the `master` branch changes then

```
git fetch upstream
git rebase -i upstream/master
git push -f origin mywork
```

as all pull requests should be squashed and rebased.

In your first pull request you need to add yourself to the `AUTHORS` file.

## Policy and guidelines for using AI

Our goal in the pgagroal project is to develop an excellent software system. This requires careful attention to
detail in every change we integrate. Maintainer time and attention is very limited, so it's important that changes
you ask us to review represent your best work.

You are encouraged to use tools that help you write good code, including AI tools. However, as noted above, you always
need to understand and explain the changes you're proposing to make, whether or not you used an LLM as part of
your process to produce them. The answer to “Why did you make change X?” should never be “I'm not sure. The AI did it.”

Do not submit an AI-generated PR you haven't personally understood and tested, as this wastes maintainers' time.
PRs that appear to violate this guideline will be closed without review. Using AI as a coding assistant and help you
create a skeleton for your code.

* Don't skip becoming familiar with the part of the codebase you're working on. This will let you write better prompts
  and validate their output if you use an LLM. Code assistants can be a useful search engine/discovery tool in this process,
  but don't trust claims they make about how pgagroal works. LLMs are often wrong, even about details that are clearly
  answered in the pgagroal documentation and surrounding code
* Don't simply ask an LLM to add code comments, as it will likely produce a bunch of text that unnecessarily explains
  what's already clear from the code. If using an LLM to generate comments, be really specific in your request,
  demand succinctness, and carefully edit the result.

### Using AI for communication

As noted above, pgagroal's contributors are expected to communicate with intention, to avoid wasting maintainer time
with long, sloppy writing. We strongly prefer clear and concise communication about points that actually require discussion
over long AI-generated comments.

When you use an LLM to write a message for you, it remains your responsibility to read through the whole thing and make sure
that it makes sense to you and represents your ideas concisely. A good rule of thumb is that if you can't make yourself
carefully read some LLM output that you generated, nobody else wants to read it either.

Here are some concrete guidelines for using LLMs as part of your communication workflows.

* When writing a pull request description, do not include anything that's obvious from looking at your changes directly
  (e.g., files changed, functions updated, etc.). Instead, focus on the why behind your changes. Don't ask an LLM to
  generate a PR description on your behalf based on your code changes, as it will simply regurgitate the information
  that's already there.
* Similarly, when responding to a pull request comment, explain your reasoning. Don't prompt an LLM to re-describe what
  can already be seen from the code.
* Verify that everything you write is accurate, whether or not an LLM generated any part of it. pgagroal's maintainers
  will be unable to review your contributions if you misrepresent your work (e.g., misdescribing your code changes,
  their effect, or your testing process).
* Complete all parts of the PR description template, maybe with screenshots and the self-review checklist.
  Don't simply overwrite the template with LLM output.
* Clarity and succinctness are much more important than perfect grammar, so you shouldn't feel obliged to pass your writing
  through an LLM. If you do ask an LLM to clean up your writing style, be sure it does not make it longer in the process.
  Demand succinctness in your prompt.
* Quoting an LLM answer is usually less helpful than linking to relevant primary sources, like source code,
  reference documentation, or web standards. If you do need to quote an LLM answer in a pgagroal conversation,
  put the answer in a pgagroal quote block, to distinguish LLM output from your own thoughts.

## Code Style

Please, follow the coding style of the project when submitting contributions.

You can use the [clang-format](https://clang.llvm.org/docs/ClangFormat.html) tool to help with the formatting, by running the script `clang-format.sh` from the root directory of the project:

```
./clang-format.sh
```

and verify the changes.



### Installation of `clang-format`

This project uses `clang-format` version 21 (or higher).
The CI uses `clang-format` to ensure consistent formatting.
Please install clang-format locally to match the CI environment.

#### Fedora

```
dnf install clang-tools-extra
```

#### Ubuntu/Debian

```
apt install clang-format
```

or to specifically install version 21

```
sudo apt update -y
sudo apt install -y lsb-release wget software-properties-common gnupg
wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo gpg --dearmor -o /usr/share/keyrings/llvm-snapshot.gpg
echo "deb [signed-by=/usr/share/keyrings/llvm-snapshot.gpg] http://apt.llvm.org/$(lsb_release -cs)/ llvm-toolchain-$(lsb_release -cs)-21 main" | sudo tee /etc/apt/sources.list.d/llvm.list
sudo apt update -y
sudo apt remove -y clang-format 2>/dev/null || true
sudo apt install -y clang-format-21
sudo ln -sf /usr/bin/clang-format-21 /usr/bin/clang-format
```


#### RHEL/Rocky

```
dnf install clang-tools-extra   # or version-specific package if available
```
