# Security Policy


## Supported versions

Only the latest published release receives security updates.
See the [Releases](https://github.com/xplus2/dvbipitools/releases) page for the current version.


## Reporting a vulnerability

If you believe you have found a security issue in `dvbipitools`, please **do not** open a public issue.

Use GitHub's private vulnerability reporting instead:
go to the [Security tab](https://github.com/xplus2/dvbipitools/security/advisories/new) and open a draft advisory.
We will confirm receipt within a few days, investigate, and coordinate a fix and disclosure timeline with you.

To reduce back-and-forth, please provide the following information where applicable:
* affected tool and version or commit
* build options and platform
* attack preconditions
* reproduction steps, proof of concept, or a core dump from a debug build
* expected impact
* whether the issue is already public
* in case of a tool-assisted finding, any information about the tool is welcome


Please check core dumps and other diagnostic files for credentials, personal data, or other sensitive information
before attaching them. 
If it was not a debug build, it is probably better not to attach the core dump.


## What if we blow it

This is a small project, and any human being can be unavailable for various reasons from time to time.

If there is no response after 30 days, you may disclose the issue responsibly, 
taking reasonable care not to expose users to unnecessary harm.

If we may ask you a favor, when publicly reporting the issue:
* Double-check that it is not a false positive.
* Be clear about the scope: which tool is affected, and under which conditions.
* If possible, provide a mitigation, such as: “Do not use untrusted Icecast sources with `dipiradiohead`.”
* A narrowly scoped proposed fix or hotfix fork is welcome, but not required. 
  If you provide one, please keep the fix isolated from unrelated changes so that a user base already 
  dealing with a 0-day can review it as easily as possible.
