# Jenkins LTS on the reference machine (R8 / ADR-16)

Per BRIEF §3.1 A4 (owner-approved), Jenkins LTS is the R8 orchestration host.
Install is **portable** (no MSI, no service, no registry changes) so it is fully
reproducible and removable:

    E:\Tools\jenkins\
      jdk-21.0.12.1+1\      Temurin JDK 21 (zip extract; the only runtime dependency)
      jdk.path              (relative JDK dir name, used by helper scripts)
      jenkins.war           Jenkins LTS 2.580.1
      home\                 JENKINS_HOME (jobs, plugin state, users)
      home\init.groovy.d\bootstrap.groovy
                            idempotent bootstrap: creates admin user
                            (admin / massbattle-ci), enables
                            FullControlOnceLoggedInAuthorizationStrategy,
                            mints the CI API token once
      api-token.txt         CI API token (written by bootstrap on first boot)
      logs\jenkins.log
      jenkins-cli.jar

## Start / stop

Start (any shell; binds 127.0.0.1 only, headless):

    set JENKINS_HOME=E:\Tools\jenkins\home
    E:\Tools\jenkins\jdk-21.0.12.1+1\bin\java.exe -Djava.awt.headless=true ^
      -Djenkins.install.runSetupWizard=false -Xmx512m ^
      -jar E:\Tools\jenkins\jenkins.war --httpPort=8080 --httpListenAddress=127.0.0.1

Stop: kill the `java.exe -jar jenkins.war` process (no service is registered).

Optional: a Windows Task Scheduler "at logon" entry running the start line keeps
Jenkins alive across reboots so the `H 2 * * *` nightly cron always fires. This
is process supervision only -- Jenkins remains the orchestration host (A4).

## Jobs (defined via HTTP API; auth = `-u admin:<token from api-token.txt>`)

| Job | Type | Definition | Schedule |
|-----|------|------------|----------|
| `massbattle-gate` | pipeline from SCM (`Jenkinsfile`, branch main) | checkout -> UBT editor build -> `run_tests.bat unit` -> archive logs | on demand / per-commit (webhook or manual; git hooks on this box are local-only) |
| `massbattle-nightly` | pipeline (inline) | checkout -> `Tools/CI/nightly.bat` -> archive staged-run logs + baselines | cron `H 2 * * *` |

Scripted management (all without crumb; API token bypasses CSRF):

    curl -u admin:<token> http://127.0.0.1:8080/job/massbattle-gate/build
    curl -u admin:<token> http://127.0.0.1:8080/job/massbattle-nightly/lastBuild/api/json

## Security posture (documented decision)

- Binds `127.0.0.1` only; single trusted user (admin), full-control-when-logged-in.
- The admin password and API token sit in plaintext on this box by design
  (single-operator reference machine). Do NOT expose this host to the network
  without rotating the credentials and enabling HTTPS.
- Plugins installed: workflow-aggregator (Pipeline suite), git, timestamper.

## Notes

- Jenkins 2.580 blocks anonymous administrative POSTs even with the setup
  wizard disabled (bootstrap groovy exists precisely to set up credentials).
- CSRF crumbs are session-bound in 2.580; scripted access must use the API
  token (Basic auth with an API token skips the crumb).
- The first nightly ran via `nightly.bat` directly (manual first-run evidence,
  Task 1); the Jenkins job wraps exactly the same script for subsequent runs.
