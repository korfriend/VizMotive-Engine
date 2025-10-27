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

## Getting Started

### Prerequisites
- **Operating System**: Windows 10 or later
- **Development Environment**: Visual Studio 2019 or later with C++17 support
- **Graphics Hardware**: DirectX 12 compatible GPU
- **Version Control**: Git (for cloning and submodule management)

### Clone the Repository

```bash
git clone <repository-url>
cd VizMotive2
```

### Initialize Submodules

The engine depends on external libraries managed as git submodules. Initialize them before building:

```bash
git submodule update --init --recursive
```

**Important**: This step downloads the Assimp library required by the AssetIO plugin. Skipping this will cause build failures.

### Build Instructions

#### Option 1: Visual Studio (Recommended)

1. Open `VizMotive2.sln` in Visual Studio
2. In Solution Explorer, locate the **Install** project (under the InstallPackage folder)
3. Right-click the **Install** project and select **Rebuild**
4. The build system will automatically compile all dependencies in the correct order:
   - `Engine_Windows` - Core engine DLL
   - `GBackendDX12` - DirectX 12 graphics backend
   - `GBackendVulkan` - Vulkan graphics backend (optional)
   - `ShaderEngine` - Shader compilation engine
   - `AssetIO` - Asset importer plugin

#### Option 2: Command Line

Open a **Developer Command Prompt for Visual Studio** and run:

```bash
cd BUILD
build_install.bat
```

This batch file builds all essential modules in both Debug and Release configurations.

Alternatively, build individual projects:

```bash
msbuild BUILD\Engine_Windows.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild GraphicsBackends\GBackendDX12.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild EngineShaders\ShaderEngine\ShaderEngine.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild EnginePlugins\AssetIO\AssetIO.vcxproj /p:Configuration=Release /p:Platform=x64
```

### Build Output

After a successful build, the compiled binaries will be located in:

```
bin/x64_Debug/          # Debug builds
├── VizEngined.dll      # Core engine (debug)
├── GBackendDX12.dll    # DirectX 12 backend
├── GBackendVulkan.dll  # Vulkan backend
├── ShaderEngine.dll    # Shader compiler
└── AssetIO.dll         # Asset importer

bin/x64_Release/        # Release builds
├── VizEngine.dll       # Core engine (release)
├── GBackendDX12.dll
├── GBackendVulkan.dll
├── ShaderEngine.dll
└── AssetIO.dll
```

The **Install** project also packages headers and binaries into `VizMotive-root/Install` for distribution.

### Building Examples

Sample applications demonstrating engine features are located in the `Examples/` directory:

```bash
# Build a specific example
msbuild Examples\Sample001\Sample001.vcxproj /p:Configuration=Release /p:Platform=x64

# Or build all examples via Visual Studio by building the entire solution
```

Examples range from `Sample001` (basic setup) to `Sample014` (advanced features).

### Troubleshooting

#### Submodule Initialization Errors

If you encounter errors like `fatal: repository not found` when initializing submodules:

1. Verify the submodule URL in `.gitmodules`:
   ```
   [submodule "EnginePlugins/AssetIO/External/assimp"]
       path = EnginePlugins/AssetIO/External/assimp
       url = https://github.com/assimp/assimp.git
       branch = v5.4.3
   ```

2. Sync and update:
   ```bash
   git submodule sync
   git submodule update --init --recursive
   ```

#### Build Failures

- Ensure all submodules are properly initialized
- Verify Visual Studio 2019+ with C++17 support is installed
- Check that the platform is set to **x64** (Win32 is not supported)
- Clean the solution and rebuild if encountering incremental build issues

#### Missing msbuild

If `msbuild` is not found in your PATH, use the **Developer Command Prompt for Visual Studio** which automatically configures the build environment.

## Development

The engine is structured to provide clear separation of concerns while maintaining high performance:
- Core functionality is isolated in the engine layer
- Graphics backends are implemented as pluggable modules
- Shader system provides a unified interface across graphics APIs
- Plugin architecture enables extensibility without core modifications
