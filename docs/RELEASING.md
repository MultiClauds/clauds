# How to make a release

This page is for the maintainer.

## How updates work

- The app uses [Sparkle](https://sparkle-project.org) to update itself.
- The app reads the feed `appcast.xml` from the `main` branch of this repository.
- The feed lists each version, its release notes, the download address (a DMG file in a GitHub
  release), and an **EdDSA signature** of that DMG.
- The app has the **public key** (`SUPublicEDKey` in `build.sh`). It installs an update only if the
  signature is valid for that key.
- The **secret key** is only in the maintainer's macOS Keychain, under the account name
  `claude-switcher` (the first name of this project). It is not in this repository, and it must never be.

So a person who can change the repository or a release, but does not have the secret key, cannot
make the app install a changed file.

## The secret key

Make a backup of the secret key, and keep it in a safe place (for example a password manager):

```bash
.build/artifacts/sparkle/Sparkle/bin/generate_keys --account claude-switcher -x ~/clauds-update-key.txt
```

Delete the file after you saved its content. To put the key on a different Mac:

```bash
.build/artifacts/sparkle/Sparkle/bin/generate_keys --account claude-switcher -f ~/clauds-update-key.txt
```

If the key is lost, installed copies cannot update themselves any more. Users must then install a
new version by hand one time.

If the key is stolen, a thief who also controls the feed can sign a bad update. Then make a new
key, put the new public key in `build.sh`, and tell users to install the next version by hand.

## Steps

The project uses Gitflow (see [CONTRIBUTING.md](../CONTRIBUTING.md#branches-gitflow)). A release
goes from `develop`, through a `release/<version>` branch, to `main`. An urgent fix goes through a
`hotfix/<version>` branch that starts from `main`. The steps are the same for both.

1. Make the release branch:

   ```bash
   git switch develop && git pull
   git switch -c release/<version>
   ```

2. Set the new version in `VERSION` (for example `1.0.2`).
3. Add the version to `CHANGELOG.md`.
4. Write the release notes that users see in the update window: `release-notes/<version>.html`.
5. Commit.
6. Make the DMG and the signed feed:

   ```bash
   ./release.sh
   ```

   macOS can ask for permission to use the key in the Keychain. The script builds a universal app,
   makes `build/Clauds-<version>.dmg`, signs it, adds the version to `appcast.xml`, and
   checks the signature.
7. Commit `appcast.xml`. Push the branch, and open a pull request to `main`. Do **not** merge yet:

   ```bash
   git push -u origin release/<version>
   gh pr create --base main --title "Release <version>" --body "Release <version>"
   ```

8. Wait for the build check. Then tag the last commit of the release branch, push the tag, and
   make the GitHub release with the DMG. The file must be online before the feed on `main` points
   to it:

   ```bash
   git tag -a v<version> -m "Clauds <version>"
   git push origin v<version>
   gh release create v<version> build/Clauds-<version>.dmg --title "Clauds <version>" --notes-file <notes.md>
   ```

   The release notes on GitHub must say that the download is not notarized, and give the SHA-256
   checksum that `release.sh` printed.
9. Merge the pull request into `main` with a merge commit. From this moment, installed copies see
   the update:

   ```bash
   gh pr merge --merge
   ```

10. Bring the release back into `develop`, with a pull request from `main` to `develop`:

    ```bash
    gh pr create --base develop --head main --title "Merge release <version> into develop" --body "Back-merge"
    gh pr merge --merge
    ```

## Rules

- Never build the DMG a second time after `appcast.xml` is made. A new DMG has a different
  signature.
- Never change a DMG in a published release. Make a new version.
- The version must go up. `CFBundleVersion` and `CFBundleShortVersionString` both come from `VERSION`.
