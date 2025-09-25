#!/bin/zsh
# check.sh - Run checkov security checks on CI/CD configuration files

set -euo pipefail

ERRORS=0

# Run checkov on GitHub Actions workflows
echo "Checking GitHub Actions workflows..."
if ! checkov --framework github_actions --directory .github/workflows --compact --quiet; then
    ERRORS=$((ERRORS + 1))
fi

# Run checkov on CircleCI config
echo "Checking CircleCI configuration..."
if ! checkov --framework circleci_pipelines --file .circleci/config.yml --compact --quiet; then
    ERRORS=$((ERRORS + 1))
fi

# Exit with error count
if [[ $ERRORS -gt 0 ]]; then
    echo "Checks failed with $ERRORS error(s)"
    exit $ERRORS
else
    echo "All checks passed"
fi