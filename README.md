# VizMotive Engine

A modern, high-performance framework for visualization and vision tasks, built on proven industry foundations. The framework aims to seamlessly integrate native AI modules, enabling high-performance vision processing and real-time AI capabilities directly within the graphics pipeline.

## Overview

VizMotive Engine is a professional-grade graphics and computation framework, incorporating design principles from established engines while introducing innovative features for contemporary visualization and vision applications.

### Core Features
- High-performance rendering pipeline based on modern graphics architecture
- COM-based high-level API design for robust integration
- Comprehensive engine-level APIs with clean abstractions
- First-class support for DirectX 12 and Vulkan
- Modular architecture enabling flexible extension

## Architecture

The engine builds upon the architectural strengths of [Wicked Engine](https://github.com/turanszkij/WickedEngine) and [Filament](https://github.com/google/filament), while introducing advanced rendering capabilities and vision-specific optimizations.

## Project Structure

```
VizMotive/
├── EngineCore/          # Core engine implementation
├── GraphicsBackends/    # Graphics API implementations (DX12, Vulkan)
├── EngineShaders/       # Shader system implementation
├── EnginePlugins/      # Modular plugin system
├── Examples/           # Sample applications and demos
└── BUILD/             # Build configuration for Windows 10+
```

## Installation

### Windows Requirements
- Windows 10 or later
- Visual Studio 2019 or later
- DirectX 12 compatible GPU

### Build Instructions
1. Open the solution in Visual Studio
2. Select the 'Engine Build' filter
3. Build the 'Install' project
4. Access the compiled binaries and headers in `VizMotive-root/Install`

## Development

The engine is structured to provide clear separation of concerns while maintaining high performance:
- Core functionality is isolated in the engine layer
- Graphics backends are implemented as pluggable modules
- Shader system provides a unified interface across graphics APIs
- Plugin architecture enables extensibility without core modifications
