# Project instructions

These user rules apply to this project and take precedence over earlier workflow preferences.

- Do not build or compile code. The user performs all building.
- Do not interact with Git: no status, diff, log, staging, commits, tags, fetches, pushes, or other Git operations.
- Do not run regression tests or other test suites. Existing tests are in tests; the user performs testing.
- Do not run local tools unless needed to add, modify, or remove files within the provided project folders. Limit inspection to what is necessary for those file edits.
- Always use incremental Major.Minor.Patch versioning. For versioned changes, increment the appropriate component for the scope of the change; never reuse or decrement a released version.
- Each change set must have a unique incremental Major.Minor.Patch version. Patch filenames must include the project and resulting version, never a date. Never overwrite or reuse a versioned patch.
- After every run that changes files, create a corresponding unified .patch file in the project's patches folder. Capture the original contents before editing and include all additions, modifications, and removals from that run, excluding the generated patch itself. The patch must support reverse application for rollback. Generate it without Git. A discussion-only run has no change set and needs no empty patch or version bump.
- Do not build, test, or apply the rollback patch to verify it. Report validation as not run and leave actual building and testing to the user.

Hardware regression testing is explicitly skipped. Do not make it a prerequisite for migration work.

## Project context

Hub is the cloud chamber application; ZapEE contains reusable Zaptronics embedded components and board support. The sibling Zap project is the canonical source of truth for package-manager formats and behavior. Do not infer Zap behavior from potentially outdated Hub or ZapEE manifests or documentation.
