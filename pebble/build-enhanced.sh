#!/bin/bash
# Enhanced Pebble Build Script
# Builds the enhanced UX version of the Pebble watchface

echo "Building Enhanced Pebble Watchface..."

# Check if pebble/rebble SDK is available
if ! command -v pebble &> /dev/null; then
    echo "Error: Pebble/Rebble SDK not found. Please install pebble-sdk first."
    echo "Visit: https://developer.getpebble.com/sdk/install/"
    exit 1
fi

# Navigate to enhanced directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR/enhanced" || {
    echo "Error: Could not find enhanced directory"
    exit 1
}

# Clean previous build
echo "Cleaning previous build..."
pebble clean || echo "No previous build to clean"

# Install dependencies (if any)
echo "Installing dependencies..."
npm install || echo "npm install failed, continuing..."

# Build the app for basalt platform (Pebble Time/Steel/Time 2)
echo "Building for basalt platform (Pebble Time/Steel/Time 2)..."
pebble build --platform basalt || {
    echo "Build failed!"
    exit 1
}

echo "Build successful!"
echo "PBW file created: build/basalt/enhanced.pbw"

# Show build info
ls -la build/basalt/enhanced.pbw

echo ""
echo "To install on device:"
echo "1. Enable Developer Mode on your Pebble watch"
echo "2. Run: pebble install --phone <phone_ip> --platform basalt"
echo ""
echo "To install in emulator:"
echo "1. Run: pebble emulator --platform basalt"
echo "2. In another terminal: pebble install --platform basalt"