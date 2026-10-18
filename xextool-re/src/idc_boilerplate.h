// idc_boilerplate.h - fixed text of xextool's -i IDC output, extracted
// verbatim from a reference run (LF here; the emitter writes CRLF).
// Runtime-generated: SetupSections, SetupResources, setupImports_*,
// SetupImports, SetupExports. Everything else is this, byte-for-byte.
#pragma once

static const char kIdcHead[] = R"IDC(//
// Xbox360 Basefile Info - Created by XexTool
//

#include <idc.idc>
#include <x360_imports.idc>


static MakeNameForce(addr, name)
{
    auto num, name_fixed;
    if( MakeNameEx(addr, name, SN_NOWARN) )
        return;
    for(num=0; num<999; num++)
    {
        name_fixed = form("%s_%d", name, num);
        if( MakeNameEx(addr, name_fixed, SN_NOWARN) )
            return;
    }
}

static GetSectionAddr(sectName)
{
	auto seg_addr, seg_base;
	seg_base = SegByName(sectName);
	return SegByBase(seg_base);
}

static SetupSection(startAddr, endAddr, segClass, perms, name, base)
{
    SetSelector(base, 0);
    SegCreate(startAddr, endAddr, base, 1, 3, 2);
    SegClass(startAddr, segClass);
    SegRename(startAddr, name);
    SetSegmentAttr(startAddr, SEGATTR_PERM, perms); // 4=read, 2=write, 1=execute
    SetSegmentAttr(startAddr, SEGATTR_FLAGS, 0x10); // SFL_LOADER
    SegDefReg(startAddr, "%r26", 0);
    SegDefReg(startAddr, "%r27", 0);
    SegDefReg(startAddr, "%r28", 0);
    SegDefReg(startAddr, "%r29", 0);
    SegDefReg(startAddr, "%r30", 0);
    SegDefReg(startAddr, "%r31", 0);
}

)IDC";

static const char kIdcMid1[] = R"IDC(
static RemoveEmptySections()
{
    auto seg_addr, seg_num;
    for(seg_num=0; seg_num<500; seg_num=seg_num+1)
    {
        seg_addr = GetSectionAddr(form( "seg%03d", seg_num) );
        if(seg_addr != -1)
            SegDelete(seg_addr, 1);
    }
}


static SetupImportFunc(importAddr, funcAddr, importNum, name)
{
    auto func_name;
    func_name = DoNameGen(name, 0, importNum);

    MakeNameForce(importAddr, "__imp__" + func_name);
    MakeDword(importAddr);

    PatchWord(funcAddr, 0x3860);
    PatchWord(funcAddr + 4, 0x3880);
    MakeUnknown(funcAddr, 0x10, 0); // DOUNK_SIMPLE
    MakeCode(funcAddr);
    MakeNameForce(funcAddr, func_name);
    MakeFunction(funcAddr, funcAddr + 0x10);
    SetFunctionFlags(funcAddr, FUNC_LIB);
}

static SetupImportData(importAddr, importNum, name)
{
    auto data_name;
    data_name = DoNameGen(name, 0, importNum);

    MakeNameForce(importAddr, data_name);
    MakeDword(importAddr);
}

)IDC";

static const char kIdcMid2[] = R"IDC(

static SetupExportFunc(funcAddr, exportNum, funcName)
{
    MakeUnkn(funcAddr, 0);
    MakeCode(funcAddr); 
    MakeNameForce(funcAddr, funcName);
    MakeFunction(funcAddr, BADADDR);
    AddEntryPoint(exportNum, funcAddr, funcName, 1);
}

static SetupExportData(dataAddr, exportNum, name)
{
    auto data_name;
    data_name = DoNameGen(name, 0, exportNum);

    AddEntryPoint(exportNum, dataAddr, data_name, 0);
    MakeNameForce(dataAddr, data_name);
    MakeDword(dataAddr);
}

)IDC";

