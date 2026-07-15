#!/usr/bin/env bash
set -e

REPO_NAME="Felidae"
VISIBILITY="private"   # Change to "public" if desired

echo "Initializing Git repository..."

# Initialize Git if needed
if [ ! -d ".git" ]; then
    git init
fi

# Create .gitignore if it doesn't exist
if [ ! -f ".gitignore" ]; then
cat > .gitignore <<EOF
__pycache__/
*.pyc
.env
.venv/
venv/
node_modules/
.DS_Store
.vscode/
.idea/
EOF
fi

git add .
git commit -m "Initial commit" || echo "Nothing to commit."

echo "Creating GitHub repository..."

# Requires GitHub CLI (gh)
gh repo create "$REPO_NAME" \
    --"$VISIBILITY" \
    --source=. \
    --remote=origin \
    --push

echo ""
echo "Repository created and pushed successfully!"