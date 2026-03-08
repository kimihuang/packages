# Demo Application

## Description
A simple example application demonstrating Buildroot external package infrastructure for Quantum Board.

## Features
- Simple command-line interface with help and version options
- Configurable greeting count
- Cross-platform compatible

## Usage

### Basic usage:
```bash
demo-app
```

### Show help:
```bash
demo-app --help
```

### Show version:
```bash
demo-app --version
```

### Print greeting multiple times:
```bash
demo-app -i 5
```

## Building with Buildroot
This package is automatically available in Buildroot menuconfig under:
```
Board options -> Quantum Board Packages -> demo-app
```

## License
GPL-2.0
