# Security Policy

## Beta Software Notice

Felidae is currently in **beta** and is under active development by Xnovity, contributors, and project supporters.

Beta releases are intended for **evaluation, development, experimentation, and non-critical workflows**. They provide users and contributors an opportunity to understand, test, and evaluate Felidae before a stable production release.

**Production use is not currently recommended.**

During the beta period:

* Do not rely on Felidae for safety-critical, financial, legal, medical, security-critical, or other high-risk decisions.
* Do not take irreversible actions solely based on outputs produced by Felidae.
* Outputs, reasoning behavior, APIs, file formats, and runtime behavior may change between beta releases.
* Bugs, incomplete functionality, unexpected behavior, and security vulnerabilities may still exist.
* Important data should be backed up before using Felidae for testing or evaluation.

Users are encouraged to independently verify outputs when evaluating Felidae.

## Supported Versions

Security fixes are provided for the currently supported beta release.

| Version          | Supported |
| ---------------- | --------- |
| `0.2.3-beta.1`   | ✅         |
| `< 0.2.3-beta.1` | ❌         |

As Felidae progresses through beta development, older beta versions may stop receiving security updates. Users should generally test against the latest supported release.

## Reporting a Vulnerability

If you discover a security vulnerability while reviewing, testing, or using Felidae, please report it privately rather than publicly disclosing the issue.

Contact:

* **[info@xnovity.com](mailto:info@xnovity.com)**
* **[support@xnovity.com](mailto:support@xnovity.com)**

When possible, include:

* A description of the vulnerability
* The affected Felidae version
* Steps required to reproduce the issue
* Potential security impact
* Relevant logs, traces, or example inputs
* Any suggested mitigation or fix, if known

Please avoid including credentials, private keys, personal information, production data, or other sensitive information in reports unless specifically requested through an appropriate secure channel.

## Vulnerability Disclosure

Please allow the maintainers reasonable time to investigate and address a reported vulnerability before publicly disclosing technical details.

Confirmed vulnerabilities may result in:

1. Investigation and impact assessment
2. Development and testing of a fix
3. Release of a patched version
4. Publication of relevant security information

Security advisories and updates concerning reported vulnerabilities will be published through the project's **GitHub Security Advisories and/or GitHub Wiki**, as appropriate.

## Security Updates

Users of beta releases are encouraged to keep Felidae and its dependencies up to date and periodically review the repository for new releases and security advisories.

Thank you for helping improve the security and reliability of Felidae.
