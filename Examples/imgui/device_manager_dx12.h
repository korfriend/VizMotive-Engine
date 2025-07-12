#include <dxgi1_6.h>
#include <algorithm>
#include <vector>
#include <string>


// Check if GPU is discrete (not integrated)
bool IsDiscreteGPU(const DXGI_ADAPTER_DESC1& desc)
{
	std::wstring description(desc.Description);
	// Convert to lowercase using Windows API (most efficient)
	CharLowerW(description.data());

	// Check dedicated video memory (512MB+ considered discrete)
	if (desc.DedicatedVideoMemory < 512 * 1024 * 1024)
		return false;

	// Exclude integrated GPU patterns
	std::vector<std::wstring> integratedPatterns = {
		L"intel(r) hd graphics",
		L"intel(r) uhd graphics",
		L"intel(r) iris",
		L"amd radeon(tm) graphics",  // APU integrated
		L"amd radeon(tm) vega",      // APU Vega
		L"microsoft basic render"
	};

	for (const auto& pattern : integratedPatterns)
	{
		if (description.find(pattern) != std::wstring::npos)
			return false;
	}

	// Check discrete GPU patterns
	std::vector<std::wstring> discretePatterns = {
		L"geforce", L"quadro", L"tesla",
		L"radeon rx", L"radeon pro", L"radeon hd",
		L"radeon r9", L"radeon r7", L"radeon r5"
	};

	for (const auto& pattern : discretePatterns)
	{
		if (description.find(pattern) != std::wstring::npos)
			return true;
	}

	// Vendor-specific checks
	if (desc.VendorId == 0x10DE) // NVIDIA
	{
		return desc.DedicatedVideoMemory > 1024 * 1024 * 1024; // 1GB+
	}
	else if (desc.VendorId == 0x1002) // AMD  
	{
		return (desc.DedicatedVideoMemory / 1024) > 2048 * 1024; // 2GB+
	}
	else if (desc.VendorId == 0x8086) // Intel
	{
		return description.find(L"arc") != std::wstring::npos;
	}

	// Default: judge by memory size
	return desc.DedicatedVideoMemory > 1024 * 1024 * 1024; // 1GB+
}

// Calculate GPU priority (higher = better)
int CalculateGPUPriority(const DXGI_ADAPTER_DESC1& desc)
{
	int priority = 0;
	std::wstring description(desc.Description);
	CharLowerW(description.data());

	// Check if it's discrete GPU
	if (IsDiscreteGPU(desc))
	{
		priority += 1000; // Big bonus for discrete GPUs
	}

	// Vendor-specific priorities
	if (desc.VendorId == 0x10DE) // NVIDIA
	{
		priority += 500;

		if (description.find(L"rtx") != std::wstring::npos)
			priority += 200; // RTX series
		else if (description.find(L"gtx") != std::wstring::npos)
			priority += 100; // GTX series
	}
	else if (desc.VendorId == 0x1002) // AMD
	{
		priority += 400;

		if (description.find(L"rx") != std::wstring::npos)
			priority += 150; // RX series
	}
	else if (desc.VendorId == 0x8086) // Intel
	{
		if (description.find(L"arc") != std::wstring::npos)
			priority += 300; // Intel Arc
		else
			priority += 10;  // Integrated graphics
	}

	// Memory size bonus (10 points per GB)
	priority += static_cast<int>(desc.DedicatedVideoMemory / (1024 * 1024 * 1024)) * 10;

	return priority;
}

// Helper function to find discrete GPU
IDXGIAdapter1* FindDiscreteGPU()
{
	IDXGIFactory1* factory = nullptr;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		return nullptr;

	IDXGIAdapter1* bestAdapter = nullptr;
	IDXGIAdapter1* adapter = nullptr;
	int bestPriority = -1000;
	UINT adapterIndex = 0;

	while (factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		// Skip software renderers
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			adapter->Release();
			adapterIndex++;
			continue;
		}

		int priority = CalculateGPUPriority(desc);

		// Debug output
		char adapterName[256];
		WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, 256, nullptr, nullptr);
		printf("Found GPU: %s (Priority: %d, VRAM: %llu MB)\n",
			adapterName, priority, desc.DedicatedVideoMemory / (1024 * 1024));

		if (priority > bestPriority)
		{
			if (bestAdapter != nullptr)
				bestAdapter->Release();

			bestAdapter = adapter;
			bestPriority = priority;
			adapter = nullptr; // Don't release this one
		}

		if (adapter != nullptr)
			adapter->Release();

		adapterIndex++;
	}

	factory->Release();

	if (bestAdapter != nullptr)
	{
		DXGI_ADAPTER_DESC1 desc;
		bestAdapter->GetDesc1(&desc);
		char adapterName[256];
		WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, 256, nullptr, nullptr);
		printf("Selected GPU: %s (Priority: %d)\n", adapterName, bestPriority);
	}

	return bestAdapter;
}

ID3D12Device* CreateDeviceHelper()
{
	ID3D12Device* pd3dDevice = nullptr;
	IDXGIAdapter1* discreteAdapter = FindDiscreteGPU();
	if (discreteAdapter != nullptr)
	{

		D3D_FEATURE_LEVEL featurelevels[] = {
			D3D_FEATURE_LEVEL_12_2,
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};
		for (auto& featureLevel : featurelevels)
		{
			if (SUCCEEDED(D3D12CreateDevice(discreteAdapter, featureLevel, IID_PPV_ARGS(&pd3dDevice))))
			{
				// Successfully created device with discrete GPU
				char adapterName[256];
				DXGI_ADAPTER_DESC1 desc;
				discreteAdapter->GetDesc1(&desc);
				WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, 256, nullptr, nullptr);
				printf("Created D3D12 device with discrete GPU: %s\n", adapterName);
				break;
			}
		}
		discreteAdapter->Release();
	}

	// Fallback to default device creation if discrete GPU failed
	if (pd3dDevice == nullptr)
	{
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_1;
		if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&pd3dDevice)) != S_OK)
			return nullptr;
		printf("Created D3D12 device with default adapter\n");
	}
	return pd3dDevice;
}