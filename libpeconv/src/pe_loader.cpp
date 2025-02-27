#include "peconv/pe_loader.h"

#include "peconv/relocate.h"
#include "peconv/imports_loader.h"
#include "peconv/buffer_util.h"
#include "peconv/function_resolver.h"
#include "peconv/exports_lookup.h"

#include <tchar.h>
#include <iostream>

using namespace peconv;

namespace peconv {
    BYTE* load_no_sec_pe(BYTE* dllRawData, size_t r_size, OUT size_t &v_size, bool executable)
    {
        ULONG_PTR desired_base = 0;
        size_t out_size = (r_size < PAGE_SIZE) ? PAGE_SIZE : r_size;
        if (executable) {
            desired_base = get_image_base(dllRawData);
            out_size = peconv::get_image_size(dllRawData);
        }
        DWORD protect = (executable) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
        BYTE* mappedPE = peconv::alloc_pe_buffer(out_size, protect, reinterpret_cast<void*>(desired_base));
        if (!mappedPE) {
            return nullptr;
        }
        memcpy(mappedPE, dllRawData, r_size);
        v_size = out_size;
        return mappedPE;
    }
};

BYTE* peconv::load_pe_module(BYTE* dllRawData, size_t r_size, OUT size_t &v_size, bool executable, bool relocate, ULONG_PTR desired_base)
{
    if (!peconv::get_nt_hdrs(dllRawData, r_size)) {
        return nullptr;
    }
    if (peconv::get_sections_count(dllRawData, r_size) == 0) {
        return load_no_sec_pe(dllRawData, r_size, v_size, executable);
    }
    // by default, allow to load the PE at the supplied base
    // if relocating is required, but the PE has no relocation table...
    if (relocate && !has_relocations(dllRawData)) {
        // ...enforce loading the PE image at its default base (so that it will need no relocations)
        desired_base = get_image_base(dllRawData);
    }
    // load a virtual image of the PE file at the desired_base address (random if desired_base is NULL):
    BYTE *mappedDLL = pe_raw_to_virtual(dllRawData, r_size, v_size, executable, desired_base);
    if (mappedDLL) {
        //if the image was loaded at its default base, relocate_module will return always true (because relocating is already done)
        if (relocate && !relocate_module(mappedDLL, v_size, (ULONGLONG)mappedDLL)) {
            // relocating was required, but it failed - thus, the full PE image is useless
            std::cerr << "[!] Could not relocate the module!\n";
            free_pe_buffer(mappedDLL, v_size);
            mappedDLL = nullptr;
        }
    } else {
        std::cerr << "[!] Could not allocate memory at the desired base!\n";
    }
    return mappedDLL;
}

BYTE* peconv::load_pe_module(LPCTSTR filename, OUT size_t &v_size, bool executable, bool relocate, ULONG_PTR desired_base)
{
    size_t r_size = 0;
    BYTE *dllRawData = load_file(filename, r_size);
    if (!dllRawData) {
#ifdef _DEBUG
        std::cerr << "Cannot load the file: " << filename << std::endl;
#endif
        return nullptr;
    }
    BYTE* mappedPE = load_pe_module(dllRawData, r_size, v_size, executable, relocate, desired_base);
    free_file(dllRawData);
    return mappedPE;
}

BYTE* peconv::load_pe_executable(BYTE* dllRawData, size_t r_size, OUT size_t &v_size, t_function_resolver* import_resolver, ULONG_PTR desired_base)
{
    BYTE* loaded_pe = load_pe_module(dllRawData, r_size, v_size, true, true, desired_base);
    if (!loaded_pe) {
        std::cerr << "[-] Loading failed!\n";
        return nullptr;
    }
#if _DEBUG
    printf("Loaded at: %p\n", loaded_pe);
#endif
    if (has_valid_import_table(loaded_pe, v_size)) {
        if (!load_imports(loaded_pe, import_resolver)) {
            printf("[-] Loading imports failed!");
            free_pe_buffer(loaded_pe, v_size);
            return NULL;
        }
    }
    else {
        printf("[-] PE doesn't have a valid Import Table!\n");
    }
    return loaded_pe;
}


BYTE* peconv::load_pe_executable(LPCTSTR my_path, OUT size_t &v_size, t_function_resolver* import_resolver)
{
#if _DEBUG
    _tprintf(TEXT("Module: %s\n"), my_path);
#endif
    BYTE* loaded_pe = load_pe_module(my_path, v_size, true, true);
    if (!loaded_pe) {
         printf("[-] Loading failed!\n");
        return NULL;
    }
#if _DEBUG
    printf("Loaded at: %p\n", loaded_pe);
#endif
    if (!load_imports(loaded_pe, import_resolver)) {
        printf("[-] Loading imports failed!");
        free_pe_buffer(loaded_pe, v_size);
        return nullptr;
    }
    return loaded_pe;
}
