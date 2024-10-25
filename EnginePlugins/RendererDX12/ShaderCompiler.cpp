#include "ShaderCompiler.h"
#include "Utils/Backlog.h"
#include "Utils/Platform.h"

#include "Libs/Math.h"
#include "Utils/Helpers.h"

#include <mutex>
#include <unordered_set>

#ifdef PLATFORM_WINDOWS_DESKTOP
#define SHADERCOMPILER_ENABLED
#define SHADERCOMPILER_ENABLED_DXCOMPILER
#define SHADERCOMPILER_ENABLED_D3DCOMPILER
#include <wrl/client.h>
#define CComPtr Microsoft::WRL::ComPtr
#endif // _WIN32

#ifdef PLATFORM_LINUX
#define SHADERCOMPILER_ENABLED
#define SHADERCOMPILER_ENABLED_DXCOMPILER
#define __RPC_FAR
#include "Utility/WinAdapter.h"
#endif // PLATFORM_LINUX

#ifdef SHADERCOMPILER_ENABLED_DXCOMPILER
#include "Utility/dxcapi.h"
#endif // SHADERCOMPILER_ENABLED_DXCOMPILER

#ifdef SHADERCOMPILER_ENABLED_D3DCOMPILER
#include <d3dcompiler.h>
#endif // SHADERCOMPILER_ENABLED_D3DCOMPILER

namespace vz::graphics::shadercompiler
{

#ifdef SHADERCOMPILER_ENABLED_DXCOMPILER
	struct InternalState_DXC
	{
		DxcCreateInstanceProc DxcCreateInstance = nullptr;

		InternalState_DXC(const std::string& modifier = "")
		{
#ifdef _WIN32
			const std::string library = "dxcompiler" + modifier + ".dll";
			HMODULE dxcompiler = vzLoadLibrary(library.c_str());
#elif defined(PLATFORM_LINUX)
			const std::string library = "./libdxcompiler" + modifier + ".so";
			HMODULE dxcompiler = vzLoadLibrary(library.c_str());
#endif
			if (dxcompiler != nullptr)
			{
				DxcCreateInstance = (DxcCreateInstanceProc)vzGetProcAddress(dxcompiler, "DxcCreateInstance");
				if (DxcCreateInstance != nullptr)
				{
					CComPtr<IDxcCompiler3> dxcCompiler;
					HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
					assert(SUCCEEDED(hr));
					CComPtr<IDxcVersionInfo> info;
					hr = dxcCompiler->QueryInterface(IID_PPV_ARGS(&info));
					assert(SUCCEEDED(hr));
					uint32_t minor = 0;
					uint32_t major = 0;
					hr = info->GetVersion(&major, &minor);
					assert(SUCCEEDED(hr));
					backlog::post("shadercompiler: loaded " + library + " (version: " + std::to_string(major) + "." + std::to_string(minor) + ")");
				}
			}
			else
			{
				backlog::post("shadercompiler: could not load library " + library, backlog::LogLevel::Error);
#ifdef PLATFORM_LINUX
				backlog::post(dlerror(), backlog::LogLevel::Error); // print dlopen() error detail: https://linux.die.net/man/3/dlerror
#endif // PLATFORM_LINUX
			}

		}
	};
	inline InternalState_DXC& dxc_compiler()
	{
		static InternalState_DXC internal_state;
		return internal_state;
	}
	inline InternalState_DXC& dxc_compiler_xs()
	{
		static InternalState_DXC internal_state("_xs");
		return internal_state;
	}

