/**
 @file patches.cpp

 @brief Implements the patches class.
 */

#include "patches.h"
#include "addrinfo.h"
#include "memory.h"
#include "debugger.h"
#include "console.h"

/**
 @brief The patches.
 */

PatchesInfo patches;

/**
 @fn bool patchset(uint addr, unsigned char oldbyte, unsigned char newbyte)

 @brief Patchsets.

 @param addr    The address.
 @param oldbyte The oldbyte.
 @param newbyte The newbyte.

 @return true if it succeeds, false if it fails.
 */

bool patchset(uint addr, unsigned char oldbyte, unsigned char newbyte)
{
    if(!DbgIsDebugging() || !memisvalidreadptr(fdProcessInfo->hProcess, addr))
        return false;
    if(oldbyte == newbyte)
        return true; //no need to make a patch for a byte that is equal to itself
    PATCHINFO newPatch;
    newPatch.addr = addr - modbasefromaddr(addr);
    modnamefromaddr(addr, newPatch.mod, true);
    newPatch.oldbyte = oldbyte;
    newPatch.newbyte = newbyte;
    uint key = modhashfromva(addr);
    PatchesInfo::iterator found = patches.find(key);
    if(found != patches.end()) //we found a patch on the specified address
    {
        if(found->second.oldbyte == newbyte) //patch is undone
        {
            patches.erase(found);
            return true;
        }
        else
        {
            newPatch.oldbyte = found->second.oldbyte; //keep the original byte from the previous patch
            found->second = newPatch;
        }
    }
    else
        patches.insert(std::make_pair(key, newPatch));
    return true;
}

/**
 @fn bool patchget(uint addr, PATCHINFO* patch)

 @brief Patchgets.

 @param addr           The address.
 @param [in,out] patch If non-null, the patch.

 @return true if it succeeds, false if it fails.
 */

bool patchget(uint addr, PATCHINFO* patch)
{
    if(!DbgIsDebugging())
        return false;
    PatchesInfo::iterator found = patches.find(modhashfromva(addr));
    if(found == patches.end()) //not found
        return false;
    if(patch)
    {
        *patch = found->second;
        patch->addr += modbasefromaddr(addr);
        return true;
    }
    return (found->second.oldbyte != found->second.newbyte);
}

/**
 @fn bool patchdel(uint addr, bool restore)

 @brief Patchdels.

 @param addr    The address.
 @param restore true to restore.

 @return true if it succeeds, false if it fails.
 */

bool patchdel(uint addr, bool restore)
{
    if(!DbgIsDebugging())
        return false;
    PatchesInfo::iterator found = patches.find(modhashfromva(addr));
    if(found == patches.end()) //not found
        return false;
    if(restore)
        memwrite(fdProcessInfo->hProcess, (void*)(found->second.addr + modbasefromaddr(addr)), &found->second.oldbyte, sizeof(char), 0);
    patches.erase(found);
    return true;
}

/**
 @fn void patchdelrange(uint start, uint end, bool restore)

 @brief Patchdelranges.

 @param start   The start.
 @param end     The end.
 @param restore true to restore.
 */

void patchdelrange(uint start, uint end, bool restore)
{
    if(!DbgIsDebugging())
        return;
    bool bDelAll = (start == 0 && end == ~0); //0x00000000-0xFFFFFFFF
    uint modbase = modbasefromaddr(start);
    if(modbase != modbasefromaddr(end))
        return;
    start -= modbase;
    end -= modbase;
    PatchesInfo::iterator i = patches.begin();
    while(i != patches.end())
    {
        if(bDelAll || (i->second.addr >= start && i->second.addr < end))
        {
            if(restore)
                memwrite(fdProcessInfo->hProcess, (void*)(i->second.addr + modbase), &i->second.oldbyte, sizeof(char), 0);
            patches.erase(i++);
        }
        else
            i++;
    }
}

/**
 @fn void patchclear(const char* mod)

 @brief Patchclears the given modifier.

 @param mod The modifier.
 */

