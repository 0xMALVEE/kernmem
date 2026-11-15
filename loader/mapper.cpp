#include "mapper.h"

#include <cstdlib>
#include <cstring>

#include "log.h"
#include "nal.h"
#include "nt.h"
#include "pe.h"

#include "vgb_shared_loader.h"

namespace
{
    /* DRIVER_OBJECT is 0x150 bytes on x64; round up generously. */
    constexpr uint64_t kDriverObjectSize = 0x200;

    /* Offset of DriverUnload inside DRIVER_OBJECT (x64, stable ABI). */
    constexpr uint64_t kDriverUnloadOffset = 0x68;

    /* DriverExtension pointer (0x30) and the block it must point at. */
    constexpr uint64_t kDriverExtensionOffset = 0x30;
    constexpr uint64_t kDriverExtensionSize = 0x40;

    bool WriteToKernel(uint64_t destination, const void* source, size_t size)
    {
        return nal::MemCopy(destination, reinterpret_cast<uint64_t>(source), size);
    }

    bool ReadFromKernel(uint64_t source, void* destination, size_t size)
    {
        return nal::ReadMemory(source, destination, size);
    }
}

namespace mapper
{
    bool Map(const std::wstring& driverPath, uint32_t clientPid, MAP_RESULT* result)
    {
        if (!result)
            return false;

        ZeroMemory(result, sizeof(*result));

        pe::IMAGE_INFO image = { 0 };
        if (!pe::Load(driverPath, &image))
            return false;

        vgb_log::Step("image accepted: %llu bytes, image size 0x%llX, entry RVA 0x%llX",
                  (unsigned long long)image.rawSize,
                  (unsigned long long)image.imageSize,
                  (unsigned long long)image.entryPoint);

        /* Allocate the image in kernel pool. */
        const uint64_t kernelImage = nal::AllocatePool(image.imageSize);
        if (!kernelImage)
        {
            vgb_log::Error("kernel allocation for the image failed");
            pe::Free(&image);
            return false;
        }

        vgb_log::Step("image allocated at 0x%llX", (unsigned long long)kernelImage);

        /* Local mirror so headers and relocations can be fixed up before the
         * copy into the kernel. */
        auto localImage = static_cast<uint8_t*>(calloc(1, image.imageSize));
        if (!localImage)
        {
            nal::FreePool(kernelImage);
            pe::Free(&image);
            return false;
        }

        /* Copy the headers (sized by SizeOfHeaders, already validated), then
         * every section at its virtual offset. */
        const size_t headerSize = (size_t)image.sizeOfHeaders;
        memcpy(localImage, image.raw, headerSize);

        for (uint32_t i = 0; i < image.sectionCount; ++i)
        {
            const PIMAGE_SECTION_HEADER section = &image.sections[i];
            if (!section->SizeOfRawData)
                continue;

            memcpy(localImage + section->VirtualAddress,
                   image.raw + section->PointerToRawData,
                   section->SizeOfRawData);
        }

        /* Relocate against the real load address. */
        const uint64_t delta = kernelImage - image.preferredBase;
        if (!pe::Relocate(reinterpret_cast<uint64_t>(localImage), kernelImage, delta))
        {
            vgb_log::Error("relocation failed");
            free(localImage);
            nal::FreePool(kernelImage);
            pe::Free(&image);
            return false;
        }

        /* Resolve imports against ntoskrnl only. */
        const auto resolve = [](const std::string& module, const std::string& function) -> uint64_t
        {
            if (_stricmp(module.c_str(), "ntoskrnl.exe") != 0)
                vgb_log::Warn("import from unexpected module %s", module.c_str());

            return nal::GetKernelModuleExport(nal::ntoskrnlBase, function);
        };

        if (!pe::ResolveImports(reinterpret_cast<uint64_t>(localImage), kernelImage,
                                image.importRva, resolve))
        {
            vgb_log::Error("import resolution failed");
            free(localImage);
            nal::FreePool(kernelImage);
            pe::Free(&image);
            return false;
        }

        /* Ship the finished image into the kernel. */
        if (!WriteToKernel(kernelImage, localImage, (size_t)image.imageSize))
        {
            vgb_log::Error("copying the image into the kernel failed");
            free(localImage);
            nal::FreePool(kernelImage);
            pe::Free(&image);
            return false;
        }

        free(localImage);

        /* Minimal DRIVER_OBJECT so the entry point has something to register
         * callbacks on. */
        /*
         * The object block also carries a DRIVER_EXTENSION. A NULL
         * DriverExtension is dereferenced by several kernel paths, so it has to
         * point at real storage that itself points back at the object.
         */
        const uint64_t driverObject = nal::AllocatePool(kDriverObjectSize + kDriverExtensionSize);
        if (!driverObject)
        {
            vgb_log::Error("driver object allocation failed");
            nal::FreePool(kernelImage);
            pe::Free(&image);
            return false;
        }

        uint8_t zeroed[kDriverObjectSize + kDriverExtensionSize] = { 0 };
        WriteToKernel(driverObject, zeroed, sizeof(zeroed));

        const uint64_t extension = driverObject + kDriverObjectSize;
        const uint64_t extensionObject = driverObject;

        WriteToKernel(driverObject + kDriverExtensionOffset, &extension, sizeof(extension));
        WriteToKernel(extension, &extensionObject, sizeof(extensionObject));

        /* Init block: magic + client pid, handed over as DriverEntry arg 2. */
        const uint64_t initBlock = nal::AllocatePool(sizeof(VGB_INIT_PARAMS));
        if (!initBlock)
        {
            vgb_log::Error("init block allocation failed");
            nal::FreePool(driverObject);
            nal::FreePool(kernelImage);
            pe::Free(&image);
            return false;
        }

        VGB_INIT_PARAMS params = { 0 };
        params.Magic = VGB_INIT_MAGIC;
        params.ClientPid = clientPid;
        params.ImageBase = kernelImage;
        params.ImageSize = image.imageSize;
        WriteToKernel(initBlock, &params, sizeof(params));

        const uint64_t entryPoint = kernelImage + image.entryPoint;
        vgb_log::Step("calling DriverEntry at 0x%llX (client pid %u, object 0x%llX)",
                  (unsigned long long)entryPoint, clientPid,
                  (unsigned long long)driverObject);

        uint64_t entryStatus = 0;
        const uint64_t args[4] = { driverObject, initBlock, 0, 0 };

        if (!nal::CallKernelFunctionRaw(&entryStatus, entryPoint, args, 2))
        {
            vgb_log::Error("the kernel call for DriverEntry failed");
            nal::FreePool(initBlock);
            nal::FreePool(driverObject);
            nal::FreePool(kernelImage);
            pe::Free(&image);
            return false;
        }

        nal::FreePool(initBlock);

        result->imageBase = kernelImage;
        result->imageSize = image.imageSize;
        result->driverObject = driverObject;
        result->entryPoint = entryPoint;
        result->status = static_cast<NTSTATUS>(entryStatus);

        pe::Free(&image);

        if (!NT_SUCCESS(result->status))
        {
            vgb_log::Error("DriverEntry returned 0x%08X", (unsigned)result->status);
            nal::FreePool(driverObject);
            nal::FreePool(kernelImage);
            return false;
        }

        return true;
    }

