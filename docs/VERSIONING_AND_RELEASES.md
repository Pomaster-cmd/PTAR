# Versioning and Release Rules

PTAR uses semantic-style project snapshots:

- MAJOR: incompatible algorithm architecture or corpus protocol break
- MINOR: new algorithm specialist/reconstruction path or benchmark protocol extension
- PATCH: non-semantic implementation fixes and documentation corrections

Every release ZIP must contain:
- `PROJECT_STATE.json`
- `README.md`
- decision log
- corpus manifests
- benchmark protocols
- raw result tables when available
- source code
- validation reports
- `MANIFEST_SHA256.txt`

Nothing is deleted from historical releases.
A superseded result is marked superseded; it is not removed.
