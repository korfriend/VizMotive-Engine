# Sample Projects

## Structure Overview
```
Examples/
├── Assets/             # Shared resources for sample applications
├── PluginSample001/    # Engine Module Plugin samples
├── Sample001/          # Application samples
├── Sample002/          # Application samples
├── Sample003/          # Application samples
├── Sample004/          # Application samples
├── glm/               # GL Mathematics library
└── imgui/             # GUI framework for samples
```

## Development Guide

### Application Development
- All samples utilize the same library files (.a) and DLLs (.so)
- Required header for application development:
  - `#include <vzm2/VzEngineAPIs.h>`
  - Additional utility headers in `vzm2/utils/` can be included as needed

### Engine Module Plugin Development
- Required header for engine-level plugin development:
  - `#include <vzmcore/Components.h>`
  - Additional utility headers in `vzmcore/utils/` can be included as needed

### Shared Utilities
- Utility headers in both `vzm2/utils/` and `vzmcore/utils/` contain identical implementations
- These utility headers can be included based on your needs regardless of development level

### Dependencies
- **GLM**: Mathematics library for graphics operations
- **ImGui**: Immediate mode GUI library for sample interfaces
- **Engine Core**: Built libraries and headers in `VizMotive-root/Install`

### Sample Assets
The `Assets` directory contains shared resources used across sample applications, including:
- Test data
- Common resources
- Sample-specific assets

## Getting Started
1. Ensure the engine is built and installed in `VizMotive-root/Install`
2. Include the required header based on your development needs:
   - Application development: `VzEngineAPIs.h`
   - Plugin development: `Components.h`
3. Reference the relevant sample project as a starting point