	void Compile_DXCompiler(const CompilerInput& input, CompilerOutput& output)
	{
		InternalState_DXC& compiler_internal = input.format == ShaderFormat::HLSL6_XS ? dxc_compiler_xs() : dxc_compiler();
		if (compiler_internal.DxcCreateInstance == nullptr)
		{
			return;
		}

		CComPtr<IDxcUtils> dxcUtils;
		CComPtr<IDxcCompiler3> dxcCompiler;

		HRESULT hr = compiler_internal.DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
		assert(SUCCEEDED(hr));
		hr = compiler_internal.DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
		assert(SUCCEEDED(hr));

		if (dxcCompiler == nullptr)
		{
			return;
		}

		std::vector<uint8_t> shadersourcedata;
		if (!vz::helper::FileRead(input.shadersourcefilename, shadersourcedata))
		{
			return;
		}

		// https://github.com/microsoft/DirectXShaderCompiler/wiki/Using-dxc.exe-and-dxcompiler.dll#dxcompiler-dll-interface

		std::vector<std::wstring> args = {
			//L"-res-may-alias",
			//L"-flegacy-macro-expansion",
			//L"-no-legacy-cbuf-layout",
			//L"-pack-optimized", // this has problem with tessellation shaders: https://github.com/microsoft/DirectXShaderCompiler/issues/3362
			//L"-all-resources-bound",
			//L"-Gis", // Force IEEE strictness
			//L"-Gec", // Enable backward compatibility mode
			//L"-Ges", // Enable strict mode
			//L"-O0", // Optimization Level 0
			//L"-enable-16bit-types",
			L"-Wno-conversion",
		};

		if (has_flag(input.flags, Flags::DISABLE_OPTIMIZATION))
		{
			args.push_back(L"-Od");
		}

		switch (input.format)
		{
		case ShaderFormat::HLSL6:
		case ShaderFormat::HLSL6_XS:
			args.push_back(L"-rootsig-define"); args.push_back(L"ENGINE_DEFAULT_ROOTSIGNATURE");
			if (has_flag(input.flags, Flags::STRIP_REFLECTION))
			{
				args.push_back(L"-Qstrip_reflect"); // only valid in HLSL6 compiler
			}
			break;
		case ShaderFormat::SPIRV:
			args.push_back(L"-spirv");
			args.push_back(L"-fspv-target-env=vulkan1.2");
			//args.push_back(L"-fspv-target-env=vulkan1.3"); // this has some problem with RenderDoc AMD disassembly so it's not enabled for now
			args.push_back(L"-fvk-use-dx-layout");
			args.push_back(L"-fvk-use-dx-position-w");
			//args.push_back(L"-fvk-b-shift"); args.push_back(L"0"); args.push_back(L"0");
			args.push_back(L"-fvk-t-shift"); args.push_back(L"1000"); args.push_back(L"0");
			args.push_back(L"-fvk-u-shift"); args.push_back(L"2000"); args.push_back(L"0");
			args.push_back(L"-fvk-s-shift"); args.push_back(L"3000"); args.push_back(L"0");
			break;
		default:
			assert(0);
			return;
		}

		ShaderModel minshadermodel = input.minshadermodel;

		// global minshadermodel override:
		//minshadermodel = std::max(minshadermodel, ShaderModel::SM_6_2);

		args.push_back(L"-T");
		switch (input.stage)
		{
		case ShaderStage::MS:
			switch (minshadermodel)
			{
			default:
				args.push_back(L"ms_6_5");
				break;
			case ShaderModel::SM_6_6:
				args.push_back(L"ms_6_6");
				break;
			case ShaderModel::SM_6_7:
				args.push_back(L"ms_6_7");
				break;
			}
			break;
		case ShaderStage::AS:
			switch (minshadermodel)
			{
			default:
				args.push_back(L"as_6_5");
				break;
			case ShaderModel::SM_6_6:
				args.push_back(L"as_6_6");
				break;
			case ShaderModel::SM_6_7:
				args.push_back(L"as_6_7");
				break;
			}
			break;
		case ShaderStage::VS:
			switch (minshadermodel)
			{
			default:
				args.push_back(L"vs_6_0");
				break;
			case ShaderModel::SM_6_1:
				args.push_back(L"vs_6_1");
				break;
			case ShaderModel::SM_6_2:
				args.push_back(L"vs_6_2");
				break;
			case ShaderModel::SM_6_3:
				args.push_back(L"vs_6_3");
				break;
			case ShaderModel::SM_6_4:
				args.push_back(L"vs_6_4");
				break;
			case ShaderModel::SM_6_5:
				args.push_back(L"vs_6_5");
				break;
			case ShaderModel::SM_6_6:
				args.push_back(L"vs_6_6");
				break;
			case ShaderModel::SM_6_7:
				args.push_back(L"vs_6_7");
				break;
			}
			break;
		case ShaderStage::HS:
			switch (minshadermodel)
			{
			default:
				args.push_back(L"hs_6_0");
				break;
			case ShaderModel::SM_6_1:
				args.push_back(L"hs_6_1");
				break;
			case ShaderModel::SM_6_2:
				args.push_back(L"hs_6_2");
				break;
			case ShaderModel::SM_6_3:
				args.push_back(L"hs_6_3");
				break;
			case ShaderModel::SM_6_4:
				args.push_back(L"hs_6_4");
				break;
			case ShaderModel::SM_6_5:
				args.push_back(L"hs_6_5");
				break;
			case ShaderModel::SM_6_6:
				args.push_back(L"hs_6_6");
				break;
			case ShaderModel::SM_6_7:
				args.push_back(L"hs_6_7");
				break;
			}
			break;
		case ShaderStage::DS:
			switch (minshadermodel)
			{
			default:
				args.push_back(L"ds_6_0");
				break;
			case ShaderModel::SM_6_1:
				args.push_back(L"ds_6_1");
				break;
			case ShaderModel::SM_6_2:
				args.push_back(L"ds_6_2");
				break;
			case ShaderModel::SM_6_3:
				args.push_back(L"ds_6_3");
				break;
			case ShaderModel::SM_6_4:
				args.push_back(L"ds_6_4");
				break;
			case ShaderModel::SM_6_5:
				args.push_back(L"ds_6_5");
				break;
			case ShaderModel::SM_6_6:
				args.push_back(L"ds_6_6");
				break;
			case ShaderModel::SM_6_7:
				args.push_back(L"ds_6_7");
				break;
			}
			break;
		case ShaderStage::GS:
			switch (minshadermodel)
			{
			default:
				args.push_back(L"gs_6_0");
				break;
			case ShaderModel::SM_6_1:
				args.push_back(L"gs_6_1");
				break;
			case ShaderModel::SM_6_2:
				args.push_back(L"gs_6_2");
				break;
			case ShaderModel::SM_6_3:
				args.push_back(L"gs_6_3");
				break;
			case ShaderModel::SM_6_4:
				args.push_back(L"gs_6_4");
				break;
			case ShaderModel::SM_6_5:
				args.push_back(L"gs_6_5");
				break;
			case ShaderModel::SM_6_6:
				args.push_back(L"gs_6_6");
				break;
			case ShaderModel::SM_6_7:
				args.push_back(L"gs_6_7");
				break;
			}
			break;
		case ShaderStage::PS:
			switch (minshadermodel)
			{
			default:
				args.push_back(L"ps_6_0");
				break;
			case ShaderModel::SM_6_1:
				args.push_back(L"ps_6_1");
				break;
			case ShaderModel::SM_6_2:
				args.push_back(L"ps_6_2");
				break;
			case ShaderModel::SM_6_3:
				args.push_back(L"ps_6_3");
				break;
			case ShaderModel::SM_6_4:
				args.push_back(L"ps_6_4");
				break;
			case ShaderModel::SM_6_5:
				args.push_back(L"ps_6_5");
				break;
			case ShaderModel::SM_6_6:
				args.push_back(L"ps_6_6");
				break;
			case ShaderModel::SM_6_7:
				args.push_back(L"ps_6_7");
				break;
			}
			break;
		case ShaderStage::CS:
			switch (minshadermodel)
			{
			default:
				args.push_back(L"cs_6_0");
				break;
			case ShaderModel::SM_6_1:
				args.push_back(L"cs_6_1");
				break;
			case ShaderModel::SM_6_2:
				args.push_back(L"cs_6_2");
				break;
			case ShaderModel::SM_6_3:
				args.push_back(L"cs_6_3");
				break;
			case ShaderModel::SM_6_4:
				args.push_back(L"cs_6_4");
				break;
			case ShaderModel::SM_6_5:
				args.push_back(L"cs_6_5");
				break;
			case ShaderModel::SM_6_6:
				args.push_back(L"cs_6_6");
				break;
			case ShaderModel::SM_6_7:
				args.push_back(L"cs_6_7");
				break;
			}
			break;
		case ShaderStage::LIB:
			switch (minshadermodel)
			{
			default:
				args.push_back(L"lib_6_5");
				break;
			case ShaderModel::SM_6_6:
				args.push_back(L"lib_6_6");
				break;
			case ShaderModel::SM_6_7:
				args.push_back(L"lib_6_7");
				break;
			}
			break;
		default:
			assert(0);
			return;
		}

		for (auto& x : input.defines)
		{
			args.push_back(L"-D");
			vz::helper::StringConvert(x, args.emplace_back());
		}

		for (auto& x : input.include_directories)
		{
			args.push_back(L"-I");
			vz::helper::StringConvert(x, args.emplace_back());
		}

#ifdef SHADERCOMPILER_XBOX_INCLUDED
		if (input.format == ShaderFormat::HLSL6_XS)
		{
			shadercompiler::xbox::AddArguments(input, args);
		}
#endif // SHADERCOMPILER_XBOX_INCLUDED

		// Entry point parameter:
		std::wstring wentry;
		vz::helper::StringConvert(input.entrypoint, wentry);
		args.push_back(L"-E");
		args.push_back(wentry.c_str());

		// Add source file name as last parameter. This will be displayed in error messages
		std::wstring wsource;
		vz::helper::StringConvert(vz::helper::GetFileNameFromPath(input.shadersourcefilename), wsource);
		args.push_back(wsource.c_str());

		DxcBuffer Source;
		Source.Ptr = shadersourcedata.data();
		Source.Size = shadersourcedata.size();
		Source.Encoding = DXC_CP_ACP;

		struct IncludeHandler : public IDxcIncludeHandler
		{
			const CompilerInput* input = nullptr;
			CompilerOutput* output = nullptr;
			CComPtr<IDxcIncludeHandler> dxcIncludeHandler;

			HRESULT STDMETHODCALLTYPE LoadSource(
				_In_z_ LPCWSTR pFilename,                                 // Candidate filename.
				_COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource  // Resultant source object for included file, nullptr if not found.
			) override
			{
				HRESULT hr = dxcIncludeHandler->LoadSource(pFilename, ppIncludeSource);
				if (SUCCEEDED(hr))
				{
					std::string& filename = output->dependencies.emplace_back();
					vz::helper::StringConvert(pFilename, filename);
				}
				return hr;
			}
			HRESULT STDMETHODCALLTYPE QueryInterface(
				/* [in] */ REFIID riid,
				/* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) override
			{
				return dxcIncludeHandler->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef(void) override
			{
				return 0;
			}
			ULONG STDMETHODCALLTYPE Release(void) override
			{
				return 0;
			}
		} includehandler;
		includehandler.input = &input;
		includehandler.output = &output;

		hr = dxcUtils->CreateDefaultIncludeHandler(&includehandler.dxcIncludeHandler);
		assert(SUCCEEDED(hr));

		std::vector<const wchar_t*> args_raw;
		args_raw.reserve(args.size());
		for (auto& x : args)
		{
			args_raw.push_back(x.c_str());
		}

		CComPtr<IDxcResult> pResults;
		hr = dxcCompiler->Compile(
			&Source,						// Source buffer.
			args_raw.data(),			// Array of pointers to arguments.
			(uint32_t)args.size(),		// Number of arguments.
			&includehandler,		// User-provided interface to handle #include directives (optional).
			IID_PPV_ARGS(&pResults)	// Compiler output status, buffer, and errors.
		);
		assert(SUCCEEDED(hr));

		CComPtr<IDxcBlobUtf8> pErrors = nullptr;
		hr = pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
		assert(SUCCEEDED(hr));
		if (pErrors != nullptr && pErrors->GetStringLength() != 0)
		{
			output.error_message = pErrors->GetStringPointer();
		}

		HRESULT hrStatus;
		hr = pResults->GetStatus(&hrStatus);
		assert(SUCCEEDED(hr));
		if (FAILED(hrStatus))
		{
			return;
		}

		CComPtr<IDxcBlob> pShader = nullptr;
		hr = pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), nullptr);
		assert(SUCCEEDED(hr));
		if (pShader != nullptr)
		{
			output.dependencies.push_back(input.shadersourcefilename);
			output.shaderdata = (const uint8_t*)pShader->GetBufferPointer();
			output.shadersize = pShader->GetBufferSize();

			// keep the blob alive == keep shader pointer valid!
			auto internal_state = std::make_shared<CComPtr<IDxcBlob>>();
			*internal_state = pShader;
			output.internal_state = internal_state;
		}

		if (input.format == ShaderFormat::HLSL6)
		{
			CComPtr<IDxcBlob> pHash = nullptr;
			hr = pResults->GetOutput(DXC_OUT_SHADER_HASH, IID_PPV_ARGS(&pHash), nullptr);
			assert(SUCCEEDED(hr));
			if (pHash != nullptr)
			{
				DxcShaderHash* pHashBuf = (DxcShaderHash*)pHash->GetBufferPointer();
				for (int i = 0; i < _countof(pHashBuf->HashDigest); i++)
				{
					output.shaderhash.push_back(pHashBuf->HashDigest[i]);
				}
			}
		}
	}
#endif // SHADERCOMPILER_ENABLED_DXCOMPILER

#ifdef SHADERCOMPILER_ENABLED_D3DCOMPILER
	struct InternalState_D3DCompiler
	{
		using PFN_D3DCOMPILE = decltype(&D3DCompile);
		PFN_D3DCOMPILE D3DCompile = nullptr;

		InternalState_D3DCompiler()
		{
			if (D3DCompile != nullptr)
			{
				return; // already initialized
			}

			HMODULE d3dcompiler = vzLoadLibrary("d3dcompiler_47.dll");
			if (d3dcompiler != nullptr)
			{
				D3DCompile = (PFN_D3DCOMPILE)vzGetProcAddress(d3dcompiler, "D3DCompile");
				if (D3DCompile != nullptr)
				{
					backlog::post("shadercompiler: loaded d3dcompiler_47.dll");
				}
			}
		}
	};
	inline InternalState_D3DCompiler& d3d_compiler()
	{
		static InternalState_D3DCompiler internal_state;
		return internal_state;
	}

	void Compile_D3DCompiler(const CompilerInput& input, CompilerOutput& output)
	{
		if (d3d_compiler().D3DCompile == nullptr)
		{
			return;
		}

		if (input.minshadermodel > ShaderModel::SM_5_0)
		{
			output.error_message = "SHADERFORMAT_HLSL5 cannot support specified minshadermodel!";
			return;
		}

		std::vector<uint8_t> shadersourcedata;
		if (!vz::helper::FileRead(input.shadersourcefilename, shadersourcedata))
		{
			return;
		}

		D3D_SHADER_MACRO defines[] = {
			"HLSL5", "1",
			"DISABLE_WAVE_INTRINSICS", "1",
			NULL, NULL,
		};

		const char* target = nullptr;
		switch (input.stage)
		{
		default:
		case ShaderStage::MS:
		case ShaderStage::AS:
		case ShaderStage::LIB:
			// not applicable
			return;
		case ShaderStage::VS:
			target = "vs_5_0";
			break;
		case ShaderStage::HS:
			target = "hs_5_0";
			break;
		case ShaderStage::DS:
			target = "ds_5_0";
			break;
		case ShaderStage::GS:
			target = "gs_5_0";
			break;
		case ShaderStage::PS:
			target = "ps_5_0";
			break;
		case ShaderStage::CS:
			target = "cs_5_0";
			break;
		}

		struct IncludeHandler : public ID3DInclude
		{
			const CompilerInput* input = nullptr;
			CompilerOutput* output = nullptr;
			std::vector<std::vector<uint8_t>> filedatas;

			HRESULT Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes) override
			{
				for (auto& x : input->include_directories)
				{
					std::string filename = x + pFileName;
					if (!vz::helper::FileExists(filename))
						continue;
					std::vector<uint8_t>& filedata = filedatas.emplace_back();
					if (vz::helper::FileRead(filename, filedata))
					{
						output->dependencies.push_back(filename);
						*ppData = filedata.data();
						*pBytes = (UINT)filedata.size();
						return S_OK;
					}
				}
				return E_FAIL;
			}

			HRESULT Close(LPCVOID pData) override
			{
				return S_OK;
			}
		} includehandler;
		includehandler.input = &input;
		includehandler.output = &output;

		// https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/d3dcompile-constants
		UINT Flags1 = 0;
		if (has_flag(input.flags, Flags::DISABLE_OPTIMIZATION))
		{
			Flags1 |= D3DCOMPILE_SKIP_OPTIMIZATION;
		}


		CComPtr<ID3DBlob> code;
		CComPtr<ID3DBlob> errors;
		HRESULT hr = d3d_compiler().D3DCompile(
			shadersourcedata.data(),
			shadersourcedata.size(),
			input.shadersourcefilename.c_str(),
			defines,
			&includehandler, //D3D_COMPILE_STANDARD_FILE_INCLUDE,
			input.entrypoint.c_str(),
			target,
			Flags1,
			0,
			&code,
			&errors
		);

		if (errors)
		{
			output.error_message = (const char*)errors->GetBufferPointer();
		}

		if (SUCCEEDED(hr))
		{
			output.dependencies.push_back(input.shadersourcefilename);
			output.shaderdata = (const uint8_t*)code->GetBufferPointer();
			output.shadersize = code->GetBufferSize();

			// keep the blob alive == keep shader pointer valid!
			auto internal_state = std::make_shared<CComPtr<ID3D10Blob>>();
			*internal_state = code;
			output.internal_state = internal_state;
		}
	}
#endif // SHADERCOMPILER_ENABLED_D3DCOMPILER

	void Compile(const CompilerInput& input, CompilerOutput& output)
	{
		output = CompilerOutput();

#ifdef SHADERCOMPILER_ENABLED
		switch (input.format)
		{
		default:
			break;

#ifdef SHADERCOMPILER_ENABLED_DXCOMPILER
		case ShaderFormat::HLSL6:
		case ShaderFormat::SPIRV:
		case ShaderFormat::HLSL6_XS:
			Compile_DXCompiler(input, output);
			break;
#endif // SHADERCOMPILER_ENABLED_DXCOMPILER

#ifdef SHADERCOMPILER_ENABLED_D3DCOMPILER
		case ShaderFormat::HLSL5:
			Compile_D3DCompiler(input, output);
			break;
#endif // SHADERCOMPILER_ENABLED_D3DCOMPILER

#ifdef SHADERCOMPILER_PS5_INCLUDED
		case ShaderFormat::PS5:
			shadercompiler::ps5::Compile(input, output);
			break;
#endif // SHADERCOMPILER_PS5_INCLUDED

		}
#endif // SHADERCOMPILER_ENABLED
	}

	constexpr const char* shadermetaextension = "shadermeta";
	bool SaveShaderAndMetadata(const std::string& shaderfilename, const CompilerOutput& output)
	{
#ifdef SHADERCOMPILER_ENABLED
		vz::helper::DirectoryCreate(vz::helper::GetDirectoryFromPath(shaderfilename));

		vz::helper::FileWrapper dependencyLibrary(vz::helper::ReplaceExtension(shaderfilename, shadermetaextension), false);
		if (dependencyLibrary.IsOpen())
		{
			std::string rootdir = dependencyLibrary.GetSourceDirectory();
			std::vector<std::string> dependencies = output.dependencies;
			for (auto& x : dependencies)
			{
				vz::helper::MakePathRelative(rootdir, x);
			}
			dependencyLibrary << dependencies;
		}

		if (vz::helper::FileWrite(shaderfilename, output.shaderdata, output.shadersize))
		{
			return true;
		}
#endif // SHADERCOMPILER_ENABLED

		return false;
	}
	bool IsShaderOutdated(const std::string& shaderfilename)
	{
#ifdef SHADERCOMPILER_ENABLED
		std::string filepath = shaderfilename;
		vz::helper::MakePathAbsolute(filepath);
		if (!vz::helper::FileExists(filepath))
		{
			return true; // no shader file = outdated shader, apps can attempt to rebuild it
		}
		std::string dependencylibrarypath = vz::helper::ReplaceExtension(shaderfilename, shadermetaextension);
		if (!vz::helper::FileExists(dependencylibrarypath))
		{
			return false; // no metadata file = no dependency, up to date (for example packaged builds)
		}

		const uint64_t tim = vz::helper::FileTimestamp(filepath);

		vz::helper::FileWrapper dependencyLibrary(dependencylibrarypath);
		if (dependencyLibrary.IsOpen())
		{
			std::string rootdir = dependencyLibrary.GetSourceDirectory();
			std::vector<std::string> dependencies;
			dependencyLibrary >> dependencies;

			for (auto& x : dependencies)
			{
				std::string dependencypath = rootdir + x;
				vz::helper::MakePathAbsolute(dependencypath);
				if (vz::helper::FileExists(dependencypath))
				{
					const uint64_t dep_tim = vz::helper::FileTimestamp(dependencypath);

					if (tim < dep_tim)
					{
						return true;
					}
				}
			}
		}
#endif // SHADERCOMPILER_ENABLED

		return false;
	}

	std::mutex locker;
	std::unordered_set<std::string> registered_shaders;
	void RegisterShader(const std::string& shaderfilename)
	{
#ifdef SHADERCOMPILER_ENABLED
		std::scoped_lock lock(locker);
		registered_shaders.insert(shaderfilename);
#endif // SHADERCOMPILER_ENABLED
	}
	size_t GetRegisteredShaderCount()
	{
		std::scoped_lock lock(locker);
		return registered_shaders.size();
	}
	bool CheckRegisteredShadersOutdated()
	{
#ifdef SHADERCOMPILER_ENABLED
		std::scoped_lock lock(locker);
		for (auto& x : registered_shaders)
		{
			if (IsShaderOutdated(x))
			{
				return true;
			}
		}
#endif // SHADERCOMPILER_ENABLED
		return false;
	}
}
