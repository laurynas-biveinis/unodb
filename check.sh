#!/bin/zsh
# check.sh - Run checkov security checks on CI/CD configuration files

set -euo pipefail

ERRORS=0

# Find all markdown files (excluding 3rd_party directories)
MARKDOWN_FILES=()
while IFS= read -r -d '' file; do
    MARKDOWN_FILES+=("$file")
done < <(find . -path "./3rd_party" -prune -o -type f -name "*.md" -print0 2>/dev/null || true)

# Find all JSON files (excluding 3rd_party directories and build directories)
JSON_FILES=()
while IFS= read -r -d '' file; do
    JSON_FILES+=("$file")
done < <(find . -path "./3rd_party" -prune -o -path "./build" -prune -o -type f -name "*.json" -print0 2>/dev/null || true)

# Find all YAML files (excluding 3rd_party directories and build directories)
YAML_FILES=()
while IFS= read -r -d '' file; do
    YAML_FILES+=("$file")
done < <(find . -path "./3rd_party" -prune -o -path "./build" -prune -o -type f \( -name "*.yml" -o -name "*.yaml" \) -print0 2>/dev/null || true)

# Run checkov on GitHub Actions workflows
echo "Checking GitHub Actions workflows..."
if ! checkov --framework github_actions --directory .github/workflows --compact --quiet; then
    ERRORS=$((ERRORS + 1))
fi

# Run zizmor security check on GitHub Actions
echo -n "Checking GitHub Actions security... $(echo .github/workflows/*.yml) "
if zizmor .github/workflows/*.yml; then
    echo "OK!"
else
    echo "zizmor check failed!"
    ERRORS=$((ERRORS + 1))
fi

# Prettier checks
echo -n "Checking Markdown formatting... "
if [ ${#MARKDOWN_FILES[@]} -gt 0 ]; then
    if prettier --log-level warn --check "${MARKDOWN_FILES[@]}"; then
        echo "OK!"
    else
        echo "prettier check for Markdown failed"
        ERRORS=$((ERRORS + 1))
    fi
else
    echo "No markdown files to check"
fi

echo -n "Checking YAML formatting... "
if [ ${#YAML_FILES[@]} -gt 0 ]; then
    if prettier --log-level warn --check "${YAML_FILES[@]}"; then
        echo "OK!"
    else
        echo "prettier check for YAML failed!"
        ERRORS=$((ERRORS + 1))
    fi
else
    echo "No YAML files to check"
fi

# Yamllint check
echo -n "Running yamllint... "
if [ ${#YAML_FILES[@]} -gt 0 ]; then
    if yamllint -c .yaml-lint.yml "${YAML_FILES[@]}"; then
        echo "OK!"
    else
        echo "yamllint check failed!"
        ERRORS=$((ERRORS + 1))
    fi
else
    echo "No YAML files to check"
fi

echo -n "Checking JSON formatting... "
if [ ${#JSON_FILES[@]} -gt 0 ]; then
    if prettier --log-level warn --check "${JSON_FILES[@]}"; then
        echo "OK!"
    else
        echo "prettier check for JSON failed"
        ERRORS=$((ERRORS + 1))
    fi
else
    echo "No JSON files to check"
fi

# Textlint terminology check
echo -n "Checking terminology... "
if [ ${#MARKDOWN_FILES[@]} -gt 0 ]; then
    if textlint --rule terminology "${MARKDOWN_FILES[@]}"; then
        echo "OK!"
    else
        echo "textlint check failed"
        ERRORS=$((ERRORS + 1))
    fi
else
    echo "No markdown files to check"
fi

# Biome checks
echo -n "Running Biome format check... "
if npx @biomejs/biome format .; then
    echo "OK!"
else
    echo "Biome format check failed!"
    ERRORS=$((ERRORS + 1))
fi

echo -n "Running Biome lint... "
if npx @biomejs/biome lint .; then
    echo "OK!"
else
    echo "Biome lint failed!"
    ERRORS=$((ERRORS + 1))
fi

# Exit with error count
if [[ $ERRORS -gt 0 ]]; then
    echo "Checks failed with $ERRORS error(s)"
    exit $ERRORS
else
    echo "All checks passed"
fi