    bool Unmap(const MAP_RESULT& result)
    {
        if (!result.imageBase)
            return false;

        /*
         * DriverUnload MUST run before the image is freed, otherwise the device
         * object is left dangling inside freed pool and any later open dispatches
         * into garbage. It was previously skipped silently whenever the kernel read
         * failed - which is exactly what happens in a fresh process that never
         * opened the host - so the free is now conditional on having read it.
         */
        if (result.driverObject)
        {
            uint64_t unload = 0;

            if (!ReadFromKernel(result.driverObject + kDriverUnloadOffset, &unload, sizeof(unload)))
            {
                vgb_log::Error("cannot read DriverUnload - image NOT freed, retry unload");
                return false;
            }

            if (unload)
            {
                vgb_log::Step("calling DriverUnload at 0x%llX", (unsigned long long)unload);
                const uint64_t args[4] = { result.driverObject, 0, 0, 0 };

                if (!nal::CallKernelFunctionRaw(nullptr, unload, args, 1))
                {
                    vgb_log::Error("DriverUnload call failed - image NOT freed");
                    return false;
                }

                vgb_log::Step("DriverUnload returned, draining IRPs");
            }

            nal::FreePool(result.driverObject);
        }

        /*
         * DriverUnload deleted the device, but an IRP already past the device
         * lookup can still be executing inside this image. Freeing the pool the
         * instant DriverUnload returns leaves it dispatching into freed memory,
         * which is a bugcheck on a machine that was only closing a handle.
         * The driver also sets its own unloading flag; this drain covers the
         * window before that flag is observable.
         */
        Sleep(250);

        nal::FreePool(result.imageBase);
        return true;
    }
}
