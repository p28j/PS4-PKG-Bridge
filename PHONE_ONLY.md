# Phone-only build guide

This project is prepared to be built in GitHub Actions, so no PC is required locally.

1. Create a GitHub repository from your phone.
2. Upload the contents of this folder (not the ZIP itself).
3. The workflow is already in `.github/workflows/build.yml`.
4. Open Actions -> Build PS4 PKG Bridge -> Run workflow.
5. The `toolchain-smoke` job first checks the official OpenOrbis `net_http` sample build.
6. The `bridge-build` job then substitutes this prototype source, builds it with the same official sample project files, packages a PKG, and publishes it as an artifact.

Important: a successful cloud build does not mean the application has been tested on a real PS4 9.00. The first runtime test is still required.