static const char kIdcTail[] = R"IDC(
static SetupExportsByName()
{
}

static SetupRegSaves()
{
	auto currAddr, i;
	
	// find all saves of gp regs
	for(currAddr=0; currAddr != BADADDR; currAddr=currAddr+4)
	{
		// find "std %r14, -0x98(%sp)" followed by "std %r15, -0x90(%sp)"
		currAddr = FindBinary(currAddr, SEARCH_DOWN, "F9 C1 FF 68 F9 E1 FF 70");
		if(currAddr == BADADDR)
			break;
		for(i=14; i<=31; i++)
		{
			MakeUnknown(currAddr, 4, 0); // DOUNK_SIMPLE
			MakeCode(currAddr);
			if(i != 31)
				MakeFunction(currAddr, currAddr + 4);
			else
				MakeFunction(currAddr, currAddr + 0x0C);
			MakeNameForce(currAddr, form("__savegprlr_%d", i));
			currAddr = currAddr + 4;
		}
	}
	
	// find all loads of gp regs
	for(currAddr=0; currAddr != BADADDR; currAddr=currAddr+4)
	{
		// find "ld  %r14, var_98(%sp)" followed by "ld  %r15, var_90(%sp)"
		currAddr = FindBinary(currAddr, SEARCH_DOWN, "E9 C1 FF 68 E9 E1 FF 70");
		if(currAddr == BADADDR)
			break;
		for(i=14; i<=31; i++)
		{
			MakeUnknown(currAddr, 4, 0); // DOUNK_SIMPLE
			MakeCode(currAddr);
			if(i != 31)
				MakeFunction(currAddr, currAddr + 4);
			else
				MakeFunction(currAddr, currAddr + 0x10);
			MakeNameForce(currAddr, form("__restgprlr_%d", i));
			currAddr = currAddr + 4;
		}
	}
}

static ConvertToCode(startAddr, endAddr)
{
    auto addr;
    if(startAddr == BADADDR || endAddr == BADADDR || startAddr>endAddr)
        return;
    
    MakeUnknown(startAddr, endAddr-startAddr, 0); // DOUNK_SIMPLE
    for(addr=startAddr&0xFFFFFFFC; addr<endAddr; addr=addr+4)
    {
        MakeCode(addr);
    }
    AnalyzeArea(startAddr, endAddr);
}

static main()
{
    // ensure file was loaded in as binary
    // if it was loaded in as PE then addresses will be incorrect
    if( GetShortPrm(INF_FILETYPE) != FT_BIN )
    {
        Warning("The file must be loaded as a BINARY file to use this script.\n"
                "Close this database and create a new one, ensuring you\n"
                "select \"Binary File\" on IDAs \"Load a new file\" dialog.");
        return;
    }
    
    // ensure file was loaded in as PPC
    if( GetCharPrm(INF_PROCNAME+0) != 'P' ||
        GetCharPrm(INF_PROCNAME+1) != 'P' ||
        GetCharPrm(INF_PROCNAME+2) != 'C' ||
        GetCharPrm(INF_PROCNAME+3) != '\0' )
    {
        Warning("The file must be loaded for the PPC processor.\n"
                "Close this database and create a new one, ensuring you\n"
                "select \"PowerPC: ppc\" on IDAs \"Load a new file\" dialog.");
        return;
    }

    // set up resources
    if( 1 == AskYN(0, "Would you like to load reources as segments?") )
        SetupResources();

    // set up sections
    SetupSections();

    // remove empty sections
    RemoveEmptySections();

    // analyse code
    if( 1 == AskYN(1, "Would you like to analyse the file as code?") )
        ConvertToCode( GetSectionAddr(".text"), SegEnd(GetSectionAddr(".text")) );

    // set up imports
    SetupImports();

    // set up exports
    SetupExports();

    // set up exports by name
    SetupExportsByName();

    // setup all reg loads/stores
    SetupRegSaves();

    // done
    Message("done\n\n");
}

)IDC";