void patchclear(const char* mod)
{
    if(!mod or !*mod)
        patches.clear();
    else
    {
        PatchesInfo::iterator i = patches.begin();
        while(i != patches.end())
        {
            if(!_stricmp(i->second.mod, mod))
                patches.erase(i++);
            else
                i++;
        }
    }
}

/**
 @fn bool patchenum(PATCHINFO* patcheslist, size_t* cbsize)

 @brief Patchenums.

 @param [in,out] patcheslist If non-null, the patcheslist.
 @param [in,out] cbsize      If non-null, the cbsize.

 @return true if it succeeds, false if it fails.
 */

bool patchenum(PATCHINFO* patcheslist, size_t* cbsize)
{
    if(!DbgIsDebugging())
        return false;
    if(!patcheslist && !cbsize)
        return false;
    if(!patcheslist && cbsize)
    {
        *cbsize = patches.size() * sizeof(LOOPSINFO);
        return true;
    }
    int j = 0;
    for(PatchesInfo::iterator i = patches.begin(); i != patches.end(); ++i, j++)
    {
        patcheslist[j] = i->second;
        uint modbase = modbasefromname(patcheslist[j].mod);
        patcheslist[j].addr += modbase;
    }
    return true;
}

/**
 @fn int patchfile(const PATCHINFO* patchlist, int count, const char* szFileName, char* error)

 @brief Patchfiles.

 @param patchlist      The patchlist.
 @param count          Number of.
 @param szFileName     Filename of the file.
 @param [in,out] error If non-null, the error.

 @return An int.
 */

int patchfile(const PATCHINFO* patchlist, int count, const char* szFileName, char* error)
{
    if(!count)
    {
        if(error)
            strcpy(error, "no patches to apply");
        return -1;
    }
    char modname[MAX_MODULE_SIZE] = "";
    strcpy(modname, patchlist[0].mod);
    //check if all patches are in the same module
    for(int i = 0; i < count; i++)
        if(_stricmp(patchlist[i].mod, modname))
        {
            if(error)
                sprintf(error, "not all patches are in module %s", modname);
            return -1;
        }
    uint modbase = modbasefromname(modname);
    if(!modbase) //module not loaded
    {
        if(error)
            sprintf(error, "failed to get base of module %s", modname);
        return -1;
    }
    char szOriginalName[MAX_PATH] = "";
    if(!GetModuleFileNameExA(fdProcessInfo->hProcess, (HMODULE)modbase, szOriginalName, MAX_PATH))
    {
        if(error)
            sprintf(error, "failed to get module path of module %s", modname);
        return -1;
    }
    if(!CopyFileA(szOriginalName, szFileName, false))
    {
        if(error)
            strcpy(error, "failed to make a copy of the original file (patch target is in use?)");
        return -1;
    }
    HANDLE FileHandle;
    DWORD LoadedSize;
    HANDLE FileMap;
    ULONG_PTR FileMapVA;
    if(StaticFileLoad((char*)szFileName, UE_ACCESS_ALL, false, &FileHandle, &LoadedSize, &FileMap, &FileMapVA))
    {
        int patched = 0;
        for(int i = 0; i < count; i++)
        {
            unsigned char* ptr = (unsigned char*)ConvertVAtoFileOffsetEx(FileMapVA, LoadedSize, modbase, patchlist[i].addr, false, true);
            if(!ptr) //skip patches that do not have a raw address
                continue;
            dprintf("patch%.4d|%s[%.8X]:%.2X/%.2X->%.2X\n", i + 1, modname, ptr - FileMapVA, *ptr, patchlist[i].oldbyte, patchlist[i].newbyte);
            *ptr = patchlist[i].newbyte;
            patched++;
        }
        if(!StaticFileUnload((char*)szFileName, true, FileHandle, LoadedSize, FileMap, FileMapVA))
        {
            if(error)
                strcpy(error, "StaticFileUnload failed");
            return -1;
        }
        return patched;
    }
    strcpy(error, "StaticFileLoad failed");
    return -1;
}
