// MassBattle per-commit gate pipeline (R8, BRIEF Task 1.3).
// Host: Jenkins LTS running on the reference machine (see Tools/CI/JENKINS.md,
// ADR-16). Nightly packaging + Gauntlet lives in Tools/CI/nightly.bat, wired to
// a Jenkins cron trigger on the nightly job.
pipeline {
    agent any

    options {
        timestamps()
        timeout(time: 60, unit: 'MINUTES')
        disableConcurrentBuilds()
    }

    parameters {
        string(name: 'TIER', defaultValue: 'unit', description: 'Automation tier: unit or func')
    }

    environment {
        ENGINE_DIR = 'E:\\Unreal\\UE_5.8'
        PROJECT = 'E:\\Unreal Projects\\MassBattle\\MassBattle.uproject'
    }

    stages {
        stage('Checkout') {
            steps {
                checkout scm
            }
        }

        stage('Build editor target') {
            steps {
                bat '"%ENGINE_DIR%\\Engine\\Build\\BatchFiles\\Build.bat" MassBattleEditor Win64 Development -Project="%PROJECT%" -WaitMutex'
            }
        }

        stage('Per-commit automation gate') {
            steps {
                bat 'Tools\\CI\\run_tests.bat %TIER%'
            }
        }
    }

    post {
        always {
            archiveArtifacts artifacts: 'Tools/CI/last_run.json, Tools/CI/last_run/**', allowEmptyArchive: true
        }
        failure {
            echo 'Gate FAILED -- see archived Tools/CI/last_run/<tier>.log for the failing test and its errors.'
        }
    }
}